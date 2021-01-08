// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the transmit-side CDI minimal test application.
*/

#include <assert.h>

#include "cdi_baseline_profile_01_00_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_core_api.h"
#include "cdi_raw_api.h"
#include "test_common.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Default rate numerator.
#define DEFAULT_RATE_NUMERATOR              (60)

/// @brief Default rate denominator.
#define DEFAULT_RATE_DENOMINATOR            (1)

/// @brief Default Tx timeout.
#define DEFAULT_TX_TIMEOUT                  (16666)

/**
 * @brief A structure that holds all the test settings as set from the command line.
 */
typedef struct {
    const char* local_adapter_ip_str;  ///< The local network adapter IP address.
    int dest_port;                     ///< The destination port number.
    const char* remote_adapter_ip_str; ///< The remote network adapter IP address.
    TestConnectionProtocolType protocol_type; ///< Protocol type (AVM or RAW).
    int num_transactions;              ///< The number of transactions in the test.
    int payload_size;                  ///< Payload size in bytes.
    int rate_numerator;                ///< The numerator for the number of payloads per second to send.
    int rate_denominator;              ///< The denominator for the number of payloads per second to send.
    int tx_timeout;                    ///< The transmit timeout in microseconds for a Tx payload.
} TestSettings;

/**
 * @brief A structure for storing all info related to a specific connection, including test settings, connection
 * configuration data from the SDK, and state information for the test connection.
 */
typedef struct {
    CdiConnectionHandle connection_handle; ///< The connection handle returned by CdiRawTxCreate().

    TestSettings test_settings;            ///< Test settings data structure provided by the user.

    CdiSignalType payload_callback_signal; ///< Signal to indicate when a payload has been delivered.
    volatile bool payload_error;           ///< true if Tx callback got a payload error.

    CdiSignalType connection_state_change_signal;   ///< Signal used for connection state changes.
    volatile CdiConnectionStatus connection_status; ///< Current status of the connection.

    void* adapter_tx_buffer_ptr;           ///< Adapter's Tx buffer pointer.

    uint64_t payload_start_time;           ///< Payload start time, used by Tx Callback functions.
    int rate_period_microseconds;          ///< Calculated Tx rate period.

    /// @brief Number of times payload callback function has been invoked. NOTE: This variable is used by multiple
    /// threads and not declared volatile. It is used as an example of how to use the CdiOsAtomic...() API functions.
    int payload_cb_count;
} TestConnectionInfo;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Output command line help message.
 */
