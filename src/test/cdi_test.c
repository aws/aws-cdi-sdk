// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * A program for testing the Cloud Digital Interface (CDI), also known as CDI SDK. This file contains the definitions
 * of the functions used for the cdi_test program.
 *
 * This test program allows users to test every aspect of the API and provides reference code for those starting out
 * with SDK integration.
 *
 * Command line options allow users to configure tests that verify sending user-specified blocks of data over multiple
 * payloads at various frame rates from one EC2 instance to another within AWS. Test options can specify the type of
 * pattern to use for the payload, the data type to send (Raw, AVM) as well as any configuration data needed by the
 * chosen data type.  Other options control test flow, such as logging, timeouts, distribution of payload transmission,
 * and CPU core assignments.
 *
 * Users can enable numerous unique connections simultaneously using the --new_conn (-X) option to delineate between
 * command line options for one connections and command line options for another connection.
 *
 * Each connection can be run as a receive (Rx) instance or transmit (Tx) instance.
 *
 * Additionally, the SDK provides three adapter types for testing, although only the EFA adapter type is available for
 * production applications. The socket adapter types can be used for development and debug. Command line options allow
 * the test to be run in any of the adapter modes.
 *
 * See @ref cdi_test for block diagrams and a discussion of program flow.
 *
 * @page cdi_test CDI Test
 * @tableofcontents
 *
 * @section cdi_test_all CDI Test Application Architecture Top Level
 *
 * The diagram shown below provides a top-level block diagram of the CDI Test application. Each connection is either a
 * transmitter (Tx) or a receiver (Tx). Each connection is started independently on its own thread.
 * @image html "test_block_diagram_top.jpg"
 *
 * @section cdi_test_tx CDI Test Application Architecture Transmit Logic
 *
 * The diagram shown below provides a block diagram of the CDI Test transmit logic application.
 * @image html "test_block_diagram_tx.jpg"
 *
 * The transmit logic follows a decision-making flow as described in this diagram.
 * @image html "test_transmitter_flow_diagram.jpg"
 *
 * @section cdi_test_rx CDI Test Application Architecture Receive Logic
 *
 * The diagram shown below provides a block diagram of the CDI Test application receive logic.
 * @image html "test_block_diagram_rx.jpg"
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

/// @brief Linux definition.
#define __USE_XOPEN2K


/// @brief _GNU_SOURCE Linux required for pthread_setname_np().
#define _GNU_SOURCE

#include "cdi_test.h"

#include <assert.h>
#include <signal.h> // Defines above are required when using this include file.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_test.h"
#include "curses.h"
#include "optarg.h"
#include "run_test.h"
#include "test_common.h"
#include "test_console.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/// @brief The maximum length of a command line.
#define MAX_COMMAND_LINE_STRING_LENGTH (10000)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Handle to log for test.
CdiLoggerHandle test_app_logger_handle = NULL;

/// An array of TestSettings objects used to store settings from the command line for each requested connection.
static TestSettings test_settings[CDI_MAX_SIMULTANEOUS_CONNECTIONS] = {{ 0 }};

/// A global structure used to hold the information global to all test settings.
static GlobalTestSettings global_test_settings = { 0 };

static CdiCsID signal_handler_lock = NULL; ///< Critical section to lock access to the signal handler.

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize global test settings.
 */
static void InitializeGlobalTestSettings(void)
{
    global_test_settings.log_level = DEFAULT_LOG_LEVEL;
    global_test_settings.num_loops = DEFAULT_NUM_LOOPS;
    global_test_settings.use_single_connection_log_file = true;
    global_test_settings.base_log_method = kLogMethodStdout;
    global_test_settings.base_log_filename_str[0] = '\0';
    global_test_settings.sdk_log_filename_str[0] = '\0';
    global_test_settings.test_app_global_log_handle = NULL;
    global_test_settings.total_num_connections = 0;
    global_test_settings.connection_info_array = NULL;
    global_test_settings.num_connections_established = 0;
    global_test_settings.all_connected_signal = NULL;
    global_test_settings.log_timestamps = false;

    for (int i = 0; i < kLogComponentLast; i++) {
        global_test_settings.log_component[i] = i == 0 ? kLogComponentGeneric : CDI_INVALID_ENUM_VALUE;
    }

    // Initialize adapter data.
    global_test_settings.adapter_data.adapter_type = CDI_INVALID_ENUM_VALUE;
    global_test_settings.adapter_data.adapter_ip_addr_str = NULL;
    memset(test_settings, 0, sizeof(test_settings));
}

