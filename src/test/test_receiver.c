// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the receive-side CDI test application logic.
 *
 * See @ref cdi_test_rx for diagrams and detailed description of the receive-side program flow.
 *
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "test_receiver.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>

#include "cdi_avm_api.h"
#include "cdi_avm_payloads_api.h"
#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_test.h"
#include "fifo_api.h"
#include "riff.h"
#include "test_control.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure for storing data to be sent to the FIFO used in TestRxVerify().
 */
typedef struct {
    int         stream_index; ///< Zero-based stream index related to this payload.
    CdiSgList   sgl; ///< Scatter-Gather-List of payload.
} TestRxPayloadState;

/// Control structure for Anc unpacketize callback.
struct UnpacketizeAncControl
{
    int num_parity_errors; ///< Count the number of parity errors.
    int num_checksum_errors; ///< Count the number of checksum errors.
};

/**
 * Enum indicating severity of check failures. For fatal failures the test should be aborted.
 */
typedef enum {
    kTestStatusOk, ///< No check failed.
    kTestStatusNonFatalFailure, ///< A check failed.
    kTestStatusFatalFailure, ///< A check failed and it does not make sense to continue testing.
} TestCheckStatus;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Callback used by UnpacketizeAncPayload. Writes user data into results buffer.
 *
 * @see CdiAvmUnpacketizeAncCallback.
 */
static void UnpacketizeAncCallback(void* user_data_ptr, CdiFieldKind field_kind,
    const CdiAvmAncillaryDataPacket* packet_ptr, bool has_parity_error, bool has_checksum_error)
{
    struct UnpacketizeAncControl* ctrl_ptr = (struct UnpacketizeAncControl*)user_data_ptr;
    if (NULL != packet_ptr) {
        ctrl_ptr->num_parity_errors += has_parity_error;
        ctrl_ptr->num_checksum_errors += has_checksum_error;
    }
    (void)field_kind; // unused
}

/**
 * Check that the data can be unpacketized successfully.
 *
 * @param connection_info_ptr Pointer to connection info data. For TEST_LOG_CONNECTION macro.
 * @param sgl_ptr Pointer to user data buffer.
 *
 * @return Success or failure.
 */
static bool UnpacketizeAncPayload(TestConnectionInfo* connection_info_ptr, CdiSgList* sgl_ptr)
{
    struct UnpacketizeAncControl ctrl = {
        .num_parity_errors = 0,
        .num_checksum_errors = 0
    };
    CdiReturnStatus rs = CdiAvmUnpacketizeAncillaryData(sgl_ptr, UnpacketizeAncCallback, &ctrl);
    if (ctrl.num_parity_errors) {
        TEST_LOG_CONNECTION(kLogError, "[%d] parity errors occurred.", ctrl.num_parity_errors);
    }
    if (ctrl.num_checksum_errors) {
        TEST_LOG_CONNECTION(kLogError, "[%d] checksum errors occurred.", ctrl.num_checksum_errors);
    }
    if (kCdiStatusOk != rs) {
        TEST_LOG_CONNECTION(kLogError, "Unpacketizing ANC payload failed with [%s].", CdiCoreStatusToString(rs));
    }
    return kCdiStatusOk == rs;
}


/**
 * Check that the data can be unpacketized successfully.
 *
 * @param sgl_ptr Pointer to user data buffer.
 * @param connection_info_ptr Pointer to connection info data. For TEST_LOG_CONNECTION macro.
 * @param stream_index Index of stream.
 *
 * @return Return code indicating check failures.
 */
static TestCheckStatus WritePayloadToFile(CdiSgList* sgl_ptr, TestConnectionInfo* connection_info_ptr, int stream_index)
{
    StreamSettings* stream_settings_ptr = &connection_info_ptr->test_settings_ptr->stream_settings[stream_index];
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];
    assert(NULL != stream_info_ptr->user_data_write_file_handle);

    TestCheckStatus return_val = kTestStatusOk;

    // Write Subheader if using a RIFF file.
    if (stream_settings_ptr->riff_file) {
        // For now just use FOURCC "ANC " but may do different subheaders later.
        RiffChunkHeader subheader = {
            .four_cc = "ANC ",
            .size = stream_info_ptr->next_payload_size
        };

        if (!CdiOsWrite(stream_info_ptr->user_data_write_file_handle, &subheader, sizeof(RiffChunkHeader))) {
            TEST_LOG_CONNECTION(kLogError, "Failed to write RIFF subheader for payload.");
            return_val = kTestStatusNonFatalFailure;
        }
    }

    // Loop through all SGL entries write received data to the file.
    CdiSglEntry* this_entry_ptr = sgl_ptr->sgl_head_ptr;
    while (NULL != this_entry_ptr && kTestStatusOk == return_val) {
        // If the user has specified a file to write received data to, then write the data from this SGL entry
        // to that file.
        if (!CdiOsWrite(stream_info_ptr->user_data_write_file_handle,
                        this_entry_ptr->address_ptr, this_entry_ptr->size_in_bytes)) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d] Failed to write data to output file [%s].",
                                test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id,
                                stream_settings_ptr->file_write_str);
            return_val = kTestStatusNonFatalFailure;
        }
        this_entry_ptr = this_entry_ptr->next_ptr;
    }
    return return_val;
}

/**
 * @brief Check a received data buffer (in scatter gather list form) against expected received data based on provided
 * test parameters.
 *
 * @param   sgl_ptr          Pointer to scatter-gather list of received payload data.
 * @param   connection_info_ptr  Pointer to data structure representing the connection parameters and associated test
 *                               parameters.
 * @param   stream_index     Index of stream.
 *
 * @return                   Return code indicating check failures.
 */
