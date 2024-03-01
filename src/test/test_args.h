// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_args.c.
 */

#ifndef TEST_ARGS_H__
#define TEST_ARGS_H__

#include <stdbool.h>

#include "cdi_baseline_profile_01_00_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_utility_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief An impossible number for a CPU core number that can be used to detect an invalid core setting.
#define OPTARG_INVALID_CORE           (-1)

/// @brief The default number of loops the test application will run.
#define DEFAULT_NUM_LOOPS             (1)

/// @brief The default number of loops the test application will run.
#define RUN_FOREVER_VALUE             (0)

/// @brief The maximum characters for the log components string.
#define MAX_CHARACTERS_LOG_COMPONENTS (256)

/// @brief The maximum characters for the log components string.
#define MAX_CHARACTERS_CONNECTION_INFO (20)

/// @brief ST 2110 Specifies a 90kHz sample rate for video and ancillary data.
#define PCR_VIDEO_SAMPLE_RATE          (90000)

/**
 * Enum for test pattern types. This list kept in sync with patterns_array[].
 */
typedef enum {
    kTestPatternSame,
    kTestPatternInc,
    kTestPatternSHR,
    kTestPatternSHL,
    kTestPatternNone,
    kTestPatternIgnore,
} TestPatternType;

/**
 * Enum for the list of test command line options.
 * NOTE: Must keep in sync with OptDef my_options[] table in test_args.c.
 */
typedef enum {
    kTestOptionLogSingleFile,
    kTestOptionLogMultipleFiles,
    kTestOptionUseStderr,
    kTestOptionMultiWindowConsole,
    kTestOptionConnectionName,
    kTestOptionTransmit,
    kTestOptionReceive,
    kTestOptionAVMAutoRx,
    kTestOptionAVMVideo,
    kTestOptionAVMAudio,
    kTestOptionAVMAncillary,
    kTestOptionStreamID,
    kTestOptionConfigSkip,
    kTestOptionKeepAlive,
    kTestOptionAdapter,
    kTestOptionBufferType,
    kTestOptionLocalIP,
    kTestOptionDestPort,
    kTestOptionRemoteIP,
    kTestOptionBindIP,
    kTestOptionShareThread,
    kTestOptionCore,
    kTestOptionPayloadSize,
    kTestOptionNumTransactions,
    kTestOptionRate,
    kTestOptionTxTimeout,
    kTestOptionRxBufferDelay,
    kTestOptionPattern,
    kTestOptionPatternStart,
    kTestOptionUseRiffFile,
    kTestOptionFileRead,
    kTestOptionFileWrite,
    kTestOptionNewConnection,
    kTestOptionNewConnectionMultipleEndpoints,
    kTestOptionNewStream,
    kTestOptionConnectionTimeout,
    kTestOptionLogLevel,
    kTestOptionLogComponent,
    kTestOptionLogTimestamps,
    kTestOptionNumLoops,
    kTestOptionStatsConfigPeriod,
#ifndef CDI_NO_MONITORING
    kTestOptionStatsConfigCloudWatch,
#endif
    kTestOptionNoPayloadUserData,
    kTestOptionHelp,
    kTestOptionHelpVideo,
    kTestOptionHelpAudio,
    kTestOptionHelpRiff,
    kTestOptionHelpStats,
    kTestOptionVersion,
    // NOTE: Must keep in sync with OptDef my_options[] table in test_args.c.
} TestOptionNames;

/**
 * @brief A structure that holds all the settings for a stream as set from the command line.
 */