/**
 * Signal handler for catching segfaults and other signals that cause abnormal program termination.
 *
 * @param signal_number The number of the signal that was raised.
 * @param siginfo_ptr A pointer to the siginfo_t structure. Valid only in Linux.
 * @param context_ptr A pointer to the context used in sigaction. Valid only in Linux.
 */
static void SignalHandler(int signal_number, siginfo_t* siginfo_ptr, void* context_ptr)
{
    (void)context_ptr;
    static bool already_used = false;

    CdiOsCritSectionReserve(signal_handler_lock);
    if (already_used) {
        CdiOsCritSectionRelease(signal_handler_lock);
        return; // Only want to run the cleanup logic below once.
    }
    already_used = true;
    CdiOsCritSectionRelease(signal_handler_lock);

    // Clean up the console (in case multi-window mode).
    TestConsoleDestroy(true); // true= Is abnormal termination.

    // If OS is Windows, siginfo will be NULL as it is not supported on the Windows OS.
    if (NULL == siginfo_ptr) {
        printf("Got signal[%d]. The CDI Test application is performing minimal cleanup.\n", signal_number);
    } else {
        printf("Got signal[%d] from sending PID[%ld] and UID[%ld].\nThe CDI Test application is performing minimal "
                "cleanup.\n", signal_number, (long)siginfo_ptr->si_pid, (long)siginfo_ptr->si_uid);
    }

    // Attempt to close all log files (flushing them out).
    CdiLoggerShutdown(true); // true= Abnormal termination.

    // Set default handler. In some cases, the default handler will generate a core-dump (which is desired).
    signal(signal_number, NULL);

    // If we got a Ctrl+C, just pass it along to the default handler in order to properly exit. Otherwise, other threads
    // may not immediately stop running and cause additional faults.
    if (SIGINT == signal_number) {
        raise(SIGINT);
    }
}

#ifdef _WIN32
/**
 * Windows console control hander.
 *
 * @param code Control code.
 */
static bool Win32CtrlHandler(uint32_t code)
{
    // Handle the CTRL-Break signal (or Ctrl+C if it ever gets enabled).
    if (CTRL_BREAK_EVENT == code || CTRL_C_EVENT == code) {
        SignalHandler(SIGINT, NULL, NULL);
        return true;
    }
    return false;
}
#endif

/**
 * Setup signal handlers so we can catch segfaults, aborts (asserts) and Ctrl+C interrupts. This allows our application
 * to perform some minimal cleanup (ie. flush log files) before exiting.
 *
 * @return Returns true if successful, otherwise false is returned.
 */
static bool SetupSignalHandlers(void) {
    if (!CdiOsCritSectionCreate(&signal_handler_lock)) {
        return false;
    }

    CdiOsSignalHandlerSet(SIGSEGV, SignalHandler); // Handle segfaults
    CdiOsSignalHandlerSet(SIGABRT, SignalHandler); // Handle asserts
    CdiOsSignalHandlerSet(SIGILL, SignalHandler);  // Handle illegal instruction
    CdiOsSignalHandlerSet(SIGFPE, SignalHandler);  // Handle floating point error
#ifdef _WIN32
    // NOTE: Ctrl+C in windows is not passed to the application, so must use Ctrl+Break instead to invoke our
    // handler.
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)Win32CtrlHandler, true)) {
        assert(0);
    }
#else
    CdiOsSignalHandlerSet(SIGINT, SignalHandler);  // Handle Ctrl+C
#endif
    return true;
}

/**
 * @brief Free logging resources used by this test application.
 *
 */