void PrintHelp(void) {
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Command line options:");
    TestConsoleLog(kLogInfo, "--tx               <protocol>     : Choose transmitter mode AVM or RAW (default RAW). "
                   "AVM uses a HD 10-bit 4:2:2 static video frame.");
    TestConsoleLog(kLogInfo, "--local_ip         <ip address>   : (required) Set the IP address of the local network "
                   "adapter.");
    TestConsoleLog(kLogInfo, "--dest_port        <port num>     : (required) Set the destination port.");
    TestConsoleLog(kLogInfo, "--remote_ip        <ip address>   : (required) The IP address of the remote network "
                   " adapter.");
    TestConsoleLog(kLogInfo, "--payload_size     <byte_size>    : Set the size (in bytes) for each payload.");
    TestConsoleLog(kLogInfo, "--num_transactions <count>        : Set the number of transactions for this test.");
    TestConsoleLog(kLogInfo, "--rate             <rate num/den> : Set the data rate as 'numerator/denominator' or "
                   "'numerator'.");
    TestConsoleLog(kLogInfo, "--tx_timeout       <microseconds> : Set the transmit timeout for a payload in "
                   "microseconds.");
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

    int i = 1;
    while (i < argc && ret) {
        const char* arg_str = argv[i++];
        if (0 == CdiOsStrCmp("--tx", arg_str)) {
            if (0 == CdiOsStrCmp("AVM", argv[i])) {
                test_settings_ptr->protocol_type = kTestProtocolAvm;
            } else if (0 == CdiOsStrCmp("RAW", argv[i])) {
                test_settings_ptr->protocol_type = kTestProtocolRaw;
            } else {
                CDI_LOG_THREAD(kLogError, "For --tx <protocol>, expected 'AVM' or 'RAW'. Got[%s].", argv);
                ret = false;
            }
            i++;
        } else if (0 == CdiOsStrCmp("--local_ip", arg_str)) {
            test_settings_ptr->local_adapter_ip_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--dest_port", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->dest_port, NULL);
        } else if (0 == CdiOsStrCmp("--remote_ip", arg_str)) {
            test_settings_ptr->remote_adapter_ip_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--num_transactions", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->num_transactions, NULL);
        } else if (0 == CdiOsStrCmp("--payload_size", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->payload_size, NULL);
        } else if (0 == CdiOsStrCmp("--rate", arg_str)) {
            char* end_str = NULL;
            ret = TestStringToInt(argv[i++], &test_settings_ptr->rate_numerator, &end_str);
            if (ret && *end_str == '/') {
                ret = TestStringToInt(end_str+1, &test_settings_ptr->rate_denominator, NULL);
            }
        } else if (0 == CdiOsStrCmp("--tx_timeout", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->tx_timeout, NULL);
        } else if (0 == CdiOsStrCmp("--help", arg_str) || 0 == CdiOsStrCmp("-h", arg_str)) {
            ret = false;
            break;
        } else {
            CDI_LOG_THREAD(kLogError, "Unknown command line option[%s]", arg_str);
            ret = false;
            break;
        }
    }

    // Ensure that required settings were specified.
    if (ret && (NULL == test_settings_ptr->local_adapter_ip_str || NULL == test_settings_ptr->remote_adapter_ip_str ||
        0 == test_settings_ptr->dest_port)) {
        CDI_LOG_THREAD(kLogError, "Must specify --local_ip, --dest_port and --remote_ip.\n");
        ret = false;
    }

    if (!ret) {
        PrintHelp();
    }

    return ret;
}

/**
 * Handle the connection callback.
 *
 * @param cb_data_ptr Pointer to CdiCoreConnectionCbData callback data.
 */
static void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->connection_user_cb_param;

    // Update connection state and set state change signal.
    connection_info_ptr->connection_status = cb_data_ptr->status_code;
    CdiOsSignalSet(connection_info_ptr->connection_state_change_signal);
}

/**
 * Process core Tx callback, that is common to both AVM and RAW Tx payload callbacks.
 *
 * @param connection_info_ptr Pointer to test connection state data.
 * @param core_cb_data_ptr  Pointer to core callback data.
 */
static void ProcessCoreTxCallback(TestConnectionInfo* connection_info_ptr, const CdiCoreCbData* core_cb_data_ptr)
{
    int count = CdiOsAtomicInc32(&connection_info_ptr->payload_cb_count);

    if (kCdiStatusOk != core_cb_data_ptr->status_code) {
        CDI_LOG_THREAD(kLogError, "Send payload failed[%s].",
                       CdiCoreStatusToString(core_cb_data_ptr->status_code));
        connection_info_ptr->payload_error = true;
    } else {
        uint64_t timeout_time = connection_info_ptr->payload_start_time + connection_info_ptr->test_settings.tx_timeout;
        uint64_t current_time = CdiOsGetMicroseconds();
        if (current_time > timeout_time) {
            CDI_LOG_THREAD(kLogError, "Payload [%d] late by [%llu]microseconds.", count, current_time - timeout_time);
            connection_info_ptr->payload_error = true;
        }
    }

    // Set next payload's expected start time.
    connection_info_ptr->payload_start_time += connection_info_ptr->rate_period_microseconds;

    // Set the payload callback signal to wakeup the app, if it was waiting.
    CdiOsSignalSet(connection_info_ptr->payload_callback_signal);
}