static TestCheckStatus TestRxBufferCheck(CdiSgList* sgl_ptr, TestConnectionInfo* connection_info_ptr, int stream_index)
{
    StreamSettings* stream_settings_ptr = &connection_info_ptr->test_settings_ptr->stream_settings[stream_index];
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];

    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.
    TestCheckStatus return_val = kTestStatusOk;

    // Based on the user-defined read_file or test data pattern, we will check each byte of the receive buffer against
    // expected values. If payload data is supposed to be checked, the rx_expected_data_buffer_ptr will have been allocated
    // and initialized with the first payload from the file_read file or with a pattern specified by the --pattern
    // option.
    uint8_t* pattern_ptr = stream_info_ptr->rx_expected_data_buffer_ptr;

    // We loop through the received SGL either way, but we only check the received data if the user has requested we
    // do so via either the --file_read or --pattern options.
    bool check_data = NULL != pattern_ptr;

    // We loop through the SGL and write to a file if a file exists as long as the write operation is not failing.
    // If a data error occurs the file output is continues to be written.
    if (NULL != stream_info_ptr->user_data_write_file_handle) {
        return_val = WritePayloadToFile(sgl_ptr, connection_info_ptr, stream_index);
        if (stream_settings_ptr->riff_file) {
            // We use RIFF only with ancillary data payloads. Try to parse the received payload as part of this check.
            if (kTestStatusFatalFailure != return_val && !UnpacketizeAncPayload(connection_info_ptr, sgl_ptr)) {
                return_val = kTestStatusNonFatalFailure;
            }
        }
    }

    // Check that the received payload is not larger than expected.
    if (sgl_ptr->total_data_size > stream_info_ptr->payload_buffer_size) {
        TEST_LOG_CONNECTION(kLogError, "Payload for stream ID [%d] too large (got [%d] expected no more than [%d]).",
            stream_settings_ptr->stream_id, sgl_ptr->total_data_size, stream_info_ptr->payload_buffer_size);
        return_val = kTestStatusFatalFailure;
    }

    // Loop through all SGL entries and check all received data until we reach the end of the list.
    CdiSglEntry* this_entry_ptr = sgl_ptr->sgl_head_ptr;
    int bytes_in_sgl_payload = 0;
    int num_sgl_entries = 0;
    while (NULL != this_entry_ptr && kTestStatusFatalFailure != return_val) {
        // Keep a running count of the number of bytes we have found in each SGL entry.  We check later to make sure
        // this matches the total number expected.
        bytes_in_sgl_payload += this_entry_ptr->size_in_bytes;
        num_sgl_entries++;

        // If we are supposed to be checking data and we haven't gotten any errors, then check this SGL entry's data
        // for correctness, comparing the SGL data to the precomputed data for this payload in the buffer pointed to
        // by connection_info_ptr->stream_info[stream_index].expected_data_buffer_ptr.
        if (check_data) {
            if (memcmp(pattern_ptr, this_entry_ptr->address_ptr, this_entry_ptr->size_in_bytes)) {
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d] Data does not match for payload[%d].",
                                    test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id,
                                    stream_info_ptr->payload_count - 1);
                TEST_LOG_CONNECTION(kLogError, "got[0x%016"PRIx64"] expected[0x%016"PRIx64"]",
                                    *(uint64_t*)this_entry_ptr->address_ptr, *(uint64_t*)pattern_ptr);

                // Once data check fails, mark the check as failed, and stop checking for the rest of the payload.
                check_data = false;

                // Get the difference in payloads, accounting for rollover.
                uint64_t received = (*(uint64_t*)this_entry_ptr->address_ptr)&MAX_TEST_PAYLOAD_COUNT;
                uint64_t expected = (*(uint64_t*)pattern_ptr)&MAX_TEST_PAYLOAD_COUNT;
                uint64_t difference = 0;
                if (received > expected) {
                    difference = received - expected;
                } else {
                    difference = MAX_TEST_PAYLOAD_COUNT - expected + received;
                }

                // If the payload difference is less than a predetermined limit, then attempt to normalize the payload
                // count to the next expected pattern.
                if (difference <= PAYLOAD_DIFFERENCE_LIMIT) {
                    TEST_LOG_CONNECTION(kLogInfo, "Unexpected payload counter value. Assuming payload drop and adjusting"
                        " expected payload counter for stream ID[%d] in receiver.", stream_settings_ptr->stream_id);
                    TestIncPayloadCount(connection_info_ptr, stream_index);
                    return_val = kTestStatusNonFatalFailure;
                } else {
                    return_val = kTestStatusFatalFailure;
                }
            }
        }

        // Update pointers for next SGL entry location.
        pattern_ptr += this_entry_ptr->size_in_bytes;
        this_entry_ptr = this_entry_ptr->next_ptr;
    }

    // The CdiSgList structure contains a total_data_size field, which is in bytes, and should exactly match the number
    // of bytes we have found in each list entry we checked in the above loop.  If they don't match, log an error.
    if (kTestStatusFatalFailure != return_val && sgl_ptr->total_data_size != bytes_in_sgl_payload) {
        TEST_LOG_CONNECTION(kLogError,
            "Connection[%s] Stream ID[%d] Payload size[%d] in SGL does not match size from SGL entries [%d].",
            test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id, sgl_ptr->total_data_size,
            bytes_in_sgl_payload);
        return_val = kTestStatusNonFatalFailure;
    }

    // With linear buffer mode we expect exactly one SGL entry.
    if (kCdiLinearBuffer == connection_info_ptr->test_settings_ptr->buffer_type && 1 != num_sgl_entries) {
        TEST_LOG_CONNECTION(kLogError,
            "Connection[%s] Stream ID[%d] SGL has [%d] entries but one is expected for LINEAR buffer type.",
            test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id, num_sgl_entries);
        return_val = kTestStatusFatalFailure;
    }

    // Update receive payload pattern check buffer in preparation for the next payload. Note that this increments the
    // first word of the payload buffer.  The same behavior happens on the transmit side, where the payload's first
    // word is incremented before even the first payload is sent.
    if (kTestStatusFatalFailure != return_val && NULL != stream_info_ptr->rx_expected_data_buffer_ptr) {
        if (stream_settings_ptr->riff_file && stream_info_ptr->user_data_read_file_handle) {
            if (!GetNextRiffChunkSize(stream_settings_ptr,
                    stream_info_ptr->user_data_read_file_handle, &stream_info_ptr->next_payload_size)) {
                return_val = kTestStatusFatalFailure;
            }
        }
        if (kTestStatusFatalFailure != return_val) {
            if (!GetNextPayloadDataLinear(connection_info_ptr, stream_settings_ptr, stream_info_ptr)) {
                return_val = kTestStatusFatalFailure;
            }
        }
    }
    return return_val;
}

/**
 * Wait for receive data from receive data FIFO. If error, print error message and mark the connection as fail. If no
 * error, then check the received payload against expected test parameters.
 *
 * @param   connection_info_ptr  Pointer to the connection info for this thread's associated connection.
 *
 * @return  True if success; false if failure.
 */
static bool TestRxVerify(TestConnectionInfo* connection_info_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    int payload_count = 0;

    // If we have already run into a problem before receiving any payloads, then just bail.
    if (connection_info_ptr->pass_status) {
        // Just stay here until we get shutdown, connection timeout or all expected payloads are received and checked.
        // Depending on timing, we can sometimes get the done_signal from callback routines before we have read all
        // SGL entries from the FIFO and checked them, so make sure our local payload count is complete.
        while (!CdiOsSignalGet(connection_info_ptr->done_signal) ||
               payload_count < connection_info_ptr->total_payloads) {

            // Ensure we are connected to remote target.
            if (kCdiConnectionStatusConnected != connection_info_ptr->connection_status) {
                if (!TestWaitForConnection(connection_info_ptr, GetGlobalTestSettings()->connection_timeout_seconds)) {
                    connection_info_ptr->pass_status = false;
                    break;
                }
            }

            // We want to count a payload error only once.
            bool payload_error = false;

            // We sit here and wait for an SGL entry in the FIFO.  The FIFO is written by the receive callback routine
            // data checker function TestRxProcessCoreCallbackData() before returning.  When we can read from this
            // FIFO, we get the SGL pointer and check its data for correctness.  It is very important that, once we
            // are done with the SGL, we free its memory, since the SDK assumes that we will do this.
            TestRxPayloadState payload_state;
            if (CdiFifoRead(connection_info_ptr->fifo_handle, CDI_INFINITE,
                            connection_info_ptr->connection_shutdown_signal, (void *)&payload_state)) {

                // Now check the received SGL data buffer for correctness based on expected pattern and payload size
                // derived from command line arguments.  If we find an error, then mark the payload in error.
                TestCheckStatus rc = TestRxBufferCheck(&payload_state.sgl, connection_info_ptr, payload_state.stream_index);
                if (kTestStatusOk != rc) {
                    payload_error = true;
                }
                if (kTestStatusFatalFailure == rc) {
                    connection_info_ptr->pass_status = false;
                }

                // IMPORTANT: Now that we are done with the received SGL, free its memory.
                if (payload_state.sgl.sgl_head_ptr) {
                    CdiReturnStatus rs = CdiCoreRxFreeBuffer(&payload_state.sgl);
                    if (kCdiStatusOk != rs) {
                        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Unable to free SGL buffer [%s].",
                                            test_settings_ptr->connection_name_str,
                                            CdiCoreStatusToString(rs));
                        payload_error = true;
                    }
                }
                payload_count++;
                connection_info_ptr->num_payload_errors += payload_error ? 1 : 0;
            } else {
                // Got a connection shutdown signal. Clear it.
                CdiOsSignalClear(connection_info_ptr->connection_shutdown_signal);
            }
        }
    }

    return connection_info_ptr->pass_status;
}

