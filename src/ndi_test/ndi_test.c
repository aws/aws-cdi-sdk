// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the NDI-CDI Converter application.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
/*
 * ------------------------------------------------------------------------------------------
 * Setup configuration file at: ~/.ndi/ndi-config.v1.json
 * ------------------------------------------------------------------------------------------
 Contents:
{
    "ndi": {
        "machinename": "<name>",
        # ----------------------------------
        # For static IPs, example:
        # ----------------------------------
        "networks": {
            "ips": "<ip address1>,<ip address2>"
        },
        # -----------------------------------
        # To use a discovery server, example:
        # -----------------------------------
        "networks": {
            "ips": "",
            "discovery":"<ip address1>,<ip address2>"
        },
        # ----------------------------------
        # Rest of file:
        # ----------------------------------
        "rudp": {
            "send": {
                "enable": true
            },
            "recv": {
                "enable": true
            }
        },
        "multicast": {
            "send": {
                "enable": false
            },
            "recv": {
                "enable": false
            }
        },
        "tcp": {
            "send": {
                "enable": false
            },
            "recv": {
                "enable": false
            }
        },
        "unicast": {
            "send": {
                 "enable": true
            },
            "recv": {
                "enable": true
            }
        }
    }
}

* ------------------------------------------------------------------------------------------
* NDI to CDI using 60fps.
*
* Note: For NDI 8-bit 4:2:2 1080p, payload size is 4147200 bytes (2073600 + 1036800 + 1036800).
*
* Can use any NDI source, but this test assumes 1080p@60.
*
* Workflow: NDI source -> NDI -> ndi_test -> CDI -> cdi_test.
* ------------------------------------------------------------------------------------------
1. Create NDI to CDI converter:

./build/debug/bin/ndi_test --local_ip <ip address> --ndi_rx --remote_ip <cdi_address> --dest_port 2000

2. Create CDI receiver:

./build/debug/bin/cdi_test --local_ip <ip address> --adapter EFA \
-X --rx AVM --dest_port 2000 --rate 60 --num_transactions 0 \
-S --id 1 --payload_size 4147200 --pattern IGNORE --keep_alive --avm_autorx \
-S --id 2 --payload_size 6144 --pattern IGNORE --keep_alive --avm_autorx

* ------------------------------------------------------------------------------------------
* CDI to NDI using 60fps. Note: The files used as source content can be downloaded from:
* https://cdi.elemental.com
*
* Workflow: cdi_test -> CDI -> ndi_test -> NDI -> NDI source
*
* Should be able to use NDI tools to view the new NDI source (assuming config/discover
* server is setup correctly).
* ------------------------------------------------------------------------------------------
1. Create CDI transmitter:

./build/debug/bin/cdi_test --adapter EFA --local_ip <ip address> \
-X --tx AVM --remote_ip <ip address> --dest_port 2000 --rate 60 --num_transactions 0 \`
-S --id 1 --payload_size 4147200 \
--avm_video 1920 1080 YCbCr422 Unused 8bit 60 1 BT709 false false SDR Narrow 1 1 0 0 0 0
--file_read Color_Bars_1080P_422_8bit.yuv \
-S --id 2 --payload_size 2400 --avm_audio "ST" 48KHz none \`
--file_read clock_ticking_24bit_48khz_stereo.pcm

2. Create CDI to NDI converter. Notes: "<name>" is the name of the NDI source to create.
   Bind IP address is required if multiple adapters exist.

./build/debug/bin/ndi_test --local_ip <ip_address> --bind_ip <ip_address> --dest_port 2000 --ndi_tx \
--ndi_source_name "<name>"
*/
#endif // DOXYGEN_SHOULD_SKIP_THIS

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
#include "cdi_pool_api.h"
#include "test_common.h"
#include "utilities_api.h"
#include "ndi_test.h"
#include "ndi_wrapper.h"
#include <Processing.NDI.Lib.h>

extern CdiReturnStatus NdiReceiverToCdiTransmitter(TestConnectionInfo* con_info_ptr); ///< External reference
extern CdiReturnStatus CdiReceiverToNdiTransmitter(TestConnectionInfo* con_info_ptr); ///< External reference

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Assume 4k, 8-bit video frame is maximum payload size.
#define TX_BUFFER_SIZE                      (3840 * 2160 * 3)

/// @brief Default memory pool size.
#define DEFAULT_FRAME_DATA_POOL_SIZE        (100)

/// @brief Default FrameData callback FIFO size.
#define DEFAULT_CALLBACK_FIFO_SIZE          (100)