/**
 * Handle the Tx AVM callback. NOTE: Only used by the AVM API functions.
 *
 * @param   cb_data_ptr  Pointer to Tx AVM callback data.
 */
static void TestAvmTxCallback(const CdiAvmTxCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)cb_data_ptr->core_cb_data.user_cb_param;
    assert(kTestProtocolAvm == connection_info_ptr->test_settings.protocol_type);

    ProcessCoreTxCallback(connection_info_ptr, &cb_data_ptr->core_cb_data);
}

/**
 * Creates the generic configuration structure to use when sending AVM payloads.
 *
 * @param connection_info_ptr Pointer to a structure containing user settings needed for the configuration.
 * @param avm_config_ptr Address of where to write the generated generic configuration structure.
 * @param payload_unit_size_ptr Pointer to the location into which the payload unit size is to be written. This value
 *                              needs to be set in payload_config_ptr->core_config_data.unit_size for calls to
 *                              CdiAvmTxPayload().
 *
 * @return CdiReturnStatus kCdiStatusOk if the configuration structure was created successfully, kCdiStatusFatal if not.
 */
static CdiReturnStatus MakeAvmConfig(const TestConnectionInfo* connection_info_ptr, CdiAvmConfig* avm_config_ptr,
                                     int* payload_unit_size_ptr)
{
    CdiAvmBaselineConfig baseline_config = {
        .payload_type = kCdiAvmVideo,
        .video_config = {
            .version.major = 01, // Test using baseline profile V01.00.
            .version.minor = 00,
            .width = 1920,
            .height = 1080,
            .sampling = kCdiAvmVidYCbCr422,
            .alpha_channel = kCdiAvmAlphaUnused,
            .depth = kCdiAvmVidBitDepth10,
            .frame_rate_num = connection_info_ptr->test_settings.rate_numerator,
            .frame_rate_den = connection_info_ptr->test_settings.rate_denominator,
            .colorimetry = kCdiAvmVidColorimetryBT709,
            .interlace = false,
            .segmented = false,
            .tcs = kCdiAvmVidTcsSDR, // Standard Dynamic Range video stream.
            .range = kCdiAvmVidRangeFull,
            .par_width = 1,
            .par_height = 1,
            .start_vertical_pos = 0,
            .vertical_size = 0, // 0= Use full frame size.
            .start_horizontal_pos = 0,
            .horizontal_size = 0 // 0= Use full frame size.
        }
    };
    return CdiAvmMakeBaselineConfiguration(&baseline_config, avm_config_ptr, payload_unit_size_ptr);
}

/**
 * Send a payload using an AVM API function.
 *
 * @param connection_info_ptr Pointer to connection info structure.
 * @param sgl_ptr Pointer to SGL.
 * @param timestamp_ptr Pointer to timestamp.
 * @param avm_config_ptr Pointer to the generic configuration structure to use for the stream.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus SendAvmPayload(TestConnectionInfo* connection_info_ptr, CdiSgList* sgl_ptr,
                                      CdiPtpTimestamp* timestamp_ptr, CdiAvmConfig* avm_config_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiAvmTxPayloadConfig payload_config = {
        .core_config_data.core_extra_data.origination_ptp_timestamp = *timestamp_ptr,
        .core_config_data.core_extra_data.payload_user_data = 0,
        .core_config_data.user_cb_param = connection_info_ptr,
        .core_config_data.unit_size = 0
    };

    if (kCdiStatusOk == rs) {
        // Send the payload, retrying if the queue is full.
        do {
            rs = CdiAvmTxPayload(connection_info_ptr->connection_handle, &payload_config, avm_config_ptr, sgl_ptr,
                                 connection_info_ptr->test_settings.tx_timeout);
        } while (kCdiStatusQueueFull == rs);
    }

    return rs;
}

/**
 * Handle the Tx RAW callback. NOTE: Only used by the RAW API functions.
 *
 * @param cb_data_ptr Pointer to Tx RAW callback data.
 */