/**
 * This function is called when a payload is received in the callback function. This function does some validation
 * of the payload by checking things like payload number and SGL payload size. Then the SGL is placed in a FIFO for
 * additional verification by TestRxVerify(). This function is used in both the RAW and AVM receive payload flow.
 *
 * @param core_cb_data_ptr  Pointer to a CdiCoreCbData callback structure.
 * @param stream_index      The stream index.
 */
static void TestRxProcessCoreCallbackData(const CdiCoreCbData* core_cb_data_ptr, int stream_index)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    // Get the connection_info data structure pointer from core_cb_data.  It was provided in the rx config data when
    // the connection was created, and is returned in rx callback data.
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)core_cb_data_ptr->user_cb_param;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];

    // Check callback status and report errors.
    // Note "err_msg_str" is a pointer to the error message string, which is only valid until this function returns.
    // To avoid doing a memcpy of it, we will simply evaluate the error status here.
    if (kCdiStatusOk != core_cb_data_ptr->status_code) {
        const char* err_msg_str = core_cb_data_ptr->err_msg_str ? core_cb_data_ptr->err_msg_str :
                                  CdiCoreStatusToString(core_cb_data_ptr->status_code);
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Rx Callback received error code[%d]. Msg[%s].",
                            test_settings_ptr->connection_name_str, core_cb_data_ptr->status_code, err_msg_str);

        // We received a bad payload but never the less it's still a payload so lets increment the count and shutdown
        // if the last payload is received.
        connection_info_ptr->num_payload_errors++;
        TestIncPayloadCount(connection_info_ptr, stream_index);
        if (CdiOsSignalGet(connection_info_ptr->done_signal)) {
            CdiOsSignalSet(connection_info_ptr->connection_shutdown_signal);
        }
        return;
    }

    // Connection start time needs to be known to predict the next timestamp in the series. Connection start time
    // should never be zero after it is set since seconds is measuring seconds since 1970. Using the first timestamp
    // received as base instead of the local system time to make the received timestamp fully predictable.
    if (0 == stream_info_ptr->connection_start_time.seconds) {
        stream_info_ptr->connection_start_time = core_cb_data_ptr->core_extra_data.origination_ptp_timestamp;
    }

    // We want to count a payload error only once.
    bool payload_error = false;

    // Check if we think we are done or not, and if we are not done, then check the rest of the payload info.
    // We should not be here if we are already done, since receive connections are marked done at the end of processing
    // the last expected payload.  If we are here, then we mark an error for unexpected payload.
    TestPatternType pattern_type = stream_settings_ptr->pattern_type;
    if (CdiOsSignalGet(connection_info_ptr->done_signal)) {
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Rx Connection is marked done, but we have received "
                                       "an unexpected payload.", test_settings_ptr->connection_name_str);
        connection_info_ptr->pass_status = false;
    } else if (kTestPatternIgnore != pattern_type && !GetGlobalTestSettings()->no_payload_user_data) {

        // The transmit logic encodes the Tx payload counter and the respective connection into the payload_user_data
        // field of CdiCoreCbData.  We use our knowledge of how the Tx logic encodes those fields to decode them here
        // into local variables.
        int rx_connection = (int)(core_cb_data_ptr->core_extra_data.payload_user_data & 0xFF);
        int rx_payload_counter_8bit = (int)(core_cb_data_ptr->core_extra_data.payload_user_data >> 8) & 0xFF;
        int rx_stream_id = (int16_t)((core_cb_data_ptr->core_extra_data.payload_user_data >> 16) & 0xFFFF);
        int rx_ptp_rate_num = (int)(core_cb_data_ptr->core_extra_data.payload_user_data >> 32);
        (void)rx_connection;

        // Verify the data from the core_extra_data field, which contains user-supplied PTP timestamp information.
        CdiPtpTimestamp current_ptp_timestamp = core_cb_data_ptr->core_extra_data.origination_ptp_timestamp;

        // Verify PTP timestamp.
        CdiPtpTimestamp expected_timestamp = GetPtpTimestamp(connection_info_ptr, stream_settings_ptr, stream_info_ptr,
                                                             rx_ptp_rate_num);
        if ((current_ptp_timestamp.seconds != expected_timestamp.seconds) || (current_ptp_timestamp.nanoseconds !=
                                                                              expected_timestamp.nanoseconds)) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d], payload[%d]: PTP timestamp [seconds:nanoseconds "
                                "%u:%u] deviates from expected RTP timestamp [seconds:nanoseconds %u:%u].",
                                test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id,
                                connection_info_ptr->payload_count, current_ptp_timestamp.seconds,
                                current_ptp_timestamp.nanoseconds, expected_timestamp.seconds,
                                expected_timestamp.nanoseconds);
            payload_error = true;
        }

        // Check that the received payload counter matches the lower 8-bits of the local payload counter.
        if (rx_payload_counter_8bit != (stream_info_ptr->payload_count & 0xFF)) {
            TEST_LOG_CONNECTION(kLogError,
                "Connection[%s] Stream ID[%d] payload count[%d] does not match expected stream ID[%d] count[%d].",
                test_settings_ptr->connection_name_str, rx_stream_id, rx_payload_counter_8bit,
                stream_settings_ptr->stream_id, stream_info_ptr->payload_count & 0xFF);
            payload_error = true;
        }

        // Check that the received stream ID matches the expected stream ID.
        if (rx_stream_id != stream_settings_ptr->stream_id) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] stream ID[%d] does not match received stream ID[%d].",
                                test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id, rx_stream_id);
            payload_error = true;
        }
    }

    connection_info_ptr->num_payload_errors += payload_error ? 1 : 0;
}

/**
 * Perform any cleanup operation for the Rx callback. Currently, this function increments the payload count and writes
 * the SGL to its thread-specific FIFO.
 *
 * @param core_cb_data_ptr  Pointer to a CdiCoreCbData callback structure.
 * @param sgl_ptr           Pointer to the received SGL.
 * @param stream_index      The stream identifier.
 */
