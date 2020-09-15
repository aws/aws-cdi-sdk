// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the transmit-side CDI test application logic.
 *
 * See @ref cdi_test_tx for diagrams and detailed description of the transmit-side program flow.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "test_transmitter.h"

#include <inttypes.h>

#include "configuration.h"
#include "logger_api.h"
#include "cdi_os_api.h"
#include "run_test.h"
#include "cdi_utility_api.h"
#include "test_control.h"
#include "test_dynamic.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief When computing SGL entry size, this chooses what percent to reduce each entry by compared to the last.
#define SGL_ENTRY_SIZE_CALC_PERCENT (90)

/// @brief Structure used to pass arguments to InitStaticBufferContents() through CdiPoolForEachItem().
typedef struct {
    void* src_ptr; ///< Address of the source buffer to be copied to a transmit buffer.
    int size;      ///< The size of the data buffer to be copied.
} InitFunctionArgs;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * This function used when we are waiting for a signal, but also want to abort on a single other abort signal.
 *
 * @param this_signal  The signal we are waiting for.
 * @param abort_signal  The signal that will abort this wait.
 * @param timeout_ms    A timeout in milliseconds that will also cause an abort if it is reached.
 *
 * @return true if this_signal is set first; false if either abort_signal is set first or we time out.
 */
static bool TestWaitSignalOrAbort(const CdiSignalType this_signal, const CdiSignalType abort_signal,
                                  const int timeout_ms)
{
    // Wait for the rate timer signal OR a shutdown signal.

    // Create an array of signal handles that we want to monitor.
    CdiSignalType signal_array[2];
    signal_array[0] = this_signal;
    signal_array[1] = abort_signal;

    // Wait for either signal.
    uint32_t signal_index;
    CdiOsSignalsWait(signal_array, 2, false, timeout_ms, &signal_index);
    if (0 != signal_index) {
        // Wait was aborted (signal_index=1) or timed-out (signal_index=OS_SIG_TIMEOUT).
        return false;
    }

    // The expected signal occurred.
    return true;
}

/**
 * Free resources used for a payload.
 *
 * @param connection_info_ptr Pointer to connection info state data.
 * @param user_data_ptr Pointer to Tx user data structure passed along with a payload.
 */
static void FreePayloadResources(TestConnectionInfo* connection_info_ptr, TestTxUserData* user_data_ptr)
{
    if (user_data_ptr) {
        // If payload buffer exists, return it to the memory pool.
        if (user_data_ptr->tx_payload_sgl_ptr) {
            CdiPoolPut(user_data_ptr->tx_pool_handle, user_data_ptr->tx_payload_sgl_ptr);
            user_data_ptr->tx_payload_sgl_ptr = NULL;
        }

        CdiPoolPut(connection_info_ptr->tx_user_data_pool_handle, user_data_ptr);
    }
}

/**
 * Copies some configuration information from a src_sgl_ptr to ret_sgl_ptr and sets the ret_sgl_ptr to the
 * next_payload_size total and entry size. This is explicitly for use with linear buffers and is intended to facilitate
 * sending variable sized payloads via RIFF files.
 *
 * @param connection_info_ptr Pointer to connection info state data.
 * @param stream_info_ptr Pointer to stream info state data.
 * @param src_sgl_ptr Pointer to an SGL from an initialized pool.
 * @param ret_sgl_ptr Pointer to a modifiable SGL that is getting initialized.
 *
 * @return true if successful or false on failure
 */
static bool RiffSgl(TestConnectionInfo* connection_info_ptr, TestConnectionStreamInfo* stream_info_ptr,
                    const CdiSgList* src_sgl_ptr, CdiSgList* ret_sgl_ptr)
{
    bool return_val = true;

    if (src_sgl_ptr == NULL) {
        CDI_LOG_THREAD(kLogError, "Invalid source SGL pointer provided");
        return_val = false;
    }

    if (ret_sgl_ptr == NULL) {
        CDI_LOG_THREAD(kLogError, "Invalid destination SGL pointer provided.");
        return_val = false;
    }

    if (connection_info_ptr->test_settings_ptr->buffer_type != kCdiLinearBuffer) {
        CDI_LOG_THREAD(kLogError, "RIFF payloads must use a linear memory buffer");
        return_val = false;
    }

    if (return_val && (stream_info_ptr->next_payload_size > stream_info_ptr->payload_buffer_size)) {
        CDI_LOG_THREAD(kLogError, "RIFF payload size is larger than allocated buffer size");
        return_val = false;
    }

    if (return_val) {
        ret_sgl_ptr->total_data_size = stream_info_ptr->next_payload_size;
        ret_sgl_ptr->sgl_head_ptr->size_in_bytes = stream_info_ptr->next_payload_size;
        ret_sgl_ptr->sgl_head_ptr->address_ptr = src_sgl_ptr->sgl_head_ptr->address_ptr;
        ret_sgl_ptr->internal_data_ptr = src_sgl_ptr->internal_data_ptr;
        ret_sgl_ptr->sgl_head_ptr->internal_data_ptr = src_sgl_ptr->sgl_head_ptr->internal_data_ptr;
    }

    return return_val;
}