static void TestRawTxCallback(const CdiRawTxCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)cb_data_ptr->core_cb_data.user_cb_param;
    assert(kTestProtocolRaw == connection_info_ptr->test_settings.protocol_type);

    ProcessCoreTxCallback(connection_info_ptr, &cb_data_ptr->core_cb_data);
}

/**
 * Send a payload using a RAW API function.
 *
 * @param connection_info_ptr Pointer to connection info structure.
 * @param sgl_ptr Pointer to SGL.
 * @param timestamp_ptr Pointer to timestamp.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus SendRawPayload(TestConnectionInfo* connection_info_ptr, CdiSgList* sgl_ptr,
                                      CdiPtpTimestamp* timestamp_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiCoreTxPayloadConfig payload_config = {
        .core_extra_data.origination_ptp_timestamp = *timestamp_ptr,
        .core_extra_data.payload_user_data = 0,
        .user_cb_param = connection_info_ptr,
        .unit_size = 0
    };

    // Send the payload, retrying if the queue is full.
    do {
        rs = CdiRawTxPayload(connection_info_ptr->connection_handle, &payload_config, sgl_ptr,
                             connection_info_ptr->test_settings.tx_timeout);
    } while (kCdiStatusQueueFull == rs);

    return rs;
}

//*********************************************************************************************************************
//******************************************* START OF C MAIN FUNCTION ************************************************
//*********************************************************************************************************************

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
    CdiLoggerInitialize(); // Intialize logger so we can use the CDI_LOG_THREAD() macro to generate console messages.

    // Setup default test settings.
    TestConnectionInfo con_info = {
        .test_settings.protocol_type = DEFAULT_PROTOCOL_TYPE,
        .test_settings.num_transactions = DEFAULT_NUM_TRANSACTIONS,
        .test_settings.payload_size = DEFAULT_PAYLOAD_SIZE,
        .test_settings.rate_numerator = DEFAULT_RATE_NUMERATOR,
        .test_settings.rate_denominator = DEFAULT_RATE_DENOMINATOR,
        .test_settings.tx_timeout = DEFAULT_TX_TIMEOUT
    };

    // Parse command line.
    CommandLineHandle command_line_handle = NULL;
    if (!TestCommandLineParserCreate(&argc, &argv, &command_line_handle) ||
        !ParseCommandLine(argc, argv, &con_info.test_settings)) {
        return 1;
    }

    CDI_LOG_THREAD(kLogInfo, "Initializing test.");

    // Create resources used by this application.
    CdiOsSignalCreate(&con_info.payload_callback_signal);
    CdiOsSignalCreate(&con_info.connection_state_change_signal);

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 1: Initialize CDI core (must do before initializing adapter or creating connections).
    //-----------------------------------------------------------------------------------------------------------------
    CdiLogMethodData log_method_data = {
        .log_method = kLogMethodStdout
    };
    CdiCoreConfigData core_config = {
        .default_log_level = kLogDebug,
        .global_log_method_data_ptr = &log_method_data,
        .cloudwatch_config_ptr = NULL
    };
    CdiReturnStatus rs = CdiCoreInitialize(&core_config);

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 2: Register the EFA adapter.
    //-----------------------------------------------------------------------------------------------------------------
    CdiAdapterHandle adapter_handle = NULL;
    if (kCdiStatusOk == rs) {
        // Round-up buffer size to a multiple of HUGE_PAGES_BYTE_SIZE.
        int tx_buffer_size_bytes = ((con_info.test_settings.payload_size + HUGE_PAGES_BYTE_SIZE-1) /
                                   HUGE_PAGES_BYTE_SIZE) * HUGE_PAGES_BYTE_SIZE;
        CdiAdapterData adapter_data = {
            .adapter_ip_addr_str = con_info.test_settings.local_adapter_ip_str,
            .tx_buffer_size_bytes = tx_buffer_size_bytes,
            .adapter_type = kCdiAdapterTypeEfa // Use EFA adapter.
        };
        rs = CdiCoreNetworkAdapterInitialize(&adapter_data, &adapter_handle);

        // Get Tx buffer allocated by the Adapter.
        con_info.adapter_tx_buffer_ptr = adapter_data.ret_tx_buffer_ptr;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 3: Create a RAW Tx connection.
    //-----------------------------------------------------------------------------------------------------------------
    if (kCdiStatusOk == rs) {
        CdiTxConfigData config_data = {
            // Settings that are unique to a Tx connection.
            .dest_ip_addr_str = con_info.test_settings.remote_adapter_ip_str,

            // Settings that are common between Tx and Rx connections.
            .adapter_handle = adapter_handle,
            .dest_port = con_info.test_settings.dest_port,
            .thread_core_num = -1, // -1= Let OS decide which CPU core to use.
            .connection_name_str = NULL,
            .connection_log_method_data_ptr = &log_method_data,

            .connection_cb_ptr = TestConnectionCallback, // Configure connection callback.
            .connection_user_cb_param = &con_info,

            .stats_cb_ptr = NULL, // Statistics gathering settings (not used here).
            .stats_user_cb_param = NULL,
            .stats_config.stats_period_seconds = 0,
            .stats_config.disable_cloudwatch_stats = true,
        };
        if (kTestProtocolAvm == con_info.test_settings.protocol_type) {
            rs = CdiAvmTxCreate(&config_data, TestAvmTxCallback,  &con_info.connection_handle);
        } else {
            rs = CdiRawTxCreate(&config_data, TestRawTxCallback,  &con_info.connection_handle);
        }
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 4: Wait for connection to be established with remote target.
    //-----------------------------------------------------------------------------------------------------------------
    while (kCdiStatusOk == rs && kCdiConnectionStatusDisconnected == con_info.connection_status) {
        CDI_LOG_THREAD(kLogInfo, "Waiting to establish connection with remote target...");
        CdiOsSignalWait(con_info.connection_state_change_signal, CDI_INFINITE, NULL);
        CdiOsSignalClear(con_info.connection_state_change_signal);
    }
    CDI_LOG_THREAD(kLogInfo, "Connected. Sending payloads...");

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 5: Can now send the desired number of payloads. Will send at the specified rate. If we get any
    // errors or the connection drops, then exit the loop.
    //-----------------------------------------------------------------------------------------------------------------
    int payload_count = 0;
    CdiAvmConfig avm_config = { 0 };
    int payload_unit_size = 0;

    // Fill Tx payload buffer with a simple pattern.
    if (kTestProtocolAvm == con_info.test_settings.protocol_type) {
        const uint8_t pattern_array[5] = { 0x80, 0x04, 0x08, 0x00, 0x40 }; // Black for 10-bit 4:2:2.
        for (int i = 0; i < con_info.test_settings.payload_size; i+= sizeof(pattern_array)) {
            memcpy(((uint8_t*)con_info.adapter_tx_buffer_ptr)+i, pattern_array, sizeof(pattern_array));
        }

        // Fill in the AVM configuration structure and payload unit size.
        MakeAvmConfig(&con_info, &avm_config, &payload_unit_size);
    } else {
        memset(con_info.adapter_tx_buffer_ptr, 0x7f, con_info.test_settings.payload_size);
    }

    // Setup rate period and start times.
    con_info.rate_period_microseconds = ((1000000 * con_info.test_settings.rate_denominator) /
                                        con_info.test_settings.rate_numerator);
    con_info.payload_start_time = CdiOsGetMicroseconds();
    uint64_t rate_next_start_time = con_info.payload_start_time + con_info.rate_period_microseconds;

    while (kCdiStatusOk == rs && payload_count < con_info.test_settings.num_transactions &&
           kCdiConnectionStatusConnected == con_info.connection_status && !con_info.payload_error) {
        // Setup Scatter-gather-list entry for the payload data to send. NOTE: The buffers the SGL entries point to must
        // persist until the payload callback has been made. Since we are reusing the same buffer for each payload, we
        // don't need any additional logic here.
        CdiSglEntry sgl_entry = {
            .address_ptr = con_info.adapter_tx_buffer_ptr,
            .size_in_bytes = con_info.test_settings.payload_size,
        };
        CdiSgList sgl = {
            .total_data_size = con_info.test_settings.payload_size,
            .sgl_head_ptr = &sgl_entry,
            .sgl_tail_ptr = &sgl_entry,
            .internal_data_ptr = NULL,
        };

        // Create a PTP timestamp to send along with the payload. CDI doesn't use it, but just here as an example.
        struct timespec start_time;
        CdiCoreGetUtcTime(&start_time);
        CdiPtpTimestamp timestamp = {
            .seconds = (uint32_t)start_time.tv_sec,
            .nanoseconds = start_time.tv_nsec
        };

        // Send the payload.
        if (kTestProtocolAvm == con_info.test_settings.protocol_type) {
            rs = SendAvmPayload(&con_info, &sgl, &timestamp, &avm_config);
        } else {
            rs = SendRawPayload(&con_info, &sgl, &timestamp);
        }

       // Update console with progress message.
       if (0 == (++payload_count % PAYLOAD_PROGRESS_UPDATE_FREQUENCY)) {
            printf("\rSent [%d] payloads.", payload_count);
            fflush(stdout);
        }

        // If we're over the timing budget, then generate an error.
        uint64_t current_time = CdiOsGetMicroseconds();
        if (rate_next_start_time < current_time) {
            CDI_LOG_THREAD(kLogError, "Payload Tx late.");
            con_info.payload_error = true;
        } else {
            // To stay on our rate-time cadence, delay this thread for the desired amount of time. NOTE: For accuracy,
            // use a CdiOsSleep(0) here within a while loop so we don't depend on OS timer tick limitations. Some
            // Windows systems have a 15 millisecond minimum tick resolution.
            // Desired logic: CdiOsSleepMicroseconds(rate_next_start_time - current_time);
            while (CdiOsGetMicroseconds() < rate_next_start_time) {
                CdiOsSleep(0);
            }
        }
        rate_next_start_time += con_info.rate_period_microseconds;
    }

    CDI_LOG_THREAD(kLogInfo, "");
    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "Waiting for any pending Tx callbacks...");
        // Make sure this signal is clear before we check payload_cb_count and wait for it.
        CdiOsSignalClear(con_info.payload_callback_signal);

        while (!con_info.payload_error && kCdiConnectionStatusConnected == con_info.connection_status &&
               CdiOsAtomicRead32(&con_info.payload_cb_count) < payload_count) {
            // Setup to wait on the connection state change and the payload Tx callback signals.
            CdiSignalType signal_array[2];
            signal_array[0] = con_info.payload_callback_signal;
            signal_array[1] = con_info.connection_state_change_signal;
            uint32_t signal_index = 0;
            CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, &signal_index);
            if (0 == signal_index) {
                // Got payload callback signal, so clear it incase we need to wait for it again.
                CdiOsSignalClear(con_info.payload_callback_signal);
            } else {
                // Got connection state change signal. The while loop will exit if we got disconnected.
                CdiOsSignalClear(con_info.connection_state_change_signal);
            }
        }
    }

    CDI_LOG_THREAD(kLogInfo, "All done. Sent [%d] payloads. Shutting down.", payload_count);

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 6. Shutdown and clean-up CDI SDK resources.
    //-----------------------------------------------------------------------------------------------------------------
    if (con_info.connection_handle) {
        CdiCoreConnectionDestroy(con_info.connection_handle);
    }
    CdiCoreShutdown();

    // Clean-up additional resources used by this application.
    CdiOsSignalDelete(con_info.connection_state_change_signal);
    CdiOsSignalDelete(con_info.payload_callback_signal);
    TestCommandLineParserDestroy(command_line_handle);

    return (kCdiStatusOk == rs) ? 0 : 1;
}