static void RxCoreCallbackCleanup(const CdiCoreCbData* core_cb_data_ptr, const CdiSgList* sgl_ptr, int stream_index)
{
    TestConnectionInfo* connection_info_ptr = core_cb_data_ptr->user_cb_param;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];

    // The payload count has been verified above and it's time to record that we've received this payload, so
    // call the routine that is responsible for incrementing the payload count and marking the connection done when
    // we have received all expected payloads for this connection.
    TestIncPayloadCount(connection_info_ptr, stream_index);

    // Now send the SGL to the thread-specific FIFO where it will sit waiting for additional data checking by
    // TestRxVerify().
    TestRxPayloadState payload_state = {
        .stream_index = stream_index,
        .sgl = *sgl_ptr
    };

    int stream_id = stream_settings_ptr->stream_id;
    if (!CdiFifoWrite(connection_info_ptr->fifo_handle, 1, connection_info_ptr->connection_shutdown_signal,
                      &payload_state)) {
        if (!CdiOsSignalGet(connection_info_ptr->connection_shutdown_signal)) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d] Unable to put Rx Callback message in FIFO[%s].",
                                test_settings_ptr->connection_name_str, stream_id,
                                CdiFifoGetName(connection_info_ptr->fifo_handle));
            connection_info_ptr->pass_status = false;
        }
    }

    // possible to check SGL data size against the payload size. If the transmitter is sending RIFF file payloads the
    // receiver must also use the -riff option to avoid payload size checking issues.
    if (kCdiStatusOk == core_cb_data_ptr->status_code &&
        !(test_settings_ptr->stream_settings[stream_index].riff_file &&
          (stream_info_ptr->user_data_read_file_handle == NULL))) {
        // Check if payload size matches the expected size from test settings.
        if (sgl_ptr->total_data_size != stream_info_ptr->next_payload_size) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d] Payload size[%d] does not match expected size[%d].",
                                test_settings_ptr->connection_name_str, stream_id, sgl_ptr->total_data_size,
                                stream_info_ptr->next_payload_size);
            connection_info_ptr->pass_status = false;
        }
    }
}

/**
 * Handle the Rx callback for RAW data payloads. This immediately calls TestRxProcessCoreCallbackData().
 *
 * @param   cb_data_ptr  Pointer to rx callback data for raw payloads.
 */
static void TestRawRxCallback(const CdiRawRxCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->core_cb_data.user_cb_param;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    int stream_index = connection_info_ptr->current_stream_count;

    // Now increment the current stream index for use by the next callback. Roll back to 0 when we hit the max.
    connection_info_ptr->current_stream_count =
        ((connection_info_ptr->current_stream_count) + 1) % test_settings_ptr->number_of_streams;

    // If we are validating a RAW connection, then everything we need to validate is done by this function.
    TestRxProcessCoreCallbackData(&cb_data_ptr->core_cb_data, stream_index);

    // Perform any cleanup operation on this data including writing the data to the destination FIFO and incrementing
    // the payload count.
    RxCoreCallbackCleanup(&cb_data_ptr->core_cb_data, &cb_data_ptr->sgl, stream_index);
}

/**
 * This function validates an audio AVM payload. The pass_status of the TestConnectionInfo will be set to 'false'
 * if there is an error in the payload.
 *
 * @param connection_info_ptr Pointer to the test connection info structure.
 * @param audio_config_ptr Pointer to the audio configuration structure received through the SDK.
 * @param stream_settings_ptr Pointer to the selected stream's StreamSettings structure.
 */
static void VerifyAvmAudioConfiguration(TestConnectionInfo* connection_info_ptr,
                                        const CdiAvmAudioConfig* audio_config_ptr,
                                        const StreamSettings* stream_settings_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    int stream_id = stream_settings_ptr->stream_id;

    if (NULL == audio_config_ptr) {
        // If there is no audio config data, then error.
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio config data, but none detected.",
                            test_settings_ptr->connection_name_str, stream_id);
        connection_info_ptr->pass_status = false;
    } else {
        // Check the audio config data. We test against what was stored in test settings based on user-supplied command
        // line options.
        if (0 != memcmp(&audio_config_ptr->version, &stream_settings_ptr->audio_params.version,
                        sizeof(audio_config_ptr->version))) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio v[%02d.%02d] but got v[%02d.%02d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->audio_params.version.major,
                                stream_settings_ptr->audio_params.version.minor,
                                audio_config_ptr->version.major,
                                audio_config_ptr->version.minor);
            connection_info_ptr->pass_status = false;
        }
        if (audio_config_ptr->grouping != stream_settings_ptr->audio_params.grouping) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio grouping [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->audio_params.grouping,
                                audio_config_ptr->grouping);
            connection_info_ptr->pass_status = false;
        }
        if (audio_config_ptr->sample_rate_khz != stream_settings_ptr->audio_params.sample_rate_khz) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio sample rate [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->audio_params.sample_rate_khz,
                                audio_config_ptr->sample_rate_khz);
            connection_info_ptr->pass_status = false;
        }
        if (0 != memcmp(audio_config_ptr->language, stream_settings_ptr->audio_params.language,
                        sizeof(audio_config_ptr->language))) {
            char language1_str[4] = { 0 };
            strncpy(language1_str, audio_config_ptr->language, 3);
            char language2_str[4] = { 0 };
            strncpy(language2_str, stream_settings_ptr->audio_params.language, 3);
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio language [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                language1_str,
                                language2_str);
            connection_info_ptr->pass_status = false;
        }
    }
}

/**
 * This function validates an audio AVM ancillary data payload. The pass_status of the TestConnectionInfo will be set to
 * 'false' if there is an error in the payload.
 *
 * @param connection_info_ptr Pointer to the test connection info structure.
 * @param anc_config_ptr Pointer to the ancillary data configuration structure received through the SDK.
 * @param stream_settings_ptr Pointer to the selected stream's StreamSettings structure.
 */
static void VerifyAvmAncillaryDataConfiguration(TestConnectionInfo* connection_info_ptr,
                                                const CdiAvmAncillaryDataConfig* anc_config_ptr,
                                                const StreamSettings* stream_settings_ptr)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    int stream_id = stream_settings_ptr->stream_id;

    if (NULL == anc_config_ptr) {
        // If there is no ancillary config data, then error.
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected ancillary config data, but none detected.",
                            test_settings_ptr->connection_name_str, stream_id);
        connection_info_ptr->pass_status = false;
    } else {
        // Check the ancillary config data. We test against what was stored in test settings based on user-supplied
        // command line options.
        if (0 != memcmp(&anc_config_ptr->version, &stream_settings_ptr->ancillary_data_params.version,
                        sizeof(anc_config_ptr->version))) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected ancillary data v[%02d.%02d] but got v[%02d.%02d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->ancillary_data_params.version.major,
                                stream_settings_ptr->ancillary_data_params.version.minor,
                                anc_config_ptr->version.major,
                                anc_config_ptr->version.minor);
            connection_info_ptr->pass_status = false;
        }
    }
}