/**
 * Construct a payload of the requested type and send it to the SDK.
 *
 * @param connection_info_ptr  Pointer to the TestConnectionInfo data structure describing this connection.
 * @param stream_index Index of stream.
 * @param payload_count Current payload number.
 * @param ptp_rate_count Current PTP rate count.
 * @param resend If resending the same payload use true, otherwise false.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus TestTxSendPayload(TestConnectionInfo* connection_info_ptr, int stream_index, int payload_count,
                                         int ptp_rate_count, bool resend)
{
    CdiReturnStatus rs = kCdiStatusOk;
    bool got_error = false;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];

    // Get a user data buffer from the user data memory pool associated with this connection. When done with the buffer,
    // it must be freed using CdiPoolPut(). This is normally done as part of the Tx payload callback. However, if this
    // function fails, the user data will be returned to the memory pool in this function. For both cases, see
    // FreePayloadResources().
    TestTxUserData* user_data_ptr = NULL;
    got_error = !CdiPoolGet(connection_info_ptr->tx_user_data_pool_handle, (void**)&user_data_ptr);

    // Create TX Payload.
    if (!got_error) {
        // Assign our SGL and connection info pointers to the TestTxUserData structure. The pointer to that structure
        // will be sent in the Tx user data field so that our Tx callback routine can tell which connection it is from.
        user_data_ptr->test_connection_info_ptr = connection_info_ptr;

        // Set the stream index so it can be referenced in the Tx callback.
        user_data_ptr->stream_index = stream_index;

        // If using a RIFF payload, grab the new payload size. If a retry occurs do not grab the size again.
        if (!resend && stream_settings_ptr->riff_file && stream_info_ptr->user_data_read_file_handle) {
            got_error = !GetNextRiffPayloadSize(connection_info_ptr, stream_settings_ptr,
                                                stream_info_ptr->user_data_read_file_handle,
                                                &stream_info_ptr->next_payload_size);
        }
    }

    // Get a payload buffer from the payload memory pool associated with this stream. When done with the buffer, it must
    // be freed using CdiPoolPut(). This is normally done as part of the Tx payload callback. However, if this function
    // fails, the user data will be returned to the memory pool in this function. For both cases, see
    // FreePayloadResources().
    CdiSgList* pool_sgl_ptr = NULL;
    if (!got_error) {
        got_error = !CdiPoolGet(stream_info_ptr->tx_pool_handle, (void**)&pool_sgl_ptr);

        // Copy the current pool and buffer SGL address to the user data so it can later be freed when the Tx payload
        // callback is made or an error occurs. Buffer is freed in FreePayloadResources().
        user_data_ptr->tx_pool_handle = stream_info_ptr->tx_pool_handle;
        user_data_ptr->tx_payload_sgl_ptr = pool_sgl_ptr;
    }


    CdiSgList* sgl_ptr = NULL;
    CdiSglEntry local_entry = {
        .address_ptr = NULL,
        .size_in_bytes = 0,
        .next_ptr = NULL
    };
    CdiSgList local_linear_sgl = {
        .sgl_head_ptr = &local_entry,
        .sgl_tail_ptr = &local_entry,
        .total_data_size = 0
    };
    // Riff file payload sizes are specified in the payload so the pool SGL is copied to a local SGL and the local SGL
    // is configured for the size of the new RIFF payload.
    if (!got_error && stream_settings_ptr->riff_file) {
        got_error = !RiffSgl(connection_info_ptr, stream_info_ptr, pool_sgl_ptr, &local_linear_sgl);
        sgl_ptr = &local_linear_sgl;
    } else {
        sgl_ptr = pool_sgl_ptr;
    }

    if (!resend && !got_error) {
        // Either load the next payload from file, or update the first word of the buffer if we are using patterns.
        got_error = !GetNextPayloadDataSgl(connection_info_ptr, stream_settings_ptr->stream_id, payload_count,
                                           stream_info_ptr->user_data_read_file_handle, sgl_ptr);
    }

    // Set up data that is common to both connection protocol types.
    CdiCoreTxPayloadConfig core_config_data = { 0 };

    // To provide validation that the CDI SDK is passing the RTP timestamp value correctly through its pipeline, we
    // are using the current payload count as the RTP origination_timestamp. The Receiver will validate that the value
    // it receives matches the expected payload count.
    core_config_data.core_extra_data.origination_ptp_timestamp =
            GetPtpTimestamp(connection_info_ptr, stream_settings_ptr, stream_info_ptr, ptp_rate_count);
#ifdef DEBUG_RX_BUFFER
    CDI_LOG_THREAD(kLogInfo, "[%d] TxTimestamp[%d.%d]", stream_index,
                   core_config_data.core_extra_data.origination_ptp_timestamp.seconds,
                   core_config_data.core_extra_data.origination_ptp_timestamp.nanoseconds);
#endif

    // Encode the Tx payload counter and the respective connection into the payload_user_data field. The receive side
    // will expect this and report it.
    core_config_data.core_extra_data.payload_user_data = (uint64_t)( (connection_info_ptr->my_index & 0xFF)
                                      | ((uint64_t)(payload_count & 0xFF) << 8)
                                      | ((uint64_t)(stream_settings_ptr->stream_id & 0xFFFF) << 16)
                                      | (((uint64_t)ptp_rate_count) << 32) );

    // Load user_cb_param with TestTxUserData from above.  We will expect to use user_data_ptr in our Tx Callback
    // routine so that we can return our per-payload data structures to their respective pools at that time.
    core_config_data.user_cb_param = (void*)user_data_ptr;

    if (!got_error) {
        // Save current time just prior to invoking the SDK API Tx function. This will be used to determine how long the
        // SDK takes to transmit the payload.
        user_data_ptr->tx_payload_start_time = CdiOsGetMicroseconds();

        // If we are sending a RAW payload, then we are done... send it.
        if (kTestProtocolRaw == test_settings_ptr->connection_protocol) {
            // Send the RAW Payload.
            rs = CdiRawTxPayload(connection_info_ptr->connection_handle, &core_config_data, sgl_ptr,
                                 test_settings_ptr->tx_timeout);
        // If we are sending an AVM payload, then we need to add the AVM configuration data to the payload request.
        } else {
            // Create a structure to use.
            CdiAvmTxPayloadConfig payload_cfg_data;

            // Setup core config data.
            payload_cfg_data.core_config_data = core_config_data;

            // Complete the AVM extra data field.
            payload_cfg_data.avm_extra_data.stream_identifier = stream_settings_ptr->stream_id;

            // We only send video and audio config data every N payloads based on the user input --config_skip, which
            // defines how many payloads to skip after sending config data before sending it again. Below, we manage the
            // counter for skipping the requested number of payloads, and set the boolean send_config if this payload should
            // have config data sent with it.
            bool send_config = false;
            if (stream_info_ptr->config_payload_skip_count == stream_settings_ptr->config_skip) {
                stream_info_ptr->config_payload_skip_count = 0;
                send_config = true;
            } else {
                stream_info_ptr->config_payload_skip_count++;
            }

            // Size of the unit this stream's payload is transfering (pixels, audio samples, etc.,).
            payload_cfg_data.core_config_data.unit_size = stream_settings_ptr->unit_size;

            CdiAvmConfig* avm_config_ptr = send_config ? &stream_settings_ptr->avm_config : NULL;
            rs = CdiAvmTxPayload(connection_info_ptr->connection_handle, &payload_cfg_data, avm_config_ptr,
                                 sgl_ptr, test_settings_ptr->tx_timeout);
        }
    }
    // Convert any errors into a CdiReturnStatus enum.
    if (got_error && (kCdiStatusOk == rs || kCdiStatusAllocationFailed == rs)) {
        rs = kCdiStatusFatal;
    }

    if (kCdiStatusOk != rs) {
        // Free payload resources.
        FreePayloadResources(connection_info_ptr, user_data_ptr);
    }

    return rs;
}

/**
 * Try to send a payload for a given stream, handling retries and timeouts.
 *
 * @param   connection_info_ptr  Pointer to the TestConnectionInfo data structure describing this connection.
 * @param   stream_index The current stream number.
 * @param   payload_count The current payload number.
 * @param   rate_next_start_time The time that the current payload period is set to expire.
 * @param   ptp_rate_count The current PTP rate count.
 * @param   is_queued_ptr Address where to write returned queue status. Writes true if queued, otherwise writes false.
 *
 * @return  True if no errors; false if errors;
 */