typedef struct {
    /// Unique stream ID.
    int stream_id;
    /// The payload size in bytes of the test payload.
    int payload_size;
    /// For receiver, auto-detect incoming AVM data and output to log.
    bool avm_auto_rx;
    /// If connection protocol is AVM, then this field holds the data type.
    CdiBaselineAvmPayloadType avm_data_type;
    /// Video parameters set by user with --avm_video option.  Unused if not --avm_video payload type.
    CdiAvmVideoConfig video_params;
    /// Audio parameters set by user with --avm_audio option.  Unused if not --avm_audio payload type.
    CdiAvmAudioConfig audio_params;
    /// Ancillary parameters set by user with --avm_anc option.  Unused if not --avm_anc payload type.
    CdiAvmAncillaryDataConfig ancillary_data_params;
    /// The number of payloads to skip before sending the video or audio parameters again.
    int config_skip;
    /// Enum representing the data pattern type.
    TestPatternType pattern_type;
    /// The configuration structure to send with AVM payloads.
    CdiAvmConfig avm_config;
    /// The bit size of the groups to not split across sgl entries. As an example for video avoid splitting pixels by
    /// setting this to the pixel_depth times the number of samples per pixel.
    int unit_size;
    /// 64-bit start value for the test pattern.
    uint64_t pattern_start;
    /// Specifies that the files provided by file_read_str and/or file_write_str will be read and written as RIFF files,
    /// which specify the payload size along with the payload data.
    bool riff_file;
    /// String defining the input file name for test data. If this is specified for a transmitter it overrides any
    /// pattern options. If specified on the receiver the received data will be checked against this file.
    const char* file_read_str;
    /// String defining the output file name for test data from the receiver.
    const char* file_write_str;

    /// The destination port number. Only used if stream
    int dest_port;

    /// The remote network adapter IP address.
    const char* remote_adapter_ip_str;
} StreamSettings;

/**
 * @brief A structure that holds all the test settings for a connection as set from the command line.
 */
typedef struct {
    /// When true, Tx mode is enabled.
    bool tx;
    /// When true, Rx mode is enabled.
    bool rx;
    /// String defining the connection name assigned to this connection.
    char connection_name_str[CDI_MAX_CONNECTION_NAME_STRING_LENGTH];
    /// Enum representing the connection protocol type.
    CdiConnectionProtocolType connection_protocol;
    /// When true, receiver stays alive even after the first test finishes.
    bool keep_alive;
    /// Enum representing the buffer type.
    CdiBufferType buffer_type;
    /// The local network adapter IP address.
    const char* local_adapter_ip_str;
    /// The destination port number.
    int dest_port;
    /// The remote network adapter IP address.
    const char* remote_adapter_ip_str;
    /// The adapter IP address to bind to.
    const char* bind_ip_addr_str;
    /// The number of transactions in the test.  One transaction can transfer multiple stream payloads.
    int num_transactions;
    /// The numerator for the number of payloads per second to send during the test.
    int rate_numerator;
    /// The denominator for the number of payloads per second to send during the test.
    int rate_denominator;
    /// The transmit timeout in microseconds for a tx payload.
    int tx_timeout;
    /// The receive buffer delay in milliseconds for a rx payload.
    int rx_buffer_delay_ms;
    /// When true, there was an error in one or more of the command line arguments that are used to create this data
    /// structure.
    bool arg_error;
    /// The number of microseconds in the selected frame rate.
    uint32_t rate_period_microseconds;
    /// The identifier of the single poll thread to share with this connection; 0 or -1 creates a unique poll thread
    /// associated only with this connection.
    int shared_thread_id;
    /// The 0-based packet poll thread's CPU core number; -1 disables pinning to a specific core.
    int thread_core_num;
    /// The number of streams in this connection.
    int number_of_streams;
    /// Array of stream settings, where each element represents a unique stream.
    StreamSettings stream_settings[CDI_MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION];
    /// @brief Statistics gathering period in seconds.
    int stats_period_seconds;
    /// @brief Connection contains multiple endpoints.
    bool multiple_endpoints;
} TestSettings;

/// Forward reference. Contains connection state and test settings.
typedef struct TestConnectionInfo TestConnectionInfo;

/**
 * @brief A structure that holds all of the global test settings set from the command-line.
 */
