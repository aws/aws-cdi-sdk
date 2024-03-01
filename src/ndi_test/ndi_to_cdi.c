// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the NDI<>CDI Converter application.
*/

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "fifo_api.h"
#include "test_common.h"
#include "utilities_api.h"
#include "ndi_test.h"
#include "ndi_wrapper.h"
#include <Processing.NDI.Lib.h>

extern void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr);

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Process core Tx callback.
 *
 * @param con_info_ptr Pointer to test connection state data.
 * @param core_cb_data_ptr Pointer to core callback data.
 */
static void ProcessCoreTxCallback(TestConnectionInfo* con_info_ptr, const CdiCoreCbData* core_cb_data_ptr)
{
    int count = CdiOsAtomicInc32(&con_info_ptr->payload_cb_count);

    if (kCdiStatusOk != core_cb_data_ptr->status_code) {
        CDI_LOG_THREAD(kLogError, "Send payload failed[%s].", CdiCoreStatusToString(core_cb_data_ptr->status_code));
    } else {
        uint64_t timeout_time = con_info_ptr->payload_start_time + con_info_ptr->test_settings.tx_timeout;
        uint64_t current_time = CdiOsGetMicroseconds();
        if (current_time > timeout_time) {
            CDI_LOG_THREAD(kLogError, "Payload [%d] late by [%"PRIu64"]us.", count, current_time - timeout_time);
        }
    }

    // Set the payload callback signal to wakeup the app, if it was waiting.
    CdiOsSignalSet(con_info_ptr->payload_callback_signal);
}

/**
 * @brief Done with a NDI payload.
 *
 * @param con_info_ptr Pointer to test connection information.
 * @param frame_data_ptr Pointer to NDI frame data.
 */