static bool TestTxTrySendStreamPayload(TestConnectionInfo* connection_info_ptr, int stream_index, int payload_count,
                                       uint64_t rate_next_start_time, int ptp_rate_count, bool* is_queued_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];

    CdiReturnStatus rs = kCdiStatusOk;
    bool late_payload = false;
    bool got_error = false;
    uint64_t current_time = 0;
    *is_queued_ptr = false;
    // The connection may be interrupted at any time, so ensure we are connected to remote target before attempting to
    // send a payload.
    CdiConnectionStatus status = connection_info_ptr->connection_status;
    if (kCdiConnectionStatusConnected != status) {
        got_error = !TestWaitForConnection(connection_info_ptr, GetGlobalTestSettings()->connection_timeout_seconds);
    }

    if (!got_error) {
        int rate_period_microseconds = test_settings_ptr->rate_period_microseconds;
        bool resend_payload = false;
        int tx_queue_full_count = 0;
        do {
            // Try queuing to send the payload.
            rs = TestTxSendPayload(connection_info_ptr, stream_index, payload_count, ptp_rate_count, resend_payload);
            if (kCdiStatusQueueFull != rs) {
                resend_payload = false;
                current_time = CdiOsGetMicroseconds();
#ifdef DISABLE_RATE_TIMEOUT_FOR_DEBUG
                // Reset next start time to allow debugging (using breakpoints).
                rate_next_start_time = current_time + rate_period_microseconds;
#endif
                // If we're over the timing budget, then mark it as late. Only count a late payload once.
                if (rate_next_start_time < current_time && !late_payload) {
                    late_payload = true; // Payload is late.
                    connection_info_ptr->tx_late_payload_count++;
                    // Continue as normal if using keep_alive; otherwise, set error.
                    got_error = !test_settings_ptr->keep_alive;
                }
            }
            if (!got_error) {
                if (kCdiStatusQueueFull == rs) {
                    // If the Tx queue is full, retry until we run out of our timing budget.
                    resend_payload = true; // Set flag that we are going to resend the payload.
                    int sleep_time = rate_period_microseconds / TX_QUEUE_FULL_RATE_PERIOD_SLEEP_DIVISOR;
                    if (0 == sleep_time) {
                        sleep_time = 1; // Want to sleep for at least some amount of time.
                    }
                    tx_queue_full_count++;
                    CdiOsSleepMicroseconds(sleep_time);
                } else if (kCdiStatusNotConnected == rs) {
                    resend_payload = true;
                    got_error = !TestWaitForConnection(connection_info_ptr,
                                                       GetGlobalTestSettings()->connection_timeout_seconds);
                } else {
                    *is_queued_ptr = true;
                }
            }
        } while (resend_payload && !got_error);

        if (tx_queue_full_count) {
            CDI_LOG_THREAD(kLogWarning,
                           "Connection[%s] Stream ID[%d] Tx queue was full. Slept for [%d]microseconds between each of "
                           "[%d]retries.", test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id,
                           rate_period_microseconds / TX_QUEUE_FULL_RATE_PERIOD_SLEEP_DIVISOR,
                           tx_queue_full_count);
        }
    }

    if (late_payload) {
        current_time = CdiOsGetMicroseconds();
        uint64_t overtime = current_time - rate_next_start_time;
        CDI_LOG_THREAD(kLogError, "Connection[%s] Payload took [%lld]microseconds too long. Rate time "
                    "[%d] microseconds.", test_settings_ptr->connection_name_str, overtime,
                    test_settings_ptr->rate_period_microseconds);
        connection_info_ptr->payload_error = true;
    }

    return !got_error;
}