/// @brief Default FrameData payload FIFO size.
#define DEFAULT_PAYLOAD_FIFO_SIZE           (100)

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Output command line help message.
 */
void PrintHelp(void) {
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Command line options:");
    TestConsoleLog(kLogInfo, "--show_ndi_sources                : Show NDI sources and then exit.\n");
    TestConsoleLog(kLogInfo, "--ndi_rx  or  --ndi_tx            : NDI receiver or transmitter. Only one option allowed. See\n"
                             "  sections below for Rx and Tx settings.");
    TestConsoleLog(kLogInfo, "\nRequired settings when using --ndi_rx:");
    TestConsoleLog(kLogInfo, "--local_ip         <ip address>   : (required) Set the IP address of the local CDI network adapter.\n");
    TestConsoleLog(kLogInfo, "--dest_port        <port num>     : Set the CDI destination port.");
    TestConsoleLog(kLogInfo, "--remote_ip        <ip address>   : Set the IP address of the remote CDI network adapter.");
    TestConsoleLog(kLogInfo, "\nOptional settings when using --ndi_rx:");
    TestConsoleLog(kLogInfo, "--tx_timeout       <microseconds> : Set the transmit timeout for a CDI payload in microseconds.");
    TestConsoleLog(kLogInfo, "--ndi_source_name  <MACHINENAME>  : Set the machine name of the desired NDI source. For\n"
                             "  example, to find the first source with machine name ABC use: --ndi_source_name \"ABC\"\n"
                             "  Optionally, specify the send source's program name along with the machine name. Surround the\n"
                             "  argument with double quotes. For example, with machine name ABC and program Test Pattern use:\n"
                             "  --ndi_source_name \"ABC (TestPattern)\"");
    TestConsoleLog(kLogInfo, "\n--ndi_source_ip    <ip address>   : Set the IP address of the desired NDI source. For example,\n"
                             "  with IP address 1.2.3.4 use: --ndi_source_ip 1.2.3.4\n"
                             "  Optionally, specify the port number to disable using the default. For example, with IP address\n"
                             "  1.2.3.4 and port 1000, use: --ndi_source_ip 1.2.3.4:1000");
    TestConsoleLog(kLogInfo, "\n--ndi_source_url   <url address>  : Set the URL address of the desired NDI source. Some\n"
                             "  examples:\n"
                             "  --ndi_source_url ndi://ABC/TestPattern\n"
                             "  --ndi_source_url http://1.2.3.4/TestPattern");
    TestConsoleLog(kLogInfo, "\nRequired settings when using --ndi_tx:");
    TestConsoleLog(kLogInfo, "--local_ip         <ip address>   : Set the IP address of the local CDI network adapter.\n");
    TestConsoleLog(kLogInfo, "--bind_ip          <ip address>   : The IP address of the local CDI network adapter");
    TestConsoleLog(kLogInfo, "                                  : to bind to. Must be specified if multiple adapters exist.");
    TestConsoleLog(kLogInfo, "--dest_port        <port num>     : Set the CDI destination port to listen to.");
    TestConsoleLog(kLogInfo, "--ndi_source_name  <MACHINENAME>  : See description above.");
    TestConsoleLog(kLogInfo, "\nOptional settings when using --ndi_tx:");
    TestConsoleLog(kLogInfo, "--video_stream_id  <id>           : Set the CDI video stream ID. Default is 1.");
    TestConsoleLog(kLogInfo, "--audio_stream_id  <id>           : Set the CDI audio stream ID. Default is 2.");
    TestConsoleLog(kLogInfo, "--anc_stream_id    <id>           : Set the CDI ancillary data stream ID. Default is 3.");
    TestConsoleLog(kLogInfo, "\nAdditional global options:");
    TestConsoleLog(kLogInfo, "--num_transactions <count>        : Limit the number of transactions for this test.");
    TestConsoleLog(kLogInfo, "--use_efa          <boolean>      : Use EFA or Unix sockets (default true).");
    TestConsoleLog(kLogInfo, "--use_ndi_timestamps              : Disable internal generation of outgoing CDI timestamps.");
    TestConsoleLog(kLogInfo, "--log_timestamps                  : Log timestamps (very verbose).");
}

/**
 * Parse command line and write to the specified TestSettings structure.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments.
 * @param test_settings_ptr Address where to write returned settings.
 *
 * @return true if successful, otherwise false.
 */