typedef struct {
    /// @brief The global log level.
    CdiLogLevel log_level;

    /// @brief The number of loops to run the tests in main.
    int num_loops;

    /// @brief The timeout in seconds to probe for a connection between EFA devices before abandoning the connection.
    int connection_timeout_seconds;

    /// @brief Flag for whether we are using multiple log files for each connection, or just a unified log file.
    bool use_single_connection_log_file;

    /// @brief The logging method chosen by command line options.
    CdiLogMethod base_log_method;

    /// @brief Pointer to the base log file name.
    char base_log_filename_str[CDI_MAX_LOG_FILENAME_LENGTH];

    /// @brief The SDK log file string.
    char sdk_log_filename_str[CDI_MAX_LOG_FILENAME_LENGTH];

    /// @brief Handle to global file log for the test application.
    CdiLogHandle test_app_global_log_handle;

    /// @brief The global log component array.
    CdiLogComponent log_component[kLogComponentLast];

    /// @brief Flag for whether we are using the multi-window console mode or just the standard console.
    bool use_multiwindow_console;

    /// @brief Output error messages to stderr in addition to log files (if log files are enabled).
    bool use_stderr;

    /// @brief Structure used to hold the information about the adapter used by the test.
    CdiAdapterData adapter_data;

    /// @brief Enable CloudWatch. Data in cloudwatch_config is valid.
    bool use_cloudwatch;

    /// @brief Statistics gathering CloudWatch configuration data.
    CdiCloudWatchConfigData cloudwatch_config;

    /// @brief Flag to disable checks using payload_user_data when sender is not another cdi_test instance.
    bool no_payload_user_data;

    /// @brief Total number of connections.
    int total_num_connections;

    /// @brief Pointer to array of connection info structures.
    TestConnectionInfo* connection_info_array;

    /// @brief Number of connections that have been established.
    int num_connections_established;

    /// @brief signal used when all connections have been established.
    CdiSignalType all_connected_signal;

    /// @brief Log origination_ptp_timestamp values.
    bool log_timestamps;
} GlobalTestSettings;

/**
 * Enumerated type that can be used to indicate whether the program should exit and whether it should do so with a 0 or
 * a 1 exit status.
 */
typedef enum {
    kProgramExecutionStatusContinue, ///< The program should continue to run.
    kProgramExecutionStatusExitOk,   ///< The program should exit successfully (exit code of 0).
    kProgramExecutionStatusExitError ///< The program should exit and indicate a failure (exit code of 1).
} ProgramExecutionStatus;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Pretty-print the test settings.
 *
 * @param test_settings_ptr Pointer to the test settings struct.
 * @param num_connections The number of connections in the test_settings array.
 */
void PrintTestSettings(const TestSettings* const test_settings_ptr, const int num_connections);


/**
 * @brief A function that turns selected log components into a string of log components delimited by the '|' character.
 *
 * @param key_array Array of log component (enum, string) items.
 * @param log_component_str A pointer to the buffer that the resulting log component string will be held.
 * @param log_component_ptr A pointer to the array of selected log components; this is held in the global_test_settings
 *                          structure.
 */
void LogComponentToString(const CdiEnumStringKey* key_array, char* log_component_str, CdiLogComponent* log_component_ptr);

/**
 * A helper function that takes in command-line arguments, sanitizes them for syntax and correctness, and then assigns
 *   them to the test_settings data structure.
 *
 * @param argc The system command line argument count variable.
 * @param argv_ptr The pointer to the system command line arguments array.
 * @param test_settings_ptr Pointer to an array of test settings data structures we will modify.
 * @param num_connections_found Pointer to the number of connections found.
 * @return ProgramExecutionStatus to indicate whether the program should continue and if not, what exit status to have.
 */
ProgramExecutionStatus GetArgs(int argc, const char** argv_ptr, TestSettings* test_settings_ptr,
                               int* num_connections_found);

#endif // TEST_ARGS_H__