/**
 * Wait for Tx payloads that have been queued to transmit to complete (waits for pending Tx payload callbacks).
 *
 * @param   connection_info_ptr Pointer to the TestConnectionInfo data structure describing this connection.
 * @param   payload_count The current payload number.
 * @param   timeout_ms Maximum time to wait in milliseconds.
 */
static void WaitForTxPayloadsToComplete(TestConnectionInfo* connection_info_ptr, int payload_count, uint32_t timeout_ms)
{
    CdiSignalType signal_array[2];
    signal_array[0] = connection_info_ptr->payload_done_signal;
    signal_array[1] = connection_info_ptr->connection_shutdown_signal;

    TEST_LOG_CONNECTION(kLogWarning, "Waiting up to [%llu]ms for [%d]queued Tx payloads to complete...", timeout_ms,
                        payload_count - CdiOsAtomicRead32(&connection_info_ptr->payload_count));

    uint64_t start_ms = CdiOsGetMilliseconds();
    uint32_t time_to_wait_ms = timeout_ms;

    while (payload_count > CdiOsAtomicRead32(&connection_info_ptr->payload_count)) {
        uint32_t signal_index;
        CdiOsSignalsWait(signal_array, 2, false, time_to_wait_ms, &signal_index);
        CdiOsSignalClear(connection_info_ptr->payload_done_signal);
        if (0 != signal_index) {
            // Wait was aborted (signal_index=1) or timed-out (signal_index=OS_SIG_TIMEOUT).
            if (OS_SIG_TIMEOUT == signal_index) {
                TEST_LOG_CONNECTION(kLogWarning, "Wait timed-out after [%u]ms.", timeout_ms);
            }
            break;
        }

        // Get the total time that has expired since we entered this function and see if we have exceeded the timeout.
        uint64_t expired_ms = CdiOsGetMilliseconds() - start_ms;
        if (expired_ms >= (uint64_t)timeout_ms) {
            // Yes, got timeout.
            break;
        }
        // Have not exceeded timeout, so setup the remaining time to wait and go wait again.
        time_to_wait_ms = timeout_ms - expired_ms;
    }
}

/**
 * Send all payloads for this connection as requested by the user.
 *
 * @param   connection_info_ptr  Pointer to the TestConnectionInfo data structure describing this connection.
 *
 * @return  True if no errors; false if errors;
 */
static bool TestTxSendAllPayloads(TestConnectionInfo* connection_info_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    bool got_error = false;

    // Do some rate-tracking initialization so we know the correct time to send payloads later.
    int rate_period_microseconds = test_settings_ptr->rate_period_microseconds;
    CDI_LOG_THREAD(kLogInfo, "Connection[%s] using rate period[%d].",
                   test_settings_ptr->connection_name_str, rate_period_microseconds);

    // Set initial timestamp for PTP time.
    struct timespec start_time;
    CdiCoreGetUtcTime(&start_time);
    // Set start time for each stream.
    for (int i = 0; i < test_settings_ptr->number_of_streams; i++) {
        TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[i];
        stream_info_ptr->connection_start_time.seconds = (uint32_t)start_time.tv_sec;
        stream_info_ptr->connection_start_time.nanoseconds = start_time.tv_nsec;
    }

    // Loop through all payloads.
    int payload_count = 0;
    int payload_id = 0;
    int ptp_rate_count = 0;
    while (IsPayloadNumLessThanTotal(payload_count, connection_info_ptr->total_payloads) && !got_error) {
        // Check for the payload_error flag which may have gotten set by the Tx Callback if the payload timed out. If
        // --keep_alive was not used, then this is an error.
        if (connection_info_ptr->payload_error && !test_settings_ptr->keep_alive) {
            got_error = true;
        }

        // Loop through all streams, sending one payload per stream within this rate period.
        uint64_t rate_next_start_time = CdiOsGetMicroseconds() + rate_period_microseconds;
        int stream_index = 0;
        while ((stream_index < test_settings_ptr->number_of_streams) && !got_error) {
            bool got_queued = false;
            got_error = !TestTxTrySendStreamPayload(connection_info_ptr, stream_index, payload_id,
                                                    rate_next_start_time, ptp_rate_count, &got_queued);
            if (!got_error) {
                // Payload was successfully queued, so increment the payload counter. We will do this until we have
                // sent the requested total number of payloads. NOTE: Payloads for all stream indexes are always sent.
                payload_count++;
            }
            stream_index++; // Advance to next stream index.
        }
        if (!got_error) {
            payload_id++;
        }
        ptp_rate_count++; // Increment PTP rate counter.

        if (!got_error) {
            // Set the next start time, using PTP from stream index 0. NOTE: Using PTP time for rate so there is no
            // drift between when we send a payload and the PTP timestamp that is sent with the payload.
            StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[0];
            TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[0];
            CdiPtpTimestamp next_timestamp = GetPtpTimestamp(connection_info_ptr, stream_settings_ptr, stream_info_ptr,
                                                             ptp_rate_count);
            uint64_t next_ptp_start_time = CdiUtilityPtpTimestampToMicroseconds(&next_timestamp);
            uint64_t current_ptp_time = CdiCoreGetUtcTimeMicroseconds(); // Function used to get PTP time.
            if (current_ptp_time > next_ptp_start_time) {
                // We ran over our timing budget.
                uint64_t overtime = current_ptp_time - next_ptp_start_time;
                if (overtime >= (uint64_t)test_settings_ptr->tx_timeout) {
                    uint64_t max_overtime = (uint64_t)test_settings_ptr->tx_timeout *
                                            MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION;
                    if (overtime >= max_overtime) {
                        // Exceeded max amount of time. Wait for Tx queue to drain so we can recover and get back on
                        // cadence.
                        CDI_LOG_THREAD(kLogWarning, "Ran over max timing budget[%llu]us by [%llu]us.",
                                       max_overtime, overtime - max_overtime);
                        WaitForTxPayloadsToComplete(connection_info_ptr, payload_count,
                                                    (uint32_t)(max_overtime / 1000L)); // Convert us to ms.
                        current_ptp_time = CdiCoreGetUtcTimeMicroseconds(); // Function used to get PTP time.
                        overtime = current_ptp_time - next_ptp_start_time;
                    }
                }
                // Simulate dropping payloads by increasing the PTP rate counter.
                ptp_rate_count += (overtime / rate_period_microseconds) + 1;
                // Don't want to sleep more, since we are already behind on our rate cadence.
                next_ptp_start_time = current_ptp_time;
            }

            // To stay on our rate-time cadence, calculate amount of time to delay and then sleep.
#ifdef DEBUG_RX_BUFFER
            CDI_LOG_THREAD(kLogInfo, "Sleeping[%llu]", next_ptp_start_time - current_ptp_time);
#endif
            CdiOsSleepMicroseconds(next_ptp_start_time - current_ptp_time);
        }

#ifdef ENABLE_TEST_INTERNAL_CORE_STATS_RECONFIGURE
        if (!got_error) {
            // Test dynamic statistics reconfiguration, if enabled.
            got_error = !TestDynamicPollStatsReconfigure(connection_info_ptr->test_dynamic_handle);
        }
#endif
    }
    return !got_error;
}