static bool ParseCommandLine(int argc, const char** argv, TestSettings* test_settings_ptr)
{
    bool ret = true;

    if (1 == argc) {
        PrintHelp();
        return false;
    }

    int i = 1;
    while (i < argc && ret) {
        const char* arg_str = argv[i++];
        if (0 == CdiOsStrCmp("--ndi_rx", arg_str)) {
            test_settings_ptr->ndi_rx = true;
        } else if (0 == CdiOsStrCmp("--show_ndi_sources", arg_str)) {
            test_settings_ptr->show_ndi_sources = true;
        } else if (0 == CdiOsStrCmp("--ndi_tx", arg_str)) {
            test_settings_ptr->ndi_tx = true;
        } else if (0 == CdiOsStrCmp("--ndi_source_name", arg_str)) {
            test_settings_ptr->ndi_source_name = argv[i++];
        } else if (0 == CdiOsStrCmp("--ndi_source_ip", arg_str)) {
            test_settings_ptr->ndi_source_ip = argv[i++];
        } else if (0 == CdiOsStrCmp("--ndi_source_url", arg_str)) {
            test_settings_ptr->ndi_source_url = argv[i++];
        } else if (0 == CdiOsStrCmp("--local_ip", arg_str)) {
            test_settings_ptr->local_adapter_ip_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--bind_ip", arg_str)) {
            test_settings_ptr->bind_ip_addr_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--dest_port", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->dest_port, NULL);
        } else if (0 == CdiOsStrCmp("--remote_ip", arg_str)) {
            test_settings_ptr->remote_adapter_ip_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--num_transactions", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->num_transactions, NULL);
        } else if (0 == CdiOsStrCmp("--video_stream_id", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->video_stream_id, NULL);
        } else if (0 == CdiOsStrCmp("--audio_stream_id", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->audio_stream_id, NULL);
        } else if (0 == CdiOsStrCmp("--anc_stream_id", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->anc_data_stream_id, NULL);
        } else if (0 == CdiOsStrCmp("--tx_timeout", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->tx_timeout, NULL);
        } else if (0 == CdiOsStrCmp("--use_efa", arg_str)) {
            test_settings_ptr->use_efa = (0 == CdiOsStrCmp("true", argv[i++]));
        } else if (0 == CdiOsStrCmp("--use_ndi_timestamps", arg_str)) {
            test_settings_ptr->use_ndi_timestamps = true;
        } else if (0 == CdiOsStrCmp("--log_timestamps", arg_str)) {
            test_settings_ptr->log_timestamps = true;
        } else if (0 == CdiOsStrCmp("--help", arg_str) || 0 == CdiOsStrCmp("-h", arg_str)) {
            ret = false;
            break;
        } else {
            CDI_LOG_THREAD(kLogError, "Unknown command line option[%s]", arg_str);
            ret = false;
            break;
        }
    }

    // Skip validation if only showing NDI sources.
    if (ret && test_settings_ptr->show_ndi_sources) {
        return true;
    }

    // Ensure settings are valid and required settings specified.
    if (ret) {
        if (test_settings_ptr->ndi_rx && test_settings_ptr->ndi_tx) {
            CDI_LOG_THREAD(kLogError, "Cannot use both --ndi_rx and --ndi_tx together.");
            ret = false;
        } else if (!test_settings_ptr->ndi_rx && !test_settings_ptr->ndi_tx) {
            test_settings_ptr->ndi_rx = true; // Set NDI Rx as default
        }
    }

    if (ret && test_settings_ptr->ndi_rx) {
        if (NULL == test_settings_ptr->local_adapter_ip_str || NULL == test_settings_ptr->remote_adapter_ip_str ||
            0 == test_settings_ptr->dest_port) {
            CDI_LOG_THREAD(kLogError, "For --ndi_rx, must specify --local_ip, --dest_port and --remote_ip.\n");
            ret = false;
        }
    }

    if (ret && test_settings_ptr->ndi_tx) {
        if (NULL == test_settings_ptr->local_adapter_ip_str || 0 == test_settings_ptr->dest_port) {
            CDI_LOG_THREAD(kLogError, "For --ndi_tx, must specify --local_ip and --dest_port.\n");
            ret = false;
        }
    }

    if (!ret) {
        PrintHelp();
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr)
{
    TestConnectionInfo* con_info_ptr = (TestConnectionInfo*)cb_data_ptr->connection_user_cb_param;

    // Update connection state and set state change signal.
    con_info_ptr->connection_status = cb_data_ptr->status_code;
    CdiOsSignalSet(con_info_ptr->connection_state_change_signal);

    if (kCdiConnectionStatusConnected != con_info_ptr->connection_status) {
        CDI_LOG_THREAD(kLogInfo, "Lost CDI connection. Port[%d].", con_info_ptr->test_settings.dest_port);
    } else {
        CDI_LOG_THREAD(kLogInfo, "CDI connected. Port[%d].", con_info_ptr->test_settings.dest_port);
    }
}

void LogTimestamps(TestConnectionInfo* con_info_ptr, const FrameData* frame_data_ptr,
                   CdiPtpTimestamp* cdi_timestamp_ptr)
{
    if (con_info_ptr->test_settings.log_timestamps) {
        if (frame_data_ptr->frame_type == kNdiVideo) {
            static int64_t last_ndi_ts = 0;
            static CdiPtpTimestamp last_cdi_ts = {0};
            if (last_ndi_ts) {
                uint64_t cdi_diff = (cdi_timestamp_ptr->seconds - last_cdi_ts.seconds) * CDI_NANOSECONDS_PER_SECOND;
                cdi_diff = cdi_diff + cdi_timestamp_ptr->nanoseconds - last_cdi_ts.nanoseconds;
                printf("Vid NDI diff[%lu] CDI diff[%lu]\n", frame_data_ptr->data.video_frame.timestamp - last_ndi_ts, cdi_diff);
            }
            last_ndi_ts = frame_data_ptr->data.video_frame.timestamp;
            last_cdi_ts = *cdi_timestamp_ptr;
        } else if (frame_data_ptr->frame_type == kNdiAudio) {
            static int64_t last_ndi_ts = 0;
            static CdiPtpTimestamp last_cdi_ts = {0};
            uint64_t cdi_diff = (cdi_timestamp_ptr->seconds - last_cdi_ts.seconds) * CDI_NANOSECONDS_PER_SECOND;
            cdi_diff = cdi_diff + cdi_timestamp_ptr->nanoseconds - last_cdi_ts.nanoseconds;
            printf("Aud NDI diff[%lu] CDI diff[%lu]\n", frame_data_ptr->data.audio_frame.timestamp - last_ndi_ts, cdi_diff);
            last_ndi_ts = frame_data_ptr->data.audio_frame.timestamp;
            last_cdi_ts = *cdi_timestamp_ptr;
        }
    }
}

/**
 * C main entry function.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments.
 *
 * @return 0 on success, otherwise 1 indicating a failure occurred.
 */
int main(int argc, const char** argv)
{
    CdiLoggerInitialize(); // Initialize logger so we can use the CDI_LOG_THREAD() macro to generate console messages.

    CdiReturnStatus rs = kCdiStatusOk;

    // Setup default test settings.
    TestConnectionInfo con_info = {0};
    con_info.test_settings.num_transactions = DEFAULT_NUM_TRANSACTIONS,
    con_info.test_settings.tx_timeout = DEFAULT_TX_TIMEOUT,
    con_info.test_settings.use_efa = true;
    con_info.test_settings.ndi_rx = false;
    con_info.test_settings.ndi_tx = false;
    con_info.test_settings.video_stream_id = 1; // Default CDI Tx stream IDs
    con_info.test_settings.audio_stream_id = 2;
    con_info.test_settings.anc_data_stream_id = 3;
    con_info.ndi_thread_rs = kCdiStatusOk;

    // Parse command line.
    CommandLineHandle command_line_handle = NULL;
    if (!TestCommandLineParserCreate(&argc, &argv, &command_line_handle) ||
        !ParseCommandLine(argc, argv, &con_info.test_settings)) {
        return 1;
    }

    CDI_LOG_THREAD(kLogInfo, "Initializing NDI SDK.");

    // Initialize NDI.
    if (!NdiInitialize()) {
        CDI_LOG_THREAD(kLogFatal, "NDI initialization failed.");
        return 1;
    }

    // Only showing NDI sources.
    if (con_info.test_settings.show_ndi_sources) {
        NdiShowSources();
        NDIlib_destroy();
        return 0;
    }

    // Create resources used by this application.
    CdiOsSignalCreate(&con_info.connection_state_change_signal);
    CdiOsSignalCreate(&con_info.payload_callback_signal);
    CdiOsSignalCreate(&con_info.ndi_thread_signal);

    if (kCdiStatusOk == rs) {
        // Create a frame data callback FIFO.
        if (!CdiFifoCreate("Frame Data Callback FIFO", // Name of the FIFO.
                            DEFAULT_CALLBACK_FIFO_SIZE, // Number of items in the FIFO (FIFO depth).
                            sizeof(FrameData*), // Size of each item in bytes.
                            NULL, // Address of callback function invoked when the FIFO is full and CdiFifoWrite()
                                  // is used. Specify NULL if not used.
                            NULL, // User defined parameter used in structure passed to full_cb_ptr.
                            &con_info.callback_fifo_handle)) // Pointer to returned handle of the new FIFO.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create Callback FIFO.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create a frame data FIFO.
        if (!CdiFifoCreate("Frame Data Payload FIFO", // Name of the FIFO.
                            DEFAULT_PAYLOAD_FIFO_SIZE, // Number of items in the FIFO (FIFO depth).
                            sizeof(FrameData*), // Size of each item in bytes.
                            NULL, // Address of callback function invoked when the FIFO is full and CdiFifoWrite()
                                  // is used. Specify NULL if not used.
                            NULL, // User defined parameter used in structure passed to full_cb_ptr.
                            &con_info.payload_fifo_handle)) // Pointer to returned handle of the new FIFO.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create Payload FIFO.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create a frame data memory pool.
        if (!CdiPoolCreate("Frame Data Pool", // Name of the pool.
                            DEFAULT_FRAME_DATA_POOL_SIZE, // Number of pool items.
                            0, // Grow count size (don't want to grow).
                            0, // Max grow count (don't want to grow).
                            sizeof(FrameData), // Payload buffer size.
                            true, // true = Make thread-safe.
                            &con_info.ndi_frame_data_pool_handle)) // Returned handle to the pool.
        {
            CDI_LOG_THREAD(kLogError, "Failed to create Frame Data Pool.");
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    // Initialize CDI core (must do before initializing adapter or creating connections).
    con_info.log_method_data.log_method = kLogMethodStdout;
    CdiCoreConfigData core_config = {
        .default_log_level = kLogDebug,
        .global_log_method_data_ptr = &con_info.log_method_data,
        .cloudwatch_config_ptr = NULL
    };

    rs = CdiCoreInitialize(&core_config);
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "SDK core initialize failed. Error=[%d], Message=[%s]", rs,
                       CdiCoreStatusToString(rs));
    }

    // Register the adapter.
    if (kCdiStatusOk == rs) {
        CdiAdapterData adapter_data = {
            .adapter_ip_addr_str = con_info.test_settings.local_adapter_ip_str,
            .tx_buffer_size_bytes = con_info.test_settings.ndi_rx ? TX_BUFFER_SIZE : 0,
            .ret_tx_buffer_ptr = NULL, // Initialize to NULL.
            .adapter_type = con_info.test_settings.use_efa ? kCdiAdapterTypeEfa : kCdiAdapterTypeSocketLibfabric
        };
        rs = CdiCoreNetworkAdapterInitialize(&adapter_data, &con_info.adapter_handle);

        // Get Tx buffer allocated by the Adapter.
        con_info.adapter_tx_buffer_ptr = adapter_data.ret_tx_buffer_ptr;
    }

    if (con_info.test_settings.ndi_rx) {
        rs = NdiReceiverToCdiTransmitter(&con_info);
    } else {
        rs = CdiReceiverToNdiTransmitter(&con_info);
    }

    // Not required, but nice.
    NDIlib_destroy();

    // Shutdown and clean-up CDI SDK resources.
    if (con_info.connection_handle) {
        CdiCoreConnectionDestroy(con_info.connection_handle);
    }
    if (con_info.adapter_handle) {
        CdiCoreNetworkAdapterDestroy(con_info.adapter_handle);
        con_info.adapter_handle = NULL;
    }
    CdiCoreShutdown();

    // Destroy NDI Pool.
    CdiPoolPutAll(con_info.ndi_frame_data_pool_handle);
    CdiPoolDestroy(con_info.ndi_frame_data_pool_handle);

    // Destroy Payload FIFO.
    CdiFifoFlush(con_info.payload_fifo_handle);
    CdiFifoDestroy(con_info.payload_fifo_handle);

    // Destroy Callback FIFO.
    CdiFifoFlush(con_info.callback_fifo_handle);
    CdiFifoDestroy(con_info.callback_fifo_handle);

    // Clean-up additional resources used by this application.
    CdiOsSignalDelete(con_info.ndi_thread_signal);
    CdiOsSignalDelete(con_info.payload_callback_signal);
    CdiOsSignalDelete(con_info.connection_state_change_signal);

    TestCommandLineParserDestroy(command_line_handle);
    CdiLoggerShutdown(false); // Matches call to CdiLoggerInitialize(). NOTE: false= Normal termination.

    return (kCdiStatusOk == rs) ? 0 : 1;
}
