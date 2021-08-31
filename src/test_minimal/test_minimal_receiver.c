// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the receive-side CDI minimal test application.
*/

#include <assert.h>
#include <stdbool.h>

#include "cdi_avm_api.h"
#include "cdi_core_api.h"
#include "cdi_raw_api.h"
#include "test_common.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure that holds all the test settings as set from the command line.
 */
typedef struct {
    const char* local_adapter_ip_str;  ///< The local network adapter IP address.
    int dest_port;                     ///< The destination port number.
    TestConnectionProtocolType protocol_type; ///< Protocol type (AVM or RAW).
    int num_transactions;              ///< The number of transactions in the test.
    int payload_size;                  ///< Payload size in bytes.
    bool use_efa;                      ///< Whether to use EFA adapter.
} TestSettings;

/**
 * @brief A structure for storing all info related to a specific connection, including test settings, connection
 * configuration data from the SDK, and state information for the test connection.
 */
typedef struct {
    CdiConnectionHandle connection_handle; ///< The connection handle returned by CdiRawTxCreate().

    TestSettings test_settings;            ///< Test settings data structure provided by the user.

    CdiSignalType payload_callback_signal; ///< Signal to indicate when a payload has been received.

    /// @brief Number of payloads successfully received. NOTE: This variable is used by multiple threads and not
    /// declared volatile. It is used as an example of how to use the CdiOsAtomic...() API functions.
    int payload_received_count;
    volatile bool payload_error;           ///< true if Rx callback got a payload error.

    CdiSignalType connection_state_change_signal;   ///< Signal used for connection state changes.
    volatile CdiConnectionStatus connection_status; ///< Current status of the connection.
} TestConnectionInfo;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Output command line help message.
 */
void PrintHelp(void) {
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "\nCommand line options:\n");
    TestConsoleLog(kLogInfo, "--rx               <protocol>     : Choose receiver mode AVM or RAW (default RAW). "
                   "AVM uses a HD 10-bit 4:2:2 static video frame.");
    TestConsoleLog(kLogInfo, "--local_ip         <ip address>   : (required) Set the IP address of the local network "
                   "adapter.");
    TestConsoleLog(kLogInfo, "--dest_port        <port num>     : (required) Set the destination port.");
    TestConsoleLog(kLogInfo, "--payload_size     <byte_size>    : Set the size (in bytes) for each payload.");
    TestConsoleLog(kLogInfo, "--num_transactions <count>        : Set the number of transactions for this test.");
    TestConsoleLog(kLogInfo, "--use_efa          <boolean>      : Whether to use EFA or Unix sockets (default true).");
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
        if (0 == CdiOsStrCmp("--rx", arg_str)) {
            if (0 == CdiOsStrCmp("AVM", argv[i])) {
                test_settings_ptr->protocol_type = kTestProtocolAvm;
            } else if (0 == CdiOsStrCmp("RAW", argv[i])) {
                test_settings_ptr->protocol_type = kTestProtocolRaw;
            } else {
                CDI_LOG_THREAD(kLogError, "For --rx <protocol>, expected 'AVM' or 'RAW'. Got[%s].", argv);
                ret = false;
            }
            i++;
        } else if (0 == CdiOsStrCmp("--local_ip", arg_str)) {
            test_settings_ptr->local_adapter_ip_str = argv[i++];
        } else if (0 == CdiOsStrCmp("--dest_port", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->dest_port, NULL);
        } else if (0 == CdiOsStrCmp("--num_transactions", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->num_transactions, NULL);
        } else if (0 == CdiOsStrCmp("--payload_size", arg_str)) {
            ret = TestStringToInt(argv[i++], &test_settings_ptr->payload_size, NULL);
        } else if (0 == CdiOsStrCmp("--use_efa", arg_str)) {
            test_settings_ptr->use_efa = (0 == CdiOsStrCmp("true", argv[i++]));
        } else if (0 == CdiOsStrCmp("--help", arg_str) || 0 == CdiOsStrCmp("-h", arg_str)) {
            ret = false;
            break;
        } else {
            CDI_LOG_THREAD(kLogError, "Unknown command line option[%s]\n", arg_str);
            ret = false;
            break;
        }
    }

    // Ensure that required settings were specified.
    if (ret && (NULL == test_settings_ptr->local_adapter_ip_str || 0 == test_settings_ptr->dest_port)) {
        CDI_LOG_THREAD(kLogError, "Must specify --local_ip and --dest_port.\n");
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
 * Handle the Rx AVM callback.
 *
 * @param cb_data_ptr Pointer to Tx RAW callback data.
 */
static void TestAvmRxCallback(const CdiAvmRxCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)cb_data_ptr->core_cb_data.user_cb_param;
    assert(kTestProtocolAvm == connection_info_ptr->test_settings.protocol_type);

    if (kCdiStatusOk != cb_data_ptr->core_cb_data.status_code) {
        CDI_LOG_THREAD(kLogError, "Receive payload failed[%s].",
                       CdiCoreStatusToString(cb_data_ptr->core_cb_data.status_code));
        connection_info_ptr->payload_error = true;
    } else {
        CdiOsAtomicInc32(&connection_info_ptr->payload_received_count);
    }

    CdiReturnStatus rs = CdiCoreRxFreeBuffer(&cb_data_ptr->sgl);
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "CdiCoreRxFreeBuffer failed[%s].", CdiCoreStatusToString(rs));
        connection_info_ptr->payload_error = true;
    }

    // Set the payload callback signal to wakeup the app.
    CdiOsSignalSet(connection_info_ptr->payload_callback_signal);
}