/**
 * Pool operator function used to initialize transmit buffers when their contents are static patterns. All it does is
 * copies a linear source buffer into a destination as described by the SGL passed in through item_ptr.
 *
 * @param context_ptr pointer to the linear source buffer address and size.
 * @param item_ptr the address of the SGL which describes the destination buffer.
 *
 * @return bool true always.
 */
static bool InitStaticBufferContents(const void* context_ptr, void* item_ptr)
{
    InitFunctionArgs* args_ptr = (InitFunctionArgs*)context_ptr;
    const CdiSgList* sgl_ptr = (CdiSgList*)item_ptr;

    uint8_t* src_ptr = args_ptr->src_ptr;
    for (CdiSglEntry* entry_ptr = sgl_ptr->sgl_head_ptr ; entry_ptr != NULL ; entry_ptr = entry_ptr->next_ptr) {
        memcpy(entry_ptr->address_ptr, src_ptr, entry_ptr->size_in_bytes);
        src_ptr += entry_ptr->size_in_bytes;
    }

    return true;
}

/**
 * This function prepares for and sends all data for this transmitter connection, and then reports transfer statistics.
 *
 * @param   connection_info_ptr  Pointer to the TestConnectionInfo data structure describing this connection.
 *
 * @return  True if no errors; false if errors;
 */