static void FreeAppLoggerResources(void)
{
    TestConsoleDestroy(false); // false= Normal termination.
    CdiLoggerThreadLogUnset();
    CdiLoggerDestroyLog(global_test_settings.test_app_global_log_handle);
    global_test_settings.test_app_global_log_handle = NULL;
    CdiLoggerDestroyLogger(test_app_logger_handle);
    test_app_logger_handle = NULL;
}

/**
 * Concatenate an array of strings into a single string buffer with a user-supplied separator between each element.
 *
 * @param   array_of_strings_ptr  Pointer to the input array of strings.
 * @param   num_entries           The number of strings in the array.
 * @param   separator_str         The separator string, such as " " or ", ".
 * @param   concat_str            Pointer to the concatenation string buffer.
 * @param   concat_max_len        The maximum size of the write buffer for the concatenation string.
 * @return                        True if string is fully written; false if an error (such as buffer overrun) occurs.
 */
static bool CreateStringFromArray(const char* array_of_strings_ptr[], int num_entries, const char* separator_str,
                           char* concat_str, int concat_max_len)
{
    int msg_index = 0;
    bool ret = true;
    for (int j = 0; j < num_entries; j++) {

        const int buffer_space_left = concat_max_len - msg_index;
        int buffer_space_requested;

        // We don't print the separator after the last array element.
        const char* const string_ptr = array_of_strings_ptr[j];
        if (j == (num_entries - 1)) {
            buffer_space_requested = snprintf(&concat_str[msg_index], buffer_space_left, "%s", string_ptr);
        } else {
            buffer_space_requested = snprintf(&concat_str[msg_index], buffer_space_left, "%s%s", string_ptr,
                                              separator_str);
        }

        // Be careful that we won't overrun the buffer.
        // Buffer space requested does not include null terminator, but buffer_pace_left does.
        if (buffer_space_requested >= buffer_space_left) {
            ret = false;
        } else {
            msg_index += buffer_space_requested;
        }
    }
    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

GlobalTestSettings* GetGlobalTestSettings(void) {
    return &global_test_settings;
}

/**
 * C main entry function.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments
 * @return Return 0 on success, otherwise 1 indicating a failure occurred.
 */
int main(int argc, const char **argv)
{
    ProgramExecutionStatus status = kProgramExecutionStatusContinue;

    // Setup signal handlers so we can catch segfaults and do some minimal cleanup.
    if (!SetupSignalHandlers()) {
        TestConsoleLog(kLogError, "Failed to setup signal handlers.");
        return 1;
    }

    int num_connections_found = 0;
    InitializeGlobalTestSettings();

    // Need to init the logger before parsing command lines to get console output.
    if (!CdiLoggerInitialize()) {
        status = kProgramExecutionStatusExitError;
    }

    // Get, parse, validate, and conform command line arguments into the test_settings data structure. Each
    // test_settings structure represents either a tx or rx connection. Takes in command-line arguments, sanitizes
    // them for syntax and correctness, and then assigns them to the test_settings data structure.
    CommandLineHandle command_line_handle = NULL;
    if (kProgramExecutionStatusContinue == status) {
        if (TestCommandLineParserCreate(&argc, &argv, &command_line_handle)) {
            status = GetArgs(argc, argv, test_settings, &num_connections_found);
        } else {
            status = kProgramExecutionStatusExitError;
        }
    }

    const CdiCoreReadOnlySettings* settings_ptr = CdiCoreGetSettings();
    TestConsoleLog(kLogInfo, "Read-only Settings:");
    TestConsoleLog(kLogInfo, "   tx_retry_timeout_ms : %llu", settings_ptr->tx_retry_timeout_ms);
    TestConsoleLog(kLogInfo, "   rx_wait_timeout_ms  : %llu", settings_ptr->rx_wait_timeout_ms);

    // Loop through the test. If the --num_loops is not used in the command-line, it will default to run the test once.
    for (int loop_num = 0;
         kProgramExecutionStatusContinue == status && (global_test_settings.num_loops > loop_num ||
                                                       global_test_settings.num_loops == RUN_FOREVER_VALUE);
         loop_num++) {
        bool got_error = false;

        // If specified, enable error output to stderr in addition to log files (if log files are enabled).
        CdiLogStderrEnable(global_test_settings.use_stderr, kLogError); // Use kLogError as the log level.

        // Create an instance of the logger, used by this test application.
        if (!CdiLoggerCreate(global_test_settings.log_level, &test_app_logger_handle)) {
            status = kProgramExecutionStatusExitError;
            break;
        }

        // Set all logging components and their logging levels if activated.
        for (int i=0; i < kLogComponentLast; i++) {
            if ((int)global_test_settings.log_component[i] == CDI_INVALID_ENUM_VALUE) {
                break;
            }
            CdiLoggerComponentEnableGlobal(global_test_settings.log_component[i], true);
            CdiLoggerLevelSetGlobal(global_test_settings.log_component[i], global_test_settings.log_level);
        }

        TestConsoleCreate(global_test_settings.use_multiwindow_console,
                          STATS_WINDOW_STATIC_HEIGHT + num_connections_found);

        // Set up the main log file for the test app, but only if a log file has been set by the user.
        if (global_test_settings.base_log_filename_str[0]) {
            // Open a log file for the use of the test application.
            TestConsoleLog(kLogInfo, "Setting log file[%s] for global test application logging.",
                           global_test_settings.base_log_filename_str);
            if (!CdiLoggerCreateFileLog(test_app_logger_handle, global_test_settings.base_log_filename_str,
                                        &global_test_settings.test_app_global_log_handle)) {
                TestConsoleLog(kLogError, "Unable to open log file[%s] for writing.",
                               global_test_settings.base_log_filename_str);
                status = kProgramExecutionStatusExitError;
                break;
            }
        } else {
            CdiLogMethodData log_method_data = {0};
            if (global_test_settings.use_multiwindow_console) {
                // Using the multi-window console, so create a callback log.
                log_method_data.log_method = kLogMethodCallback;
                log_method_data.callback_data.log_msg_cb_ptr = TestConsoleLogMessageCallback;
                log_method_data.callback_data.log_user_cb_param = NULL;
            } else {
                // Using normal stdout.
                log_method_data.log_method = kLogMethodStdout;
            }
            if (!CdiLoggerCreateLog(test_app_logger_handle, NULL, &log_method_data,
                                    &global_test_settings.test_app_global_log_handle)) {
                TestConsoleLog(kLogError, "Unable to open test app log for writing.");
                status = kProgramExecutionStatusExitError;
                break;
            }
        }

        // Set this thread to use our application log. Now, can use CDI_LOG_THREAD() macros to log to it.
        CdiLoggerThreadLogSet(global_test_settings.test_app_global_log_handle);

        // Set up the log file for the SDK.
        CdiLogMethodData sdk_log_method_data = {0};
        if (global_test_settings.base_log_filename_str[0]) {
            // Create a filename for the SDK global logger.
            char filename[CDI_MAX_LOG_FILENAME_LENGTH] = {0};
            char directory[CDI_MAX_LOG_FILENAME_LENGTH] = {0};
            if (!CdiOsSplitPath(global_test_settings.base_log_filename_str, filename, CDI_MAX_LOG_FILENAME_LENGTH,
                                directory, CDI_MAX_LOG_FILENAME_LENGTH)) {
                CDI_LOG_THREAD(kLogError, "CdiOsSplitPath failed, filename or directory buffers are too small.");
            }
            if (snprintf(global_test_settings.sdk_log_filename_str, sizeof(global_test_settings.sdk_log_filename_str),
                         "%sSDK_%s", directory, filename) >= (int)sizeof(global_test_settings.sdk_log_filename_str)) {
                TestConsoleLog(kLogError, "Path to log file name is too long.");
                status = kProgramExecutionStatusExitError;
                break;
            } else {
                TestConsoleLog(kLogInfo, "Setting log file[%s] for global SDK logging.",
                               global_test_settings.sdk_log_filename_str);
                sdk_log_method_data.log_method = kLogMethodFile;
                sdk_log_method_data.log_filename_str = global_test_settings.sdk_log_filename_str;
            }
        } else if (global_test_settings.use_multiwindow_console) {
                // Using the multi-window console, so create a callback log.
                sdk_log_method_data.log_method = kLogMethodCallback;
                sdk_log_method_data.callback_data.log_msg_cb_ptr = TestConsoleLogMessageCallback;
                sdk_log_method_data.callback_data.log_user_cb_param = NULL;
        } else {
            // Using normal stdout.
            sdk_log_method_data.log_method = kLogMethodStdout;
        }

        // Get a time string to add to the log.
        char time_str[CDI_MAX_FORMATTED_TIMEZONE_STRING_LENGTH];
        CdiOsGetLocalTimeString(time_str, CDI_MAX_FORMATTED_TIMEZONE_STRING_LENGTH);
        CDI_LOG_THREAD(kLogInfo, "-- Running CDI Test App -- %s", time_str);

        // Print the command line to the log.
        char command_line_str[MAX_COMMAND_LINE_STRING_LENGTH];
        if (!CreateStringFromArray(argv, argc, " ", command_line_str, MAX_COMMAND_LINE_STRING_LENGTH)) {
            CDI_LOG_THREAD(kLogError, "Command line string too long.");
        } else {
            CDI_LOG_THREAD(kLogInfo, "Command line: %s", command_line_str);
        }

        // Call the initialize function so we can start creating connections.
        CdiCoreConfigData core_config = {
            .default_log_level = global_test_settings.log_level,
            .global_log_method_data_ptr = &sdk_log_method_data,
            .cloudwatch_config_ptr = NULL,
        };

        GlobalTestSettings* global_test_settings_ptr = GetGlobalTestSettings();
        if (global_test_settings_ptr->use_cloudwatch) {
            core_config.cloudwatch_config_ptr = &global_test_settings_ptr->cloudwatch_config;
        }

        CdiReturnStatus rs = CdiCoreInitialize(&core_config);
        if (kCdiStatusOk != rs) {
            CDI_LOG_THREAD(kLogError, "SDK core initialize failed. Error=[%d], Message=[%s]", rs,
                           CdiCoreStatusToString(rs));
            status = kProgramExecutionStatusExitError;
            break;
        }

        // Print the test_settings data structure for each connection.
        PrintTestSettings(test_settings, num_connections_found);

        // Run the test!  Note that we allocate the number of connections specified on the command line.
        if (!got_error) {
            got_error = !RunTestGeneric(test_settings, CDI_MAX_SIMULTANEOUS_CONNECTIONS, num_connections_found);
        }

        // Check for pass/fail.
        if (!got_error) {
            CDI_LOG_THREAD(kLogInfo, "** Tests PASSED **");
            if (kLogMethodStdout != global_test_settings.base_log_method) {
                TestConsoleLog(kLogInfo, "** Tests PASSED **");
            }
        } else {
            CDI_LOG_THREAD(kLogInfo, "** Tests FAILED **");
            if (kLogMethodStdout != global_test_settings.base_log_method) {
                TestConsoleLog(kLogInfo, "** Tests FAILED **");
            }
            status = kProgramExecutionStatusExitError;
        }

        TestConsoleLog(kLogInfo, "Finishing test[%d].", loop_num+1);

        FreeAppLoggerResources(); // Close test application's log files and resources.

        // Reset the logger name.
        global_test_settings.base_log_filename_str[0] = '\0';

        // Shut down the application and free all resources. We do this after all the logger resources created by this test
        // app have been freed, otherwise the SDK will generate an internal error about logger resources not being freed.
        CdiCoreShutdown();

        // If we are looping the tests, specify a delay in between tests. A loop value of 0 indicates run forever.
        if (1 != global_test_settings.num_loops) {
            CdiOsSleep(MAIN_TEST_LOOP_WAIT_TIMEOUT_MS);
        }
    }

    // Ensure test application's log files and resources are freed.
    FreeAppLoggerResources();
    CdiLoggerShutdown(false); // Matches call to CdiLoggerInitialize(). NOTE: false= Normal termination.

    TestCommandLineParserDestroy(command_line_handle);

    // Free signal handler lock, if it was created.
    CdiOsCritSectionDelete(signal_handler_lock);
    signal_handler_lock = NULL;

    return kProgramExecutionStatusExitError == status;
}
