// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in ndi_test.c.
 */

#ifndef NDI_TEST_H__
#define NDI_TEST_H__

#include <stdbool.h>
#include <stdint.h>

#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_log_api.h"
#include "cdi_os_api.h"
#include "fifo_api.h"
#include "cdi_pool_api.h"
#include "ndi_wrapper.h"
#include <Processing.NDI.Lib.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Enable this define to disable use of the RX linear buffer. Packets will arrive in SGL (Scatter Gather List).
//#define DISABLE_LINEAR_RX_BUFFER

/// @brief Default Tx timeout.
#define DEFAULT_TX_TIMEOUT                  (20000)

/// @brief Define TestConsoleLog.
#define TestConsoleLog SimpleConsoleLog

/**
 * @brief A structure that holds all the test settings as set from the command line.
 */
typedef struct TestSettings TestSettings;
struct TestSettings {
    const char* local_adapter_ip_str;  ///< The local network adapter IP address.
    const char* bind_ip_addr_str;      ///< IP address to bind to.
    int dest_port;                     ///< The destination port number.
    const char* remote_adapter_ip_str; ///< The remote network adapter IP address.
    int num_transactions;              ///< The number of transactions in the test.
    int tx_timeout;                    ///< The transmit timeout in microseconds for a Tx payload.
    bool use_efa;                      ///< Whether to use EFA adapter.

    bool show_ndi_sources;             ///< Show NDI sources and stop.
    bool ndi_rx;                       ///< Enable NDI receiver.
    bool ndi_tx;                       ///< Enable NDI transmitter.
    int video_stream_id;               ///< CDI video stream ID.
    int audio_stream_id;               ///< CDI audio stream ID.
    int anc_data_stream_id;            ///< CDI ancilllary data stream ID.

    const char* ndi_source_name;       ///< The NDI source name.
    const char* ndi_source_ip;         ///< The NDI source IP address.
    const char* ndi_source_url;        ///< The NDI source URL address.

    bool use_ndi_timestamps;           ///< Use NDI timestamps for CDI output instead of internally generating them.
    bool log_timestamps;               ///< Output timestamp information to console
};

/**
 * @brief A structure for storing all info related to a specific connection, including test settings, connection
 * configuration data from the SDK, and state information for the test connection.
 */
typedef struct TestConnectionInfo TestConnectionInfo;
struct TestConnectionInfo {
    CdiLogMethodData log_method_data;               ///< CDI log method.
    CdiAdapterHandle adapter_handle;                ///< CDI adapter Handle.
    CdiConnectionHandle connection_handle;          ///< The connection handle returned by CdiRawTxCreate().

    TestSettings test_settings;                     ///< Test settings data structure provided by the user.

    CdiSignalType payload_callback_signal;          ///< Signal to indicate when a payload has been delivered.
    volatile bool payload_error;                    ///< true if Tx callback got a payload error.

    CdiSignalType connection_state_change_signal;   ///< Signal used for connection state changes.
    volatile CdiConnectionStatus connection_status; ///< Current status of the connection.

    void* adapter_tx_buffer_ptr;                    ///< Adapter's Tx buffer pointer.

    uint64_t payload_start_time;                    ///< Payload start time, used by Tx Callback functions.

    /// @brief Number of times payload callback function has been invoked. NOTE: This variable is used by multiple
    /// threads and not declared volatile. It is used as an example of how to use the CdiOsAtomic...() API functions.
    int payload_cb_count;

    CdiThreadID ndi_thread_id;                      ///< NDI thread ID.
    CdiReturnStatus ndi_thread_rs;                  ///< NDI thread return status.
    CdiSignalType ndi_thread_signal;                ///< NDI thread signal.

    CdiPoolHandle ndi_frame_data_pool_handle;       ///< Memory Pool Handle.
    CdiFifoHandle payload_fifo_handle;              ///< Payload FIFO Handle.
    CdiFifoHandle callback_fifo_handle;             ///< Callback FIFO Handle.

    NDIlib_recv_instance_t pNDI_recv;               ///< NDI receiver pointer.

    // Used by NDI transmitter.
    NDIlib_send_instance_t pNDI_send;               ///< NDI sender pointer.
    CdiPoolHandle ndi_audio_pool_handle;            ///< NDI audio pool handle.
    CdiPoolHandle ndi_video_pool_handle;            ///< NDI video pool handle.

    /// @brief Baseline AVM configuration[CdiBaselineAvmPayloadType]. Used to log changes to AVM.
    CdiAvmBaselineConfig last_baseline_config[kCdiAvmAncillary]; // kCdiAvmAncillary is last value in the enum.

    /// @brief Start time for the connection. For TX this is the time of the first payload sent. For RX it is the time
    /// from the PTP timestamp of the first payload received.
    CdiPtpTimestamp connection_start_time;
    uint64_t total_audio_samples; ///< Total number of audio samples processed.
    uint32_t total_video_frames; ///< Total number of video frames processed.

    double cdi_video_period_fraction_ns; ///< Video CDI period fractional portion in nS.
    double cdi_audio_period_fraction_ns; ///< Audio CDI period fractional portion in uS.
};

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Handle the connection callback.
 *
 * @param cb_data_ptr Pointer to core connection callback data.
 */
void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr);

/**
 * @brief If enabled using the command line option, log timestamps for every frame.
 *
 * @param con_info_ptr Pointer to test connection information.
 * @param frame_data_ptr Pointer to frame data.
 * @param cdi_timestamp_ptr Pointer to CDI timestamp to log.
 */
void LogTimestamps(TestConnectionInfo* con_info_ptr, const FrameData* frame_data_ptr,
                   CdiPtpTimestamp* cdi_timestamp_ptr);

#endif // NDI_TEST_H__