static bool TestTxSendTestData(TestConnectionInfo* connection_info_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    bool got_error = false;

    // Prepare each stream for transmission.
    for (int i = 0; i < test_settings_ptr->number_of_streams && !got_error; i++) {
        StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[i];
        TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[i];
        uint8_t* tx_static_payload_pattern_ptr = NULL;

        // Load a buffer with the first payload's pattern.
        if (NULL == stream_settings_ptr->file_read_str) {
            tx_static_payload_pattern_ptr = CdiOsMemAllocZero(stream_info_ptr->payload_buffer_size);
            if (NULL == tx_static_payload_pattern_ptr) {
                CDI_LOG_THREAD(kLogError, "Failed to allocate memory for payload pattern buffer.");
                got_error = true;
            }
        }
        if (!got_error) {
            got_error = !PreparePayloadData(stream_settings_ptr, stream_info_ptr->payload_buffer_size,
                                            &stream_info_ptr->user_data_read_file_handle,
                                            tx_static_payload_pattern_ptr);
        }
        // Initialize the tx_payload_buffer pools with pattern data if not getting it from file.
        // Doing this here allows all of the pools to have the correct data, therefore obviating
        // the need for a memcpy on each payload transfer, which is a big performance hit.
        if (NULL == stream_settings_ptr->file_read_str && !got_error) {
            const InitFunctionArgs args = {
                .src_ptr = tx_static_payload_pattern_ptr,
                .size = stream_info_ptr->payload_buffer_size
            };
            if (!CdiPoolForEachItem(stream_info_ptr->tx_pool_handle, InitStaticBufferContents, &args)) {
                CDI_LOG_THREAD(kLogError, "Failed to initialize tx payload pattern buffer.");
            }
        }
        if (tx_static_payload_pattern_ptr) {
            CdiOsMemFree(tx_static_payload_pattern_ptr);
            tx_static_payload_pattern_ptr = NULL;
        }

        // Compute the AVM configuration structure and payload unit size if this is an AVM connection type.
        if (kTestProtocolAvm == test_settings_ptr->connection_protocol) {
            switch (stream_settings_ptr->avm_data_type) {
                case kCdiAvmNotBaseline:
                    // This should never happen but nothing can be done if it does.
                    break;
                case kCdiAvmVideo:
                    {
                        // Load video config data directly from the test settings provided by command line input.
                        CdiAvmBaselineConfig baseline_config = {
                            .payload_type = kCdiAvmVideo,
                            .video_config = stream_settings_ptr->video_params
                        };
                        CdiAvmMakeBaselineConfiguration(&baseline_config, &stream_settings_ptr->avm_config,
                                                        &stream_settings_ptr->unit_size);
                    }
                    break;
                case kCdiAvmAudio:
                    {
                        // Load audio config data directly from the test settings provided by command line input.
                        CdiAvmBaselineConfig baseline_config = {
                            .payload_type = kCdiAvmAudio,
                            .audio_config = stream_settings_ptr->audio_params
                        };
                        CdiAvmMakeBaselineConfiguration(&baseline_config, &stream_settings_ptr->avm_config,
                                                        &stream_settings_ptr->unit_size);
                    }
                    break;
                case kCdiAvmAncillary:
                    {
                        // Make generic config data structure for ancillary data; no specific configuration
                        // parameters are allowed for this type.
                        CdiAvmBaselineConfig baseline_config = {
                            .payload_type = kCdiAvmAncillary
                        };
                        CdiAvmMakeBaselineConfiguration(&baseline_config, &stream_settings_ptr->avm_config,
                                                        &stream_settings_ptr->unit_size);
                    }
                    break;
                // No default so compiler complains about missing cases.
            }
        }
    }

    if (!got_error) {
        // Ensure we are connected to remote target before starting the test.
        if (kCdiConnectionStatusConnected != connection_info_ptr->connection_status) {
            got_error = !TestWaitForConnection(connection_info_ptr,
                                               GetGlobalTestSettings()->connection_timeout_seconds);
        }
    }

    // Loop through sending one payload for each stream in this connection.
    if (!got_error) {
        got_error = !TestTxSendAllPayloads(connection_info_ptr);
    }

    if (!got_error) {
        // Done signal timeout. We will wait for double the length of the tx_timeout setting. tx_timeout is specified in
        // milliseconds, so we need to convert our microsecond tx_timeout to milliseconds.
        uint32_t timeout_ms = test_settings_ptr->tx_timeout * TX_ALL_DONE_TIMEOUT_FACTOR / 1000;
#ifdef DISABLE_RATE_TIMEOUT_FOR_DEBUG
        timeout_ms = CDI_INFINITE; // Force to infinite for debugging (so we can use breakpoints).
#endif

        // Wait for the done signal since the Tx callbacks will lag our last transmission of payloads above, but timeout
        // and error if it has been too long, or if a shutdown signal occurs while we are waiting.
        if (!TestWaitSignalOrAbort(connection_info_ptr->done_signal, connection_info_ptr->connection_shutdown_signal,
                                   timeout_ms)) {
            CDI_LOG_THREAD(kLogError, "Shutdown or timeout received while waiting for done signal to be set by Tx "
                                      "callback.");
            got_error = true;
        }
    }

    // Close the payload data file if opened.
    for (int i = 0; i < test_settings_ptr->number_of_streams; i++) {
        if (connection_info_ptr->stream_info[i].user_data_read_file_handle) {
            CdiOsClose(connection_info_ptr->stream_info[i].user_data_read_file_handle);
        }
    }

    // Set pass/fail status for the connection based on the got_error signal.
    if (got_error) {
        connection_info_ptr->pass_status = false;
    }

    return !got_error;
}

/**
 * Process core data from Tx callback that is common to both connection protocol types (RAW and AVM). If error, log
 * error message and mark the connection as failed. If no error, then check the transmitted payload against expected
 * test parameters.
 *
 * @param core_cb_data_ptr Pointer to core callback data.
 * @param stream_index Zero-based stream index.
 */
static void TestTxProcessCoreCallbackData(const CdiCoreCbData* core_cb_data_ptr, int stream_index)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    uint64_t current_time = CdiOsGetMicroseconds();
    TestTxUserData* user_data_ptr = core_cb_data_ptr->user_cb_param;
    TestConnectionInfo* connection_info_ptr = user_data_ptr->test_connection_info_ptr;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    uint64_t start_time = user_data_ptr->tx_payload_start_time;

    // Free payload resources.
    FreePayloadResources(connection_info_ptr, user_data_ptr);

    // Check if we think we are done or not, and if we are not done, then check the rest of the payload info.
    if (CdiOsSignalGet(connection_info_ptr->done_signal)) {
        TEST_LOG_CONNECTION(kLogError, "Tx Connection is marked done, but we have received an unexpected Tx callback.");
        connection_info_ptr->pass_status = false;
    }

    // Increment the payload processed count and check for done whether the payload was in error or not.
    TestIncPayloadCount(connection_info_ptr, stream_index);
    if (core_cb_data_ptr->status_code != kCdiStatusOk) {
        TEST_LOG_CONNECTION(kLogError, "TX Callback received error code[%d]. Msg[%s]", core_cb_data_ptr->status_code,
                            core_cb_data_ptr->err_msg_str);
        connection_info_ptr->pass_status = false;
        connection_info_ptr->payload_error = true;
    } else {
        // Validate that we received the payload within the expected time, as indicated by this Tx callback routine
        // getting called. The origination_timestamp value was set to the current time when the payload was transmitted,
        // so we would expect to have received this callback before our Tx timeout occurs.
        uint64_t expected_time = start_time + test_settings_ptr->tx_timeout;
        if (expected_time < current_time) {
            TEST_LOG_CONNECTION(kLogInfo, "Connection[%s] payload[%d] transmitted late by [%lld]microseconds",
                                connection_info_ptr->test_settings_ptr->connection_name_str,
                                connection_info_ptr->payload_count-1, current_time - expected_time);
            connection_info_ptr->payload_error = true;
        }
    }
}