/**
 * Handle the Rx RAW callback.
 *
 * @param cb_data_ptr Pointer to Tx RAW callback data.
 */
static void TestRawRxCallback(const CdiRawRxCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = (TestConnectionInfo*)cb_data_ptr->core_cb_data.user_cb_param;
    assert(kTestProtocolRaw == connection_info_ptr->test_settings.protocol_type);

    if (kCdiStatusOk != cb_data_ptr->core_cb_data.status_code) {
        CDI_LOG_THREAD(kLogError, "Receive payload failed[%s].",
                       CdiCoreStatusToString(cb_data_ptr->core_cb_data.status_code));
        connection_info_ptr->payload_error = true;
    } else {
        CdiOsAtomicInc32(&connection_info_ptr->payload_received_count);
    }

    CdiReturnStatus rs = CdiCoreRxFreeBuffer(&cb_data_ptr->sgl);
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "CdiCoreRxFreeBuffer failed[%s].", CdiCoreStatusToString(rs));
        connection_info_ptr->payload_error = true;
    }

    // Set the payload callback signal to wakeup the app.
    CdiOsSignalSet(connection_info_ptr->payload_callback_signal);
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
    CdiLoggerInitialize(); // Initialize logger so we can use the CDI_LOG_THREAD() macro to generate console messages.

    // Setup default test settings.
    TestConnectionInfo con_info = {
        .test_settings.protocol_type = DEFAULT_PROTOCOL_TYPE,
        .test_settings.num_transactions = DEFAULT_NUM_TRANSACTIONS,
        .test_settings.payload_size = DEFAULT_PAYLOAD_SIZE
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
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "SDK core initialize failed. Error=[%d], Message=[%s]", rs,
                       CdiCoreStatusToString(rs));
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 2: Register the adapter.
    //-----------------------------------------------------------------------------------------------------------------
    CdiAdapterHandle adapter_handle = NULL;
    if (kCdiStatusOk == rs) {
        CdiAdapterData adapter_data = {
            .adapter_ip_addr_str = con_info.test_settings.local_adapter_ip_str,
            .adapter_type = con_info.test_settings.use_efa ? kCdiAdapterTypeEfa : kCdiAdapterTypeSocketLibfabric
        };
        rs = CdiCoreNetworkAdapterInitialize(&adapter_data, &adapter_handle);
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 3. Create a RAW Rx connection.
    //-----------------------------------------------------------------------------------------------------------------
    if (kCdiStatusOk == rs) {
        CdiRxConfigData config_data = {
            // Settings that are unique to an Rx connection.
            .rx_buffer_type = kCdiSgl,
            .linear_buffer_size = 0, // Not used for kCdiSgl type buffer.
            .user_cb_param = &con_info,

            // Settings that are common between Rx and Tx connections.
            .adapter_handle = adapter_handle,
            .dest_port = con_info.test_settings.dest_port,
            .shared_thread_id = 0, // 0 or -1= Use unique poll thread for this connection.
            .thread_core_num = -1, // -1= Let OS decide which CPU core to use.

            .connection_name_str = NULL,
            .connection_log_method_data_ptr = &log_method_data,

            .connection_cb_ptr = TestConnectionCallback, // Configure connection callback.
            .connection_user_cb_param = &con_info,

            .stats_cb_ptr = NULL, // Statistics gathering settings (not used here).
            .stats_user_cb_param = NULL,
            .stats_config.stats_period_seconds = 0,
            .stats_config.disable_cloudwatch_stats = true
        };
        if (kTestProtocolAvm == con_info.test_settings.protocol_type) {
            rs = CdiAvmRxCreate(&config_data, TestAvmRxCallback,  &con_info.connection_handle);
        } else {
            rs = CdiRawRxCreate(&config_data, TestRawRxCallback,  &con_info.connection_handle);
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
    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "Connected. Waiting to receive payloads...");
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 5: Loop until the desired number of payloads are received. If we get any errors or the connection
    // drops, then exit the loop.
    //-----------------------------------------------------------------------------------------------------------------
    int payload_count = 0;
    while (kCdiStatusOk == rs && payload_count < con_info.test_settings.num_transactions &&
           kCdiConnectionStatusConnected == con_info.connection_status) {
        // Wait for TestRawRxCallback() to be called or connection state change. The callback is invoked whenever a
        // payload has been received.
        CdiSignalType signal_array[2];
        signal_array[0] = con_info.connection_state_change_signal;
        signal_array[1] = con_info.payload_callback_signal;
        uint32_t signal_index = 0;
        CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, &signal_index);
        // Update local copy of counter that is incremented by the Rx callback. NOTE: To reduce code complexity,
        // synchronization logic has not been added here concerning the use of this counter and payload_callback_signal.
        // The Rx callback function may be invoked more than once before this logic is executed again.
        payload_count = CdiOsAtomicRead32(&con_info.payload_received_count);
        if (0 == signal_index) {
            CdiOsSignalClear(con_info.connection_state_change_signal);
            break; // Got connection state change signal. Must be disconnected, so exit the loop.
        } else {
            // Got payload callback signal.
            CdiOsSignalClear(con_info.payload_callback_signal);
            if (con_info.payload_error) {
                break; // Callback got a payload error, so exit the loop.
            }
        }

        // Update console with progress message.
        if (0 == (payload_count % PAYLOAD_PROGRESS_UPDATE_FREQUENCY)) {
            printf("\rReceived [%d] payloads.", payload_count);
            fflush(stdout);
        }
    }

    if (kCdiStatusOk == rs) {
        TestConsoleLog(kLogInfo, "");
        CDI_LOG_THREAD(kLogInfo, "All done. Received [%d] payloads. Shutting down.", payload_count);
    }

    //-----------------------------------------------------------------------------------------------------------------
    // CDI SDK Step 6. Shutdown and clean-up CDI SDK resources.
    //-----------------------------------------------------------------------------------------------------------------
    if (con_info.connection_handle) {
        CdiCoreConnectionDestroy(con_info.connection_handle);
    }
    if (adapter_handle) {
        CdiCoreNetworkAdapterDestroy(adapter_handle);
    }
    CdiCoreShutdown();

    // Clean-up additional resources used by this application.
    CdiOsSignalDelete(con_info.connection_state_change_signal);
    CdiOsSignalDelete(con_info.payload_callback_signal);
    TestCommandLineParserDestroy(command_line_handle);
    CdiLoggerShutdown(false); // Matches call to CdiLoggerInitialize(). NOTE: false= Normal termination.

    return (kCdiStatusOk == rs) ? 0 : 1;
}
