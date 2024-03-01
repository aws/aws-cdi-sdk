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

#include "cdi_baseline_profile_01_00_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "fifo_api.h"
#include "cdi_pool_api.h"
#include "test_common.h"
#include "ndi_test.h"
#include "ndi_wrapper.h"
#include <Processing.NDI.Lib.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Assume 20 frames of 1080, 8-bit video frame is maximum payload size.
#define RX_LINEAR_BUFFER_SIZE           (20 * 1920 * 1080 * 3)

/// @brief Number of NDI audio buffer pool entries.
#define NDI_AUDIO_BUFFER_POOL_ENTRIES   (100)

/// @brief Size in bytes of each NDI audio buffer in pool.
#define NDI_AUDIO_BUFFER_SIZE           (20000)

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Handle the Rx AVM callback.
 *
 * @param cb_data_ptr Pointer to Tx RAW callback data.
 */
static void AvmRxCallback(const CdiAvmRxCbData* cb_data_ptr)
{
    TestConnectionInfo* con_info_ptr = (TestConnectionInfo*)cb_data_ptr->core_cb_data.user_cb_param;

    if (kCdiStatusOk != cb_data_ptr->core_cb_data.status_code) {
        CDI_LOG_THREAD(kLogError, "Receive payload failed[%s].",
                       CdiCoreStatusToString(cb_data_ptr->core_cb_data.status_code));
        con_info_ptr->payload_error = true;
    } else {
        CdiOsAtomicInc32(&con_info_ptr->payload_cb_count);
    }

    CdiReturnStatus rs = kCdiStatusOk;
    CdiAvmBaselineConfig baseline_config = {0};
    if (NULL != cb_data_ptr->config_ptr) {
        // Attempt to convert the generic configuration structure to a baseline profile configuration structure.
        rs = CdiAvmParseBaselineConfiguration(cb_data_ptr->config_ptr, &baseline_config);
        if (kCdiStatusOk != rs) {
            CDI_LOG_THREAD(kLogWarning, "Failed to parse baseline configuration [%s].", CdiCoreStatusToString(rs));
            rs = kCdiStatusNonFatal;
        }
    }

    if (kCdiStatusOk == rs) {
        TestLogAVMChanges(cb_data_ptr->avm_extra_data.stream_identifier, cb_data_ptr->sgl.total_data_size,
                          cb_data_ptr->config_ptr, &baseline_config,
                          &con_info_ptr->last_baseline_config[baseline_config.payload_type]);
    }

    if (kCdiStatusOk == rs) {
        // Get frame data buffer from memory pool.
        FrameData* frame_data_ptr = NULL;
        if (!CdiPoolGet(con_info_ptr->ndi_frame_data_pool_handle, (void**)&frame_data_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to Get NDI Frame buffer from pool.");
            con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
            assert(false);
        }

        memset(frame_data_ptr, 0, sizeof(*frame_data_ptr));
        frame_data_ptr->connect_info_ptr = con_info_ptr;
        frame_data_ptr->rx_sgl = cb_data_ptr->sgl;

        if (baseline_config.payload_type == kCdiAvmAudio) {
            // CDI uses 24-bit Big-Endian PCM. NDI uses 32-bit float Little-Endian, so must use a larger buffer to
            // hold the NDI audio data.
            if (!CdiPoolGet(con_info_ptr->ndi_audio_pool_handle, (void**)&frame_data_ptr->data.audio_frame.p_data)) {
                CDI_LOG_THREAD(kLogError, "Failed to Get NDI audio frame buffer from pool.");
                con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                assert(false);
            }
            frame_data_ptr->p_data_size = NDI_AUDIO_BUFFER_SIZE;
        }

        CdiPtpTimestamp cdi_timestamp = cb_data_ptr->core_cb_data.core_extra_data.origination_ptp_timestamp;
        rs = NdiConvertCdiToNdi(&cdi_timestamp, &baseline_config, cb_data_ptr->sgl.total_data_size,
                                &cb_data_ptr->sgl, frame_data_ptr);

        if (kCdiStatusOk == rs) {
            LogTimestamps(con_info_ptr, frame_data_ptr, &cdi_timestamp);

            // Write frame data to the Payload FIFO.
            if (!CdiFifoWrite(con_info_ptr->payload_fifo_handle, 1, NULL, &frame_data_ptr)) {
                CDI_LOG_THREAD(kLogError, "Failed to write Payload FIFO.");
                con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                CdiCoreRxFreeBuffer(&cb_data_ptr->sgl);
                if (baseline_config.payload_type == kCdiAvmAudio) {
                    CdiPoolPut(con_info_ptr->ndi_audio_pool_handle, frame_data_ptr->data.audio_frame.p_data);
                    frame_data_ptr->data.audio_frame.p_data = NULL;
                }
                assert(false);
            }
        }
    }

    // Set the payload callback signal to wakeup the app.
    CdiOsSignalSet(con_info_ptr->payload_callback_signal);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiReceiverToNdiTransmitter(TestConnectionInfo* con_info_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (kCdiStatusOk == rs) {
        // Create an NDI audio buffer memory pool.
        if (!CdiPoolCreate("CDI Audio Frame Pool", // Name of the pool.
                           NDI_AUDIO_BUFFER_POOL_ENTRIES, // Number of pool items.
                           0, // Grow count size (don't want to grow).
                           0, // Max grow count (don't want to grow).
                           NDI_AUDIO_BUFFER_SIZE, // Size of each entry.
                           true, // true = Make thread-safe.
                           &con_info_ptr->ndi_audio_pool_handle)) // Returned handle to the pool.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create Audio Frame Data Pool.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create NDI Thread.
        if (!CdiOsThreadCreate(NdiTransmitPayloadThread, // Pointer to a function for the thread.
                                &con_info_ptr->ndi_thread_id, // Pointer to CdiThreadID to return.
                                "NdiRxThread", // Name of Thread.
                                con_info_ptr, // Pointer to frame data passed to the thread delegate.
                                NULL)) // Signal used to start the thread. If NULL, thread starts running immediately.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create NDI transmit thread.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    // Create a AVM Rx connection.
    if (kCdiStatusOk == rs) {
        CdiRxConfigData config_data = {
            // Settings that are unique to an Rx connection.
            .rx_buffer_type = kCdiLinearBuffer,
            .linear_buffer_size = RX_LINEAR_BUFFER_SIZE, // Required for kCdiLinearBuffer type buffer.
            .user_cb_param = con_info_ptr,

            // Settings that are common between Rx and Tx connections.
            .adapter_handle = con_info_ptr->adapter_handle,
            .dest_port = con_info_ptr->test_settings.dest_port,
            .bind_ip_addr_str = con_info_ptr->test_settings.bind_ip_addr_str,
            .shared_thread_id = 0, // 0 or -1= Use unique poll thread for this connection.
            .thread_core_num = -1, // -1= Let OS decide which CPU core to use.

            .connection_name_str = NULL,
            .connection_log_method_data_ptr = &con_info_ptr->log_method_data,

            .connection_cb_ptr = TestConnectionCallback, // Configure connection callback.
            .connection_user_cb_param = con_info_ptr,

            .stats_cb_ptr = NULL, // Statistics gathering settings (not used here).
            .stats_user_cb_param = NULL,
            .stats_config.stats_period_seconds = 0,
            .stats_config.disable_cloudwatch_stats = true
        };
        rs = CdiAvmRxCreate(&config_data, AvmRxCallback, &con_info_ptr->connection_handle);
    }

    con_info_ptr->pNDI_send = NdiCreateSender(&con_info_ptr->test_settings);
    if (con_info_ptr->pNDI_send == NULL) {
        CDI_LOG_THREAD(kLogFatal, "NDI failed to create NDI sender.");
        return false;
    }

    // Wait for connection to be established with remote CDI source.
    while (kCdiStatusOk == rs && kCdiConnectionStatusDisconnected == con_info_ptr->connection_status) {
        CDI_LOG_THREAD(kLogInfo, "Waiting to establish connection with remote CDI source...");
        CdiOsSignalWait(con_info_ptr->connection_state_change_signal, CDI_INFINITE, NULL);
        CdiOsSignalClear(con_info_ptr->connection_state_change_signal);
    }
    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "CDI Connected. Waiting to receive CDI payloads...");
    }

    // Loop until the desired number of payloads are received. If we get any errors or the connection
    // drops, then exit the loop.
    int payload_count = 0;
    while (kCdiStatusOk == rs &&
           (0 == con_info_ptr->test_settings.num_transactions ||
            payload_count < con_info_ptr->test_settings.num_transactions)) {
        // Wait for AvmRxCallback() to be called or connection state change. The callback is invoked whenever a
        // payload has been received.
        CdiSignalType signal_array[2];
        signal_array[0] = con_info_ptr->connection_state_change_signal;
        signal_array[1] = con_info_ptr->payload_callback_signal;
        uint32_t signal_index = 0;
        CdiOsSignalsWait(signal_array, 2, false, 2000/*CDI_INFINITE*/, &signal_index);
        if (CDI_OS_SIG_TIMEOUT == signal_index) {
            if (kCdiConnectionStatusConnected == con_info_ptr->connection_status) {
                CDI_LOG_THREAD(kLogInfo, "No CDI payloads being received. Port[%d].",
                                con_info_ptr->test_settings.dest_port);
            }
            continue;
        } else if (0 == signal_index) {
            CdiOsSignalClear(con_info_ptr->connection_state_change_signal);
        } else {
            // Got payload callback signal.

            // Update local copy of counter that is incremented by the Rx callback. NOTE: To reduce code complexity,
            // synchronization logic has not been added here concerning the use of this counter and payload_callback_signal.
            // The Rx callback function may be invoked more than once before this logic is executed again.
            payload_count = CdiOsAtomicRead32(&con_info_ptr->payload_cb_count);

            CdiOsSignalClear(con_info_ptr->payload_callback_signal);
        }

        // Update console with progress message.
        if (0 == (payload_count % PAYLOAD_PROGRESS_UPDATE_FREQUENCY)) {
            printf("\rReceived CDI [%d] payloads.", payload_count);
            fflush(stdout);
        }
    }

    if (kCdiStatusOk == rs) {
        TestConsoleLog(kLogInfo, "");
        CDI_LOG_THREAD(kLogInfo, "All done. Received [%d] CDI payloads. Shutting down.", payload_count);
    }

    // Signal NDI thread to terminate and wait for completion.
    CdiOsSignalSet(con_info_ptr->ndi_thread_signal);
    CdiOsThreadJoin(con_info_ptr->ndi_thread_id, CDI_INFINITE, NULL);

    // Destroy NDI Pool.
    CdiPoolPutAll(con_info_ptr->ndi_audio_pool_handle);
    CdiPoolDestroy(con_info_ptr->ndi_audio_pool_handle);

    return rs;
}