/**
 * Handle the Tx RAW callback.
 *
 * @param   cb_data_ptr Pointer to Tx RAW callback data.
 */
static void TestRawTxCallback(const CdiRawTxCbData* cb_data_ptr)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    // Raw protocol only uses core data, so just Validate that.
    TestTxProcessCoreCallbackData(&cb_data_ptr->core_cb_data, 0);
}

/**
 * Handle the Tx AVM callback.
 *
 * @param   cb_data_ptr  Pointer to Tx AVM callback data.
 */
static void TestAvmTxCallback(const CdiAvmTxCbData* cb_data_ptr)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    // Perform validation of the AVM data.
    TestTxUserData *user_data_ptr = cb_data_ptr->core_cb_data.user_cb_param;
    TestConnectionInfo* connection_info_ptr = user_data_ptr->test_connection_info_ptr;
    int stream_index = user_data_ptr->stream_index;
    StreamSettings* stream_settings_ptr = &connection_info_ptr->test_settings_ptr->stream_settings[stream_index];

    // Perform validation of the stream ID.
    int expected_stream_identifier = stream_settings_ptr->stream_id;
    if (expected_stream_identifier != cb_data_ptr->avm_extra_data.stream_identifier) {
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d] Tx expected stream_identifier[%d] but got [%d].",
                            connection_info_ptr->test_settings_ptr->connection_name_str,
                            stream_settings_ptr->stream_id, expected_stream_identifier,
                            cb_data_ptr->avm_extra_data.stream_identifier);
        connection_info_ptr->pass_status = false;
    }

    // Validate core callback data.
    TestTxProcessCoreCallbackData(&cb_data_ptr->core_cb_data, stream_index);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