/**
 * This function validates a video AVM payload. The pass_status of the TestConnectionInfo will be set to 'false'
 * if there is an error in the payload.
 *
 * @param connection_info_ptr Pointer to the test connection info structure.
 * @param video_config_ptr Pointer to the video configuration structure received through the SDK.
 * @param stream_settings_ptr Pointer to the selected stream's StreamSettings structure.
 * @param stream_index Index of the stream_settings array for this particular stream.
 */
static void VerifyAvmVideoConfiguration(TestConnectionInfo* connection_info_ptr,
                                        const CdiAvmVideoConfig* video_config_ptr,
                                        const StreamSettings* stream_settings_ptr, int stream_index)
{
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    int stream_id = stream_settings_ptr->stream_id;

    if (video_config_ptr == NULL) {
        // If there is no video config data, then error.
        TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video config data, but none detected.",
                            test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id);
        connection_info_ptr->pass_status = false;
    } else {
        // Check the video config data. We test against what was stored in test settings based on user-supplied command
        // line options.
        if (0 != memcmp(&video_config_ptr->version, &stream_settings_ptr->video_params.version,
                        sizeof(video_config_ptr->version))) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected v[%02d.%02d] but got v[%02d.%02d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->video_params.version.major,
                                stream_settings_ptr->video_params.version.minor,
                                video_config_ptr->version.major,
                                video_config_ptr->version.minor);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->width != stream_settings_ptr->video_params.width) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video width [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->video_params.width,
                                video_config_ptr->width);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->height != stream_settings_ptr->video_params.height) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video height [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_index,
                                stream_settings_ptr->video_params.height,
                                video_config_ptr->height);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->sampling != stream_settings_ptr->video_params.sampling) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video sampling [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType,
                                    stream_settings_ptr->video_params.sampling, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType, video_config_ptr->sampling,
                                    &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->alpha_channel != stream_settings_ptr->video_params.alpha_channel) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected alpha channel [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiAvmKeyEnumToString(kKeyAvmVideoAlphaChannelType,
                                    stream_settings_ptr->video_params.alpha_channel, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoAlphaChannelType,
                                    video_config_ptr->alpha_channel, &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->depth != stream_settings_ptr->video_params.depth) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video depth [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_index,
                                CdiAvmKeyEnumToString(kKeyAvmVideoBitDepthType,
                                    stream_settings_ptr->video_params.depth, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoBitDepthType, video_config_ptr->depth,
                                    &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->frame_rate_num != stream_settings_ptr->video_params.frame_rate_num) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video frame rate numerator [%d] "
                                "but got [%d].",
                                test_settings_ptr->connection_name_str, stream_id,
                                stream_settings_ptr->video_params.frame_rate_num,
                                video_config_ptr->frame_rate_num);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->frame_rate_den != stream_settings_ptr->video_params.frame_rate_den) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video frame rate denominator [%d] "
                                "but got [%d].",
                                test_settings_ptr->connection_name_str, stream_id,
                                stream_settings_ptr->video_params.frame_rate_den,
                                video_config_ptr->frame_rate_den);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->colorimetry != stream_settings_ptr->video_params.colorimetry) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video colorimetry [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiAvmKeyEnumToString(kKeyAvmVideoColorimetryType,
                                    stream_settings_ptr->video_params.colorimetry, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoColorimetryType, video_config_ptr->colorimetry,
                                    &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->interlace != stream_settings_ptr->video_params.interlace) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID%d]: Rx expected video interlace [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiUtilityBoolToString( stream_settings_ptr->video_params.interlace),
                                CdiUtilityBoolToString(video_config_ptr->interlace));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->segmented != stream_settings_ptr->video_params.segmented) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video segmented [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiUtilityBoolToString( stream_settings_ptr->video_params.segmented),
                                CdiUtilityBoolToString(video_config_ptr->segmented));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->tcs != stream_settings_ptr->video_params.tcs) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video TCS [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiAvmKeyEnumToString(kKeyAvmVideoTcsType,
                                    stream_settings_ptr->video_params.tcs, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoTcsType, video_config_ptr->tcs,
                                    &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->range != stream_settings_ptr->video_params.range) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video range [%s] but got [%s].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                CdiAvmKeyEnumToString(kKeyAvmVideoRangeType,
                                    stream_settings_ptr->video_params.range, &video_config_ptr->version),
                                CdiAvmKeyEnumToString(kKeyAvmVideoRangeType, video_config_ptr->range,
                                    &video_config_ptr->version));
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->par_width != stream_settings_ptr->video_params.par_width) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video PAR width [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->video_params.par_width,
                                video_config_ptr->par_width);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->par_height != stream_settings_ptr->video_params.par_height) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video PAR height [%d] but got [%d].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                stream_settings_ptr->video_params.par_height,
                                video_config_ptr->par_height);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->start_vertical_pos != stream_settings_ptr->video_params.start_vertical_pos) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video start vertical position [%d] "
                                "but got [%d].",
                                test_settings_ptr->connection_name_str, stream_id,
                                stream_settings_ptr->video_params.start_vertical_pos,
                                video_config_ptr->start_vertical_pos);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->vertical_size != stream_settings_ptr->video_params.vertical_size) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video vertical size [%d] but got "
                                "[%d].",
                                test_settings_ptr->connection_name_str, stream_settings_ptr->stream_id,
                                stream_settings_ptr->video_params.vertical_size,
                                video_config_ptr->vertical_size);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->start_horizontal_pos != stream_settings_ptr->video_params.start_horizontal_pos) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video start horizontal position "
                                "[%d] but got [%d].",
                                test_settings_ptr->connection_name_str, stream_id,
                                stream_settings_ptr->video_params.start_horizontal_pos,
                                video_config_ptr->start_horizontal_pos);
            connection_info_ptr->pass_status = false;
        }
        if (video_config_ptr->horizontal_size != stream_settings_ptr->video_params.horizontal_size) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video horizontal size [%d] but got "
                                "[%d].",
                                test_settings_ptr->connection_name_str, stream_id,
                                stream_settings_ptr->video_params.horizontal_size,
                                video_config_ptr->horizontal_size);
            connection_info_ptr->pass_status = false;
        }
    }
}

/**
 * This function validates an AVM baseline configuration. The pass_status of the TestConnectionInfo will be set to false
 * if there is an error in the configuration.
 *
 * @param cb_data_ptr Pointer to the CdiAvmRxCbData.
 * @param baseline_config_ptr Pointer to the baseline configuration structure.
 * @param stream_index Index of the streams settings array for this particular stream.
 */