static void DoneWithPayload(TestConnectionInfo* con_info_ptr, FrameData* frame_data_ptr)
{
    // if video frame is successfully called back, write video frame payload information to Callback FIFO.
    // It is necessary to keep video frame till it is known that NDI has another video frame to transmit.
    // If NDI does not have another video frame to transmit, then program does not release memory of current video
    // frame so that it can repeat this frame till further notice. This accounts for scenarios, such as odd frame
    // rates, so that video is always streamed.
    if (frame_data_ptr->frame_type == kNdiVideo) {
        // Write payload information to the Callback FIFO.
        if (!CdiFifoWrite(con_info_ptr->callback_fifo_handle, 1, NULL, &frame_data_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to write Callback FIFO.");
            con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
            assert(false);
        }
    } else {
        // Audio and Metadata can be released immediately. No scenario suggests that these frames should be repeated.
        NdiReleasePayload(frame_data_ptr);
    }
}

/**
 * @brief Handle the Tx AVM callback. NOTE: Only used by the AVM API functions.
 *
 * @param cb_data_ptr Pointer to Tx AVM callback data.
 */
static void AvmTxCallback(const CdiAvmTxCbData* cb_data_ptr)
{
    FrameData* frame_data_ptr = (FrameData*)cb_data_ptr->core_cb_data.user_cb_param;
    TestConnectionInfo* con_info_ptr = frame_data_ptr->connect_info_ptr;

    ProcessCoreTxCallback(con_info_ptr, &cb_data_ptr->core_cb_data);
    DoneWithPayload(con_info_ptr, frame_data_ptr);
}

/**
 * @brief Send a payload using an AVM API function.
 *
 * @param frame_data_ptr Pointer to frame data structure.
 * @param sgl_ptr Pointer to SGL.
 * @param timestamp_ptr Pointer to timestamp.
 * @param avm_config_ptr Pointer to the generic configuration structure to use for the stream.
 * @param stream_identifier Stream identifier.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus SendAvmPayload(FrameData* frame_data_ptr, CdiSgList* sgl_ptr,
                                      CdiPtpTimestamp* timestamp_ptr, CdiAvmConfig* avm_config_ptr,
                                      int stream_identifier)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiAvmTxPayloadConfig payload_config = {
        .core_config_data.core_extra_data.origination_ptp_timestamp = *timestamp_ptr,
        .core_config_data.core_extra_data.payload_user_data = 0,
        .core_config_data.user_cb_param = frame_data_ptr,
        .core_config_data.unit_size = 0,
        .avm_extra_data.stream_identifier = stream_identifier
    };

    if (kCdiStatusOk == rs) {
        // Send the payload, retrying if the queue is full.
        do {
            rs = CdiAvmTxPayload(frame_data_ptr->connect_info_ptr->connection_handle, &payload_config, avm_config_ptr,
                                 sgl_ptr, frame_data_ptr->connect_info_ptr->test_settings.tx_timeout);
        } while (kCdiStatusQueueFull == rs);
    }

    return rs;
}

/**
 * @brief Return a CDI PTP timestamp for the specified NDI frame.
 *
 * @param con_info_ptr Pointer to connection information structure.
 * @param frame_data_ptr Pointer to frame data structure.

 * @return Timestamp that should be used for the frame.
 */
static CdiPtpTimestamp GetPtpTimestamp(TestConnectionInfo* con_info_ptr, const FrameData* frame_data_ptr)
{
    CdiPtpTimestamp timestamp;
    uint64_t duration_ns;

    if (frame_data_ptr->frame_type == kNdiVideo || frame_data_ptr->frame_type == kNdiMetaData) {
        // Video and metadata uses the total number of frames processed to determine the timestamp.
        duration_ns = (con_info_ptr->total_video_frames * (uint64_t)CDI_NANOSECONDS_PER_SECOND *
                      frame_data_ptr->data.video_frame.frame_rate_D) / frame_data_ptr->data.video_frame.frame_rate_N;
        if (frame_data_ptr->frame_type == kNdiVideo) {
            // For video, increment the video frame counter used above to calculate timestamps.
            con_info_ptr->total_video_frames++;
        }
    } else if (frame_data_ptr->frame_type == kNdiAudio) {
        // Audio uses the total number of audio samples processed to determine the timestamp.
        duration_ns = con_info_ptr->total_audio_samples * CDI_NANOSECONDS_PER_SECOND /
                      frame_data_ptr->data.audio_frame.sample_rate;
        // Now, add the number of audio samples in this frame to the running total used above to calculate timestamps.
        con_info_ptr->total_audio_samples += frame_data_ptr->data.audio_frame.no_samples;
    }

    // Add the existing start time nanoseconds to the duration so the logic below calculates the correct seconds and
    // nanoseconds.
    duration_ns += con_info_ptr->connection_start_time.nanoseconds;
    timestamp.seconds = con_info_ptr->connection_start_time.seconds + duration_ns / CDI_NANOSECONDS_PER_SECOND;
    timestamp.nanoseconds = duration_ns % CDI_NANOSECONDS_PER_SECOND;

    return timestamp;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus NdiReceiverToCdiTransmitter(TestConnectionInfo* con_info_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Create a AVM Tx connection.
    if (kCdiStatusOk == rs) {
        CdiTxConfigData config_data = {
            // Settings that are unique to a Tx connection.
            .dest_ip_addr_str = con_info_ptr->test_settings.remote_adapter_ip_str,
            .adapter_handle = con_info_ptr->adapter_handle,
            .dest_port = con_info_ptr->test_settings.dest_port,
            .shared_thread_id = 0, // 0 or -1= Use unique poll thread for this connection.
            .thread_core_num = -1, // -1= Let OS decide which CPU core to use.
            .connection_name_str = NULL,
            .connection_log_method_data_ptr = &con_info_ptr->log_method_data,

            .connection_cb_ptr = TestConnectionCallback, // Configure connection callback.
            .connection_user_cb_param = con_info_ptr,

            .stats_cb_ptr = NULL, // Statistics gathering settings (not used here).
            .stats_user_cb_param = NULL,
            .stats_config.stats_period_seconds = 0,
            .stats_config.disable_cloudwatch_stats = true,
        };
        rs = CdiAvmTxCreate(&config_data, AvmTxCallback,  &con_info_ptr->connection_handle);
    }

    // Setup NDI receiver using media source specified in Test Settings and wait until source is found.
    con_info_ptr->pNDI_recv = NdiCreateReceiver(&con_info_ptr->test_settings);
    if (con_info_ptr->pNDI_recv == NULL) {
        CDI_LOG_THREAD(kLogFatal, "NDI failed to create NDI receiver.");
        return false;
    }

    // Wait for connection to be established with remote target.
    while (kCdiStatusOk == rs && kCdiConnectionStatusDisconnected == con_info_ptr->connection_status) {
        CDI_LOG_THREAD(kLogInfo, "Waiting to establish connection with CDI remote target...");
        CdiOsSignalWait(con_info_ptr->connection_state_change_signal, CDI_INFINITE, NULL);
        CdiOsSignalClear(con_info_ptr->connection_state_change_signal);
    }
    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "CDI connected. Sending payloads...");
    }

    // Can now send the desired number of payloads. Will send at the specified rate. If we get any
    // errors or the connection drops, then exit the loop.
    int payload_count = 0;

    if (kCdiStatusOk == rs) {
        // Create NDI Thread.
        if (!CdiOsThreadCreate(NdiReceivePayloadThread, // Pointer to a function for the thread.
                                &con_info_ptr->ndi_thread_id, // Pointer to CdiThreadID to return.
                                "NdiRxThread", // Name of Thread.
                                con_info_ptr, // Pointer to frame data passed to the thread delegate.
                                NULL)) // Signal used to start the thread. If NULL, thread starts running immediately.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create NDI thread.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    while (kCdiStatusOk == rs && kCdiStatusOk == con_info_ptr->ndi_thread_rs &&
           (0 == con_info_ptr->test_settings.num_transactions ||
            payload_count < con_info_ptr->test_settings.num_transactions)) {
        // Setup Scatter-gather-list entry for the payload data to send. NOTE: The buffers the SGL entries point to must
        // persist until the payload callback has been made. Since we are reusing the same buffer for each payload, we
        // don't need any additional logic here.
        //
        // NOTE: To demonstrate minimal functionality, a single buffer is used here. Applications typically would use a
        // buffering scheme that supports multiple buffers. This would allow buffers to be written to while additional
        // buffers are used for data transfer.
        CdiSglEntry sgl_entry = {
            .address_ptr = con_info_ptr->adapter_tx_buffer_ptr,
            .size_in_bytes = 0,
        };
        CdiSgList sgl = {
            .total_data_size = 0,
            .sgl_head_ptr = &sgl_entry,
            .sgl_tail_ptr = &sgl_entry,
            .internal_data_ptr = NULL, // Initialize to NULL (not used by application).
        };

        // Read Frame Data information from the Payload FIFO buffer and copy to frame_data_ptr.
        FrameData* frame_data_ptr = NULL;
        if (!CdiFifoRead(con_info_ptr->payload_fifo_handle, CDI_INFINITE, NULL, (void *)&frame_data_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to read FIFO.");
            rs = kCdiStatusFatal;
            break;
        }

        if (0 == con_info_ptr->connection_start_time.seconds) {
            con_info_ptr->connection_start_time = CdiCoreGetPtpTimestamp(NULL);
        }

        // if FIFO has valid information, use NDI frame data to create an AVM structure that is compatible with CDI.
        assert(frame_data_ptr->frame_type == kNdiVideo || frame_data_ptr->frame_type == kNdiAudio ||
               frame_data_ptr->frame_type == kNdiMetaData);
        // Create a PTP timestamp to send along with the payload. CDI doesn't use it, but downstream CDI receiver might.
        CdiPtpTimestamp cdi_timestamp = CdiCoreGetPtpTimestamp(NULL);

        // The NDI timestamp is specified as a 100ns time that was the exact moment that the NDI frame was submitted by
        // the sending side and is generated by the NDI-SDK.
        int64_t ndi_timestamp;

        CdiAvmBaselineConfig baseline_config;
        // Convert NDI video, audio, or metadata frame into CDI compatible configuration.
        if (kCdiStatusOk != rs || kCdiStatusOk != NdiConvertNdiToCdi(frame_data_ptr, &sgl_entry.size_in_bytes,
            &sgl_entry.address_ptr, &ndi_timestamp, &baseline_config)) {
            // Don't continue if a process fails.
            CDI_LOG_THREAD(kLogError, "Error ocurred.");
            break;
        }

        if (!con_info_ptr->test_settings.use_ndi_timestamps) {
            // Internally generate CDI timestamps instead of converting them from NDI. This should be the default
            // behaviour.
            // Note: TODO Logic should be added to account for dropped video/audio frames and repeated video frames.
            cdi_timestamp = GetPtpTimestamp(con_info_ptr, frame_data_ptr);
        } else {
            // Convert the NDI timestamp to CDI.
            NdiTime ndi_time_breakdown = NdiTimeBreakdown(ndi_timestamp);
            cdi_timestamp.seconds = ndi_time_breakdown.ndi_time_in_s;
            cdi_timestamp.nanoseconds = ndi_time_breakdown.ndi_time_in_ns;
        }

        // Updates SGL size to match size of NDI payload.
        sgl.total_data_size = sgl_entry.size_in_bytes;

        // Creates the generic configuration structure to use when sending AVM payloads.
        CdiAvmConfig avm_config = {0};
        int payload_unit_size = 0;
        rs = CdiAvmMakeBaselineConfiguration(&baseline_config, &avm_config, &payload_unit_size);

        if(kCdiStatusOk == rs && kCdiConnectionStatusConnected != con_info_ptr->connection_status) {
            // Throwing payload away, so done with it.
            DoneWithPayload(con_info_ptr, frame_data_ptr);
            frame_data_ptr = NULL;
            continue;
        }

        // Send the payload.
        if (kCdiStatusOk == rs) {
            int stream_identifier = 0; // Used by AVM APIs to identify the stream within a connection.
            if (frame_data_ptr->frame_type == kNdiVideo) {
                // Video.
                stream_identifier = con_info_ptr->test_settings.video_stream_id; // Represents video stream.
            } else if (frame_data_ptr->frame_type == kNdiAudio) {
                // Audio.
                stream_identifier = con_info_ptr->test_settings.audio_stream_id; // Represents audio stream.
            } else if (frame_data_ptr->frame_type == kNdiMetaData) {
                // Metadata.
                stream_identifier = con_info_ptr->test_settings.anc_data_stream_id; // Represents metadata stream.
            }
            LogTimestamps(con_info_ptr, frame_data_ptr, &cdi_timestamp);
            TestLogAVMChanges(stream_identifier, sgl.total_data_size, &avm_config, &baseline_config,
                              &con_info_ptr->last_baseline_config[baseline_config.payload_type]);

            // Setup payload start time.
            con_info_ptr->payload_start_time = CdiOsGetMicroseconds();
            rs = SendAvmPayload(frame_data_ptr, &sgl, &cdi_timestamp, &avm_config, stream_identifier);
            if (kCdiStatusOk != rs) {
                CDI_LOG_THREAD(kLogWarning, "SendAvmPayload Failed. Throwing it away.");
                // Throwing payload away, so done with it.
                DoneWithPayload(con_info_ptr, frame_data_ptr);
                frame_data_ptr = NULL;
                rs = kCdiStatusOk; // Don't want to abort.
            }

            // Update console with progress message.
            if (0 == (++payload_count % PAYLOAD_PROGRESS_UPDATE_FREQUENCY)) {
                printf("\rSent [%d] payloads.", payload_count);
                fflush(stdout);
            }
        }
    }

    CDI_LOG_THREAD(kLogInfo, "");
    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "Waiting for any pending Tx callbacks...");
        // Make sure this signal is clear before we check payload_cb_count and wait for it.
        CdiOsSignalClear(con_info_ptr->payload_callback_signal);

        while (kCdiConnectionStatusConnected == con_info_ptr->connection_status &&
               CdiOsAtomicRead32(&con_info_ptr->payload_cb_count) < payload_count) {
            // Setup to wait on the connection state change and the payload Tx callback signals.
            CdiSignalType signal_array[2];
            signal_array[0] = con_info_ptr->payload_callback_signal;
            signal_array[1] = con_info_ptr->connection_state_change_signal;
            uint32_t signal_index = 0;
            CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, &signal_index);
            if (0 == signal_index) {
                // Got payload callback signal, so clear it incase we need to wait for it again.
                CdiOsSignalClear(con_info_ptr->payload_callback_signal);
            } else {
                // Got connection state change signal. The while loop will exit if we got disconnected.
                CdiOsSignalClear(con_info_ptr->connection_state_change_signal);
            }
        }
    }

    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "All done. Sent [%d] payloads. Shutting down.", payload_count);
    }

    // Signal NDI thread to terminate and wait for completion.
    CdiOsSignalSet(con_info_ptr->ndi_thread_signal);
    CdiOsThreadJoin(con_info_ptr->ndi_thread_id, CDI_INFINITE, NULL);

    // Destroy the receiver.
    NDIlib_recv_destroy(con_info_ptr->pNDI_recv);

    // Shutdown and clean-up CDI SDK resources.
    if (con_info_ptr->connection_handle) {
        CdiCoreConnectionDestroy(con_info_ptr->connection_handle);
    }

    return (kCdiStatusOk == rs && kCdiStatusOk == con_info_ptr->ndi_thread_rs) ? rs : kCdiStatusFatal;
}