// This function creates a Tx connection.
THREAD TestTxCreateThread(void* arg_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)arg_ptr;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;

    // Assign the connection name from TestSettings to this connection's configuration data.
    char* connection_name_str = test_settings_ptr->connection_name_str;
    connection_info_ptr->config_data.tx.connection_name_str = connection_name_str;

    CdiLogMethodData log_method_data;
    char sdk_log_filename_str[MAX_LOG_FILENAME_LENGTH]; // Use this buffer so the string is valid for ...TxCreate().

    // Setup log files for the test application and SDK connections.
    bool got_error = !TestCreateConnectionLogFiles(connection_info_ptr, &log_method_data, sdk_log_filename_str);
    if (got_error) {
        CDI_LOG_THREAD(kLogFatal, "Failed to create log file for Tx connection[%s]", connection_name_str);
    }

    if (!got_error) {
        // Now that we have the log setup for this connection, we set this thread to use it. Can then use the
        // CDI_LOG_THREAD() macro to log to it from this thread.
        CdiLoggerThreadLogSet(connection_info_ptr->app_file_log_handle);

        // Set up transmitter parameters and create the Tx connection.
        connection_info_ptr->config_data.tx.dest_ip_addr_str = test_settings_ptr->remote_adapter_ip_str;
        connection_info_ptr->config_data.tx.dest_port = test_settings_ptr->dest_port;
        connection_info_ptr->config_data.tx.thread_core_num = test_settings_ptr->thread_core_num;
        connection_info_ptr->config_data.tx.connection_log_method_data_ptr = &log_method_data;

        // Configure connection callback.
        connection_info_ptr->config_data.tx.connection_cb_ptr = TestConnectionCallback;
        connection_info_ptr->config_data.tx.connection_user_cb_param = connection_info_ptr;

        // Configure statistics period and callback.
        connection_info_ptr->config_data.tx.stats_config.stats_period_seconds = test_settings_ptr->stats_period_seconds;
        connection_info_ptr->config_data.tx.stats_cb_ptr = TestStatisticsCallback;
        connection_info_ptr->config_data.tx.stats_user_cb_param = connection_info_ptr;

        // Create a Tx user data memory pool for this connection. Will allocate enough pool items to allow for 1 + the
        // maximum number of simultaneous connections (see POOL_PAYLOAD_ITEM_COUNT).
        got_error = !CdiPoolCreate(
                            "TestTxUserData Pool", // Name of the pool.
                            POOL_PAYLOAD_ITEM_COUNT, // Number of pool items.
                            0, // Grow count size (don't want to grow).
                            0, // Max grow count (don't want to grow).
                            sizeof(TestTxUserData), // Payload buffer size.
                            true, // true= Make thread-safe.
                            &connection_info_ptr->tx_user_data_pool_handle); // Returned handle to the pool.
    }

    if (!got_error) {
        CDI_LOG_THREAD(kLogInfo, "Setting up Tx connection. Protocol[%s] Destination IP[%s] Destination Port[%d] Name[%s]",
                       CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, test_settings_ptr->connection_protocol),
                       connection_info_ptr->config_data.tx.dest_ip_addr_str,
                       connection_info_ptr->config_data.tx.dest_port,
                       CdiGetEmptyStringIfNull(connection_info_ptr->config_data.tx.connection_name_str));

        // Based on the user-specified protocol type, we either plan to transmit RAW payloads or AVM payloads.
        if (kTestProtocolRaw == test_settings_ptr->connection_protocol) {
            got_error = (kCdiStatusOk != CdiRawTxCreate(&connection_info_ptr->config_data.tx, TestRawTxCallback,
                                                        &connection_info_ptr->connection_handle));
        } else {
            got_error = (kCdiStatusOk != CdiAvmTxCreate(&connection_info_ptr->config_data.tx, TestAvmTxCallback,
                                                        &connection_info_ptr->connection_handle));
        }

        // If connection name was not specified, copy the name generated by the CDI SDK.
        if ('\0' == connection_name_str[0]) {
            CdiOsStrCpy(connection_name_str, sizeof(test_settings_ptr->connection_name_str),
                        connection_info_ptr->config_data.tx.connection_name_str);
        }

        if (got_error) {
            CdiLogMultilineState m_state;
            CDI_LOG_THREAD_MULTILINE_BEGIN(kLogError, &m_state);
            CDI_LOG_MULTILINE(&m_state, "Failed to create Tx connection. Protocol[%s] Destination Port[%d] Name[%s]",
                CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, test_settings_ptr->connection_protocol),
                connection_info_ptr->config_data.tx.dest_port, connection_name_str);
            CDI_LOG_MULTILINE(&m_state, "Some other application (or another instance of this application) may be using "
                                        "the requested port.");
            CDI_LOG_MULTILINE_END(&m_state);
        }
    }

    // Send the user-specified number of payloads.
    if (!got_error) {
        got_error = !TestTxSendTestData(connection_info_ptr);
    }

    if (connection_info_ptr->connection_handle) {
        // When the connection is closed, the connection_handle will be set to NULL. We will protect access to it
        // with a lock so the main thread that updates stats won't crash.
        CdiOsCritSectionReserve(connection_info_ptr->connection_handle_lock);
        if (kCdiStatusOk != CdiCoreConnectionDestroy(connection_info_ptr->connection_handle)) {
            got_error = true;
        }
        connection_info_ptr->connection_handle = NULL;
        CdiOsCritSectionRelease(connection_info_ptr->connection_handle_lock);
    }

    if (connection_info_ptr->tx_user_data_pool_handle) {
        if (got_error) {
            CdiPoolPutAll(connection_info_ptr->tx_user_data_pool_handle);
        }
        CdiPoolDestroy(connection_info_ptr->tx_user_data_pool_handle);
        connection_info_ptr->tx_user_data_pool_handle = NULL;
    }

    // By closing the connection, the SDK sends the last set of stats using our user-registered callback function
    // TestStatisticsCallback(). So, now print TX final statistics.
    CdiPayloadCounterStats total_stats = { 0 };
    for (int i = 0; i < connection_info_ptr->number_stats; i++) {
        total_stats.num_payloads_transferred += connection_info_ptr->payload_counter_stats_array[i].num_payloads_transferred;
        total_stats.num_payloads_dropped += connection_info_ptr->payload_counter_stats_array[i].num_payloads_dropped;
        total_stats.num_payloads_late += connection_info_ptr->payload_counter_stats_array[i].num_payloads_late;
    }
    const CdiPayloadCounterStats* counter_stats_ptr = &total_stats;

    // Write these stats out to the log associated with this thread.  We use the CDI_LOG_THREAD_MULTILINE_BEGIN macro,
    // which automatically creates a multiline log buffer associated with this thread, and we use the returned handle to
    // keep writing lines to that buffer via CDI_LOG_MULTILINE until we are ready to print with CDI_LOG_MULTILINE_END.
    CdiLogMultilineState handle;
    CDI_LOG_THREAD_MULTILINE_BEGIN(kLogInfo, &handle);
    CDI_LOG_MULTILINE(&handle, "Connection[%s] TX Stats:", test_settings_ptr->connection_name_str);
    CDI_LOG_MULTILINE(&handle, "Number of payloads transferred[%llu]", counter_stats_ptr->num_payloads_transferred);
    CDI_LOG_MULTILINE(&handle, "Number of payloads dropped    [%llu]", counter_stats_ptr->num_payloads_dropped);

    // This value is the number of payloads that were queued to be transmitted, but took longer than expected to
    // actually complete the transfer.
    CDI_LOG_MULTILINE(&handle, "Number of payloads late       [%llu]", counter_stats_ptr->num_payloads_late);

    // This value is the number of payloads that were delayed from being queued to be sent because a previous payload
    // being transmitted did not complete the transfer in time. We currently don't have a way to cancel a pending
    // transfer, so we had to wait for it to complete before starting transfer of the next payload.
    CDI_LOG_MULTILINE(&handle, "Number of payloads delayed    [%llu]", connection_info_ptr->tx_late_payload_count);
    CDI_LOG_MULTILINE_END(&handle);

    // Destroy the connection's logger last, so it can be used in all the logic above.
    CdiLoggerDestroyLog(connection_info_ptr->app_file_log_handle);
    CdiLoggerDestroyLog(connection_info_ptr->sdk_file_callback_log_handle);

    // Update the pass_status flag for the connection if any of the above logic has failed.
    if (got_error) {
        connection_info_ptr->pass_status = false;
    }

    // Make sure to set this signal so the test can exit.
    CdiOsSignalSet(connection_info_ptr->done_signal);

    CdiLoggerThreadLogUnset();
    return 0; // This is not used.
}