static void VerifyAvmConfiguration(const CdiAvmRxCbData* cb_data_ptr, CdiAvmBaselineConfig* baseline_config_ptr,
                                   int stream_index)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    // Perform validation of the AVM data.
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->core_cb_data.user_cb_param;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];
    StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
    int stream_id = stream_settings_ptr->stream_id;

    // We only get video and audio config data every N payloads based on the user input --config_skip, which
    // defines how many payloads to skip after receiving config data before receiving it again.
    // Below, we manage the counter for skipping the requested number of payloads, and set the boolean receive_config
    // if this payload should have config data received with it.
    bool expect_config = false;
    if (stream_info_ptr->config_payload_skip_count >= stream_settings_ptr->config_skip) {
        stream_info_ptr->config_payload_skip_count = 0;
        expect_config = true;
    } else {
        stream_info_ptr->config_payload_skip_count++;
    }

    // Only perform this check when the payloads were sent by another cdi_test instance.
    if (GetGlobalTestSettings()->no_payload_user_data) {
        return;
    }

    // For whichever type of AVM data we got, we check the necessary data fields for correctness.
    bool have_valid_config = NULL != baseline_config_ptr;
    switch (stream_settings_ptr->avm_data_type) {
        case kCdiAvmVideo:
            // Make sure config data is received if it's expected with this payload.
            if (expect_config && have_valid_config) {
                CdiAvmVideoConfig* video_config_ptr = have_valid_config ? &baseline_config_ptr->video_config : NULL;
                VerifyAvmVideoConfiguration(connection_info_ptr, video_config_ptr, stream_settings_ptr, stream_index);
            } else if (have_valid_config) {
                // On config data skip payload - make sure there is no video config data.
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected NO video config data, "
                                               "but found some.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;
            } else if (expect_config) {
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected video config data, "
                                               "but found none.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;

            }
            break;
        case kCdiAvmAudio:
            // Make sure config data is received if it's expected with this payload.
            if (expect_config && have_valid_config) {
                CdiAvmAudioConfig* audio_config_ptr = have_valid_config ? &baseline_config_ptr->audio_config : NULL;
                VerifyAvmAudioConfiguration(connection_info_ptr, audio_config_ptr, stream_settings_ptr);
            } else if (have_valid_config) {
                // On config data skip payload - make sure there is no audio config data.
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected NO audio config data, "
                                               "but found some.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;
            } else if (expect_config) {
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected audio config data, "
                                               "but found none.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;
            }
            break;
        case kCdiAvmAncillary:
            // Make sure config data is received if it's expected with this payload.
            if (expect_config && have_valid_config) {
                CdiAvmAncillaryDataConfig* anc_config_ptr = have_valid_config ? &baseline_config_ptr->ancillary_data_config : NULL;
                VerifyAvmAncillaryDataConfiguration(connection_info_ptr, anc_config_ptr, stream_settings_ptr);
            } else if (have_valid_config) {
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected NO ancillary data config data, "
                                               "but found some.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;
            } else if (expect_config) {
                TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected ancillary data config data, "
                                               "but found none.",
                                    test_settings_ptr->connection_name_str, stream_id);
                connection_info_ptr->pass_status = false;
            }
            break;
        case kCdiAvmNotBaseline:
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx invalid payload type. Timestamp[%u:%u].",
                                test_settings_ptr->connection_name_str,
                                stream_id,
                                cb_data_ptr->core_cb_data.core_extra_data.origination_ptp_timestamp.seconds,
                                cb_data_ptr->core_cb_data.core_extra_data.origination_ptp_timestamp.nanoseconds);
            connection_info_ptr->pass_status = false;
    }
}

/**
 * This function finds the index of the stream in the stream_settings array using the specified endpoint handle.
 *
 * @param connection_info_ptr Pointer to test connection information.
 * @param stream_identifier The stream identifier to search for.
 *
 * @return The index of the selected stream_settings array. If an index is not found, return -1.
 */
static int GetStreamSettingsIndexFromStreamId(const TestConnectionInfo* connection_info_ptr, int stream_identifier)
{
    for (int i = 0; i < connection_info_ptr->test_settings_ptr->number_of_streams; i++) {
        if (connection_info_ptr->test_settings_ptr->stream_settings[i].stream_id == stream_identifier) {
            return i;
        }
    }
    return -1;
}

/**
 * Handle the RX callback for AVM data payloads.  This callback will check AVM specific configuration data before
 * calling TestRxProcessCoreCallbackData().
 *
 * @param   cb_data_ptr  Pointer to rx callback data for AVM payloads.
 */
static void TestAvmRxCallback(const CdiAvmRxCbData* cb_data_ptr)
{
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.

    // Perform validation of the AVM data.
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->core_cb_data.user_cb_param;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StreamSettings* stream_settings_ptr = NULL;
    TestConnectionStreamInfo* stream_info_ptr = NULL;

    // Use the stream id to find the stream index in the stream_settings array.
    int stream_id = cb_data_ptr->avm_extra_data.stream_identifier;
    int stream_index = GetStreamSettingsIndexFromStreamId(connection_info_ptr, stream_id);
    if (stream_index >= 0) {
        stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
        stream_info_ptr = &connection_info_ptr->stream_info[stream_index];
        assert(stream_settings_ptr->stream_id == stream_id);
    }

    // Verify that a stream was found with the user-defined stream_id.
    if (NULL == stream_settings_ptr) {
        TEST_LOG_CONNECTION(kLogError, "Connection[%s]: Unexpected stream with ID[%d] in this connection.",
                            test_settings_ptr->connection_name_str, stream_id);
        connection_info_ptr->pass_status = false;
        if (kCdiStatusOk != cb_data_ptr->core_cb_data.status_code) {
            TEST_LOG_CONNECTION(kLogError, "Connection[%s] Rx Callback received error code[%d], Message[%s]",
                                test_settings_ptr->connection_name_str, cb_data_ptr->core_cb_data.status_code,
                                CdiCoreStatusToString(cb_data_ptr->core_cb_data.status_code));
        }
    } else if ((0 == test_settings_ptr->num_transactions) ||
               (stream_info_ptr->payload_count < test_settings_ptr->num_transactions)) {
        // Always check that the expected type of AVM payload (audio, video, etc.) was received if config provided.
        CdiAvmBaselineConfig baseline_config = { 0 };
        if (NULL != cb_data_ptr->config_ptr) {
            // Attempt to convert the generic configuration structure to a baseline profile configuration structure.
            CdiReturnStatus rc = CdiAvmParseBaselineConfiguration(cb_data_ptr->config_ptr, &baseline_config);
            if (kCdiStatusOk == rc) {
                CdiBaselineAvmPayloadType expected_payload_type = stream_settings_ptr->avm_data_type;
                if (expected_payload_type != baseline_config.payload_type) {
                    TEST_LOG_CONNECTION(kLogError, "Connection[%s] Stream ID[%d]: Rx expected payload type[%d] but got [%d].",
                                        test_settings_ptr->connection_name_str, stream_id,
                                        expected_payload_type, baseline_config.payload_type);
                    connection_info_ptr->pass_status = false;
                }
            } else {
                TEST_LOG_CONNECTION(kLogWarning, "Failed to parse baseline configuration [%s].", CdiCoreStatusToString(rc));
                connection_info_ptr->pass_status = false;
            }
        }

        if (connection_info_ptr->pass_status) {
            CdiAvmBaselineConfig* baseline_config_ptr = (NULL == cb_data_ptr->config_ptr) ? NULL : &baseline_config;
            VerifyAvmConfiguration(cb_data_ptr, baseline_config_ptr, stream_index);
        }
        stream_info_ptr->next_payload_size = cb_data_ptr->sgl.total_data_size;
    } else {
        // This is an unexpected payload. Warn for the first and skip it.
        if (test_settings_ptr->num_transactions &&
            stream_info_ptr->payload_count == test_settings_ptr->num_transactions) {
            TEST_LOG_CONNECTION(kLogWarning, "Receiving unexpected payloads for stream [%d]. Ignoring them.", stream_id);
            stream_info_ptr->payload_count++; // Increment so we warn only once.
        }
        CdiCoreRxFreeBuffer(&cb_data_ptr->sgl);
        return;
    }

    // If the pass status is still 'true' then process the callback data.
    if (connection_info_ptr->pass_status) {
        TestRxProcessCoreCallbackData(&cb_data_ptr->core_cb_data, stream_index);
    }

    // Perform any cleanup operation on this data including writing the data to the destination FIFO and incrementing
    // the payload count.
    RxCoreCallbackCleanup(&cb_data_ptr->core_cb_data, &cb_data_ptr->sgl, stream_index);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

// This function creates an Rx connection and monitors received payloads, checking for pass/fail.
CDI_THREAD TestRxCreateThread(void* arg_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)arg_ptr;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;

    // Assign the connection name from TestSettings to this connection's configuration data.
    char* connection_name_str = test_settings_ptr->connection_name_str;
    connection_info_ptr->config_data.rx.connection_name_str = connection_name_str;

    // Setup log files for the test application's and SDK's connection.
    CdiLogMethodData log_method_data;
    char sdk_log_filename_str[MAX_LOG_FILENAME_LENGTH];
    bool got_error = !TestCreateConnectionLogFiles(connection_info_ptr, &log_method_data, sdk_log_filename_str);
    if (got_error) {
        CDI_LOG_THREAD(kLogFatal, "Failed to create log file for Rx connection[%s]", connection_name_str);
        connection_info_ptr->pass_status = false;
        return 0; // Return value is not used.
    }

    // Now that we have the log setup for this connection, we set this thread to use it. Can then use the
    // CDI_LOG_THREAD() macro to log to it from this thread.
    CdiLoggerThreadLogSet(connection_info_ptr->app_file_log_handle);

    // Set up receiver parameters. and create the RX connection.
    connection_info_ptr->config_data.rx.dest_port = test_settings_ptr->dest_port;
    connection_info_ptr->config_data.rx.shared_thread_id = test_settings_ptr->shared_thread_id;
    connection_info_ptr->config_data.rx.thread_core_num = test_settings_ptr->thread_core_num;
    connection_info_ptr->config_data.rx.rx_buffer_type = test_settings_ptr->buffer_type;
    connection_info_ptr->config_data.rx.buffer_delay_ms = test_settings_ptr->rx_buffer_delay_ms;
    // Find the largest payload size of all of the streams, and set the linear_buffer_size to be that size.
    int max_payload_size = test_settings_ptr->stream_settings[0].payload_size;
    for (int i=1; i<test_settings_ptr->number_of_streams; i++) {
        if (test_settings_ptr->stream_settings[i].payload_size > max_payload_size) {
            max_payload_size = test_settings_ptr->stream_settings[i].payload_size;
        }
    }
    connection_info_ptr->config_data.rx.linear_buffer_size = max_payload_size;
    connection_info_ptr->config_data.rx.user_cb_param = connection_info_ptr;
    connection_info_ptr->config_data.rx.connection_log_method_data_ptr = &log_method_data;

    // Configure connection callback.
    connection_info_ptr->config_data.rx.connection_cb_ptr = TestConnectionCallback;
    connection_info_ptr->config_data.rx.connection_user_cb_param = connection_info_ptr;

    // Configure statistics period and callback.
    connection_info_ptr->config_data.rx.stats_config.stats_period_seconds = test_settings_ptr->stats_period_seconds;
    connection_info_ptr->config_data.rx.stats_cb_ptr = TestStatisticsCallback;
    connection_info_ptr->config_data.rx.stats_user_cb_param = connection_info_ptr;

    // Create a FIFO instance for the callback routine to pass SGL pointers to the checking thread.
    if (!got_error) {
        got_error = !CdiFifoCreate("TestRxPayloadState FIFO", CDI_MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION*10,
                                   sizeof(TestRxPayloadState), NULL, NULL, &connection_info_ptr->fifo_handle);
    }

    for (int stream_index=0; !got_error && stream_index<test_settings_ptr->number_of_streams; stream_index++) {
        StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
        TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];
        TestPatternType pattern_type = stream_settings_ptr->pattern_type;
        bool need_expected_data_buffer = true;
        if (NULL == stream_settings_ptr->file_read_str &&
                (kTestPatternNone == pattern_type || kTestPatternIgnore == pattern_type)) {
            need_expected_data_buffer = false;
        }
        // If rx is doing payload data checking allocate a buffer and prepare buffer or file for data checking.
        if (!got_error && need_expected_data_buffer) {
            connection_info_ptr->stream_info[stream_index].rx_expected_data_buffer_ptr =
                CdiOsMemAlloc(stream_info_ptr->payload_buffer_size);
            if (NULL == connection_info_ptr->stream_info[stream_index].rx_expected_data_buffer_ptr) {
                CDI_LOG_THREAD(kLogError, "Failed to allocate memory for user data buffer.");
                got_error = true;
            } else {
                got_error = !PreparePayloadData(stream_settings_ptr, stream_info_ptr->payload_buffer_size,
                                        &stream_info_ptr->user_data_read_file_handle,
                                        stream_info_ptr->rx_expected_data_buffer_ptr);
            }
        }

        // Preload first payload checking buffer.
        if (!got_error && (NULL != stream_info_ptr->rx_expected_data_buffer_ptr)) {
            if (stream_settings_ptr->riff_file && stream_info_ptr->user_data_read_file_handle) {
                got_error = !GetNextRiffChunkSize(stream_settings_ptr,
                    stream_info_ptr->user_data_read_file_handle, &stream_info_ptr->next_payload_size);
            }
            if (!got_error) {
                got_error = !GetNextPayloadDataLinear(connection_info_ptr, stream_settings_ptr, stream_info_ptr);
            }
        }

        if (!got_error) {
            const char* file_write_str = stream_settings_ptr->file_write_str;
            if (NULL != file_write_str) {
                if (!CdiOsOpenForWrite(file_write_str, &stream_info_ptr->user_data_write_file_handle)) {
                    CDI_LOG_THREAD(kLogError, "Error opening file[%s] for writing.", file_write_str);
                    got_error = true;
                }
            }

            // If writing a RIFF file write the file header.
            if (!got_error && stream_settings_ptr->riff_file) {
                RiffFileHeader file_header = {
                    .chunk_header.four_cc = "RIFF",
                    .chunk_header.size = 0,
                    .form_type = "CDI "
                };

                // We don't know the file size at this point. Size is updated before closing the file.
                if (!CdiOsWrite(stream_info_ptr->user_data_write_file_handle, &file_header, sizeof(RiffFileHeader))) {
                    CDI_LOG_THREAD(kLogError, "Failed to write file header for RIFF file output.");
                    got_error = true;
                }
            }
        }
    }

    if (!got_error) {
        CDI_LOG_THREAD(kLogInfo, "Setting up Rx connection. Protocol[%s] Destination Port[%d] Name[%s]",
                       CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, test_settings_ptr->connection_protocol),
                       connection_info_ptr->config_data.rx.dest_port, connection_name_str);

        // Based on the user-specified protocol type, we either plan to receive RAW payloads or AVM payloads. Do this
        // only after all of the resources for testing the connection are prepared because callbacks can start even
        // before the create function returns.
        if (kProtocolTypeRaw == test_settings_ptr->connection_protocol) {
            got_error = (kCdiStatusOk != CdiRawRxCreate(&connection_info_ptr->config_data.rx, &TestRawRxCallback,
                                                        &connection_info_ptr->connection_handle));
        } else {
            got_error = (kCdiStatusOk != CdiAvmRxCreate(&connection_info_ptr->config_data.rx, &TestAvmRxCallback,
                                                        &connection_info_ptr->connection_handle));
        }

        // If connection name was not specified, copy the name generated by the CDI SDK.
        if (!got_error && '\0' == connection_name_str[0]) {
            CdiOsStrCpy(connection_name_str, sizeof(test_settings_ptr->connection_name_str),
                        connection_info_ptr->config_data.rx.connection_name_str);
        }

        if (got_error) {
            CdiLogMultilineState m_state;
            CDI_LOG_THREAD_MULTILINE_BEGIN(kLogError, &m_state);
            CDI_LOG_MULTILINE(&m_state, "Failed to create Rx connection. Protocol[%s] Destination Port[%d] Name[%s]",
                CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, test_settings_ptr->connection_protocol),
                connection_info_ptr->config_data.rx.dest_port, connection_name_str);
            CDI_LOG_MULTILINE(&m_state, "Some other application (or another instance of this application) may be using "
                                        "the requested port.");
            CDI_LOG_MULTILINE_END(&m_state);
        }
    }

    // Run the verify task, which will just sit and wait until payloads arrive and then check them.
    if (!got_error) {
        got_error = !TestRxVerify(connection_info_ptr);
    }

    if (connection_info_ptr->connection_handle) {
        // When the connection is closed, the connection_handle will be set to NULL. We will protect access to it
        // with a lock so the main thread that updates stats won't crash.
        CdiOsCritSectionReserve(connection_info_ptr->connection_handle_lock);
        got_error = (kCdiStatusOk != CdiCoreConnectionDestroy(connection_info_ptr->connection_handle));
        connection_info_ptr->connection_handle = NULL;
        CdiOsCritSectionRelease(connection_info_ptr->connection_handle_lock);
    }

    // By closing the connection, the SDK sends the last set of stats using our user-registered callback function
    // TestStatisticsCallback(). So, now print RX final statistics.
    CdiPayloadCounterStats total_stats = { 0 };
    for (int i = 0; i < connection_info_ptr->number_stats; i++) {
        total_stats.num_payloads_transferred += connection_info_ptr->payload_counter_stats_array[i].num_payloads_transferred;
        total_stats.num_payloads_dropped += connection_info_ptr->payload_counter_stats_array[i].num_payloads_dropped;
        total_stats.num_payloads_late += connection_info_ptr->payload_counter_stats_array[i].num_payloads_late;
    }
    const CdiPayloadCounterStats* counter_stats_ptr = &total_stats;

    // Write these stats out to the log associated with this thread.  We use the CDI_LOG_THREAD_MULTILINE_BEGIN macro,
    // which automatically creates a multiline log buffer associated with this thread, and we use the returned handle
    // to keep writing lines to that buffer via CDI_LOG_MULTILINE until we are ready to print with CDI_LOG_MULTILINE_END.
    CdiLogMultilineState handle;
    CDI_LOG_THREAD_MULTILINE_BEGIN(kLogInfo, &handle);
    CDI_LOG_MULTILINE(&handle, "Connection[%s] Rx Stats:", test_settings_ptr->connection_name_str);
    CDI_LOG_MULTILINE(&handle, "Number of payloads transferred[%"PRIu64"]", counter_stats_ptr->num_payloads_transferred);
    CDI_LOG_MULTILINE(&handle, "Number of payloads dropped    [%"PRIu64"]", counter_stats_ptr->num_payloads_dropped);
    CDI_LOG_MULTILINE(&handle, "Number of payloads late       [%"PRIu64"]", counter_stats_ptr->num_payloads_late);
    CDI_LOG_MULTILINE(&handle, "Number of payload errors      [%"PRIu64"]", connection_info_ptr->num_payload_errors);
    CDI_LOG_MULTILINE_END(&handle);

    // Destroy resources if they got created above.
    for (int i=0; i<connection_info_ptr->test_settings_ptr->number_of_streams; i++) {
        if (connection_info_ptr->stream_info[i].user_data_write_file_handle) {
            // RIFF file specifies that bytes [4-7] at the top of the file contain the file size so write the file size
            // before closing the file now that the size is known.
            if(connection_info_ptr->test_settings_ptr->stream_settings[i].riff_file) {
                uint64_t file_size;
                bool set_size_successful = CdiOsFTell(connection_info_ptr->stream_info[i].user_data_write_file_handle,
                                                      &file_size);

                // The file size is the size minus the chunk header for the RIFF chunk.
                if (file_size > sizeof(RiffChunkHeader)) {
                    file_size = file_size - sizeof(RiffChunkHeader);
                } else {
                    file_size = 0;
                }
                set_size_successful = set_size_successful &&
                                      CdiOsFSeek(connection_info_ptr->stream_info[i].user_data_write_file_handle,
                                                 offsetof(RiffFileHeader, chunk_header.size), SEEK_SET);
                set_size_successful = set_size_successful &&
                                      CdiOsWrite(connection_info_ptr->stream_info[i].user_data_write_file_handle,
                                                 &file_size, sizeof(((RiffFileHeader *)NULL)->chunk_header.size));
                if (!set_size_successful) {
                    CDI_LOG_THREAD(kLogError, "Failed to write file size to output RIFF file [%s].",
                                    connection_info_ptr->test_settings_ptr->stream_settings[i].file_write_str);
                }
            }

            CdiOsClose(connection_info_ptr->stream_info[i].user_data_write_file_handle);
        }

        if (connection_info_ptr->stream_info[i].user_data_read_file_handle) {
            CdiOsClose(connection_info_ptr->stream_info[i].user_data_read_file_handle);
        }

        if (connection_info_ptr->stream_info[i].rx_expected_data_buffer_ptr) {
            CdiOsMemFree(connection_info_ptr->stream_info[i].rx_expected_data_buffer_ptr);
        }
    }

    CdiFifoFlush(connection_info_ptr->fifo_handle); // Ensure FIFO has been flushed before destroying it.
    CdiFifoDestroy(connection_info_ptr->fifo_handle);
    CdiLoggerDestroyLog(connection_info_ptr->app_file_log_handle);
    CdiLoggerDestroyLog(connection_info_ptr->sdk_file_callback_log_handle);

    if (got_error) {
        connection_info_ptr->pass_status = false;
    }

    // Make sure to set this signal so the test can exit.
    CdiOsSignalSet(connection_info_ptr->done_signal);

    CdiLoggerThreadLogUnset();
    return 0; // This is not used.
}