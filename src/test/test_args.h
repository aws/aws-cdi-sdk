// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_args.c.
 */

#ifndef TEST_ARGS_H__
#define TEST_ARGS_H__

/// @brief Using the console logger. This define is used in test_common.h to determine if the TestConsoleLog() API
/// function needs to be implemented or not in that header file.
#define USE_CONSOLE_LOGGER

#include <assert.h>
#include <stdbool.h>

#include "logger_api.h"
#include "cdi_baseline_profile_api.h"
#include "cdi_core_api.h"
#include "cdi_utility_api.h"
#include "cdi_os_api.h"
#include "test_common.h"

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

/// @brief An attosecond is 10^-18 seconds. Using this for storing the period value. High precision is needed to prevent
/// drift in the RTP time generated from different time sources.
#define ATTOSECONDS_PER_SECOND         (1000000000000000000ULL)

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
    kTestOptionNewStream,
    kTestOptionConnectionTimeout,
    kTestOptionLogLevel,
    kTestOptionLogComponent,
    kTestOptionNumLoops,
    kTestOptStatsConfigPeriod,
    kTestOptStatsConfigCloudWatch,
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
    /// If connection protocol is AVM, then this field holds the data type.
    CdiBaselineAvmPayloadType avm_data_type;
    /// Video parameters set by user with --avm_video option.  Unused if not --avm_video payload type.
    CdiAvmVideoConfig video_params;
    /// Audio parameters set by user with --avm_audio option.  Unused if not --avm_audio payload type.
    CdiAvmAudioConfig audio_params;
    /// If using audio set this true if either the sample rate, bit depth, or any of the sample groups are unspecified.
    bool do_not_use_audio_rtp_time;
    /// Integer value in attoseconds for the audio sample period. Unused if not --avm_audio payload type.
    uint64_t audio_sample_period_attoseconds;
    /// Integer value of the sample rate for RTP timestamps. This is 90kHz for video or the audio sample rate value.
    uint32_t rtp_sample_rate;
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
 * @brief A structure that holds all the test settings as set from the command line.
 */
typedef struct {
    /// When true, Tx mode is enabled.
    bool tx;
    /// When true, Rx mode is enabled.
    bool rx;
    /// String defining the connection name assigned to this connection.
    char connection_name_str[MAX_CONNECTION_NAME_STRING_LENGTH];
    /// Enum representing the connection protocol type.
    TestConnectionProtocolType connection_protocol;
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
    /// The number of transactions in the test.  One transaction can multiple stream payloads.
    int num_transactions;
    /// The numerator for the number of payloads per second to send during the test.
    int rate_numerator;
    /// The denominator for the number of payloads per second to send during the test.
    int rate_denominator;
    /// The number of PTP counts each payload advances PTP time for video or ancillary data.
    int video_anc_ptp_periods_per_payload;
    /// The transmit timeout in microseconds for a tx payload.
    int tx_timeout;
    /// The receive buffer delay in milliseconds for a rx payload.
    int rx_buffer_delay_ms;
    /// When true, there was an error in one or more of the command line arguments that are used to create this data
    /// structure.
    bool arg_error;
    /// The number of microseconds in the selected frame rate.
    uint32_t rate_period_microseconds;
    /// The number of nanoseconds in the selected frame rate. Used for PTP time with high precision to limit time drift.
    uint64_t rate_period_nanoseconds;
    /// The 0-based packet polling thread's CPU core number; -1 disables pinning to a specific core.
    int thread_core_num;
    /// The number of streams in this connection.
    int number_of_streams;
    /// Array of stream settings, where each element represents a unique stream.
    StreamSettings stream_settings[MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION];
    /// @brief Statistics gathering period in seconds.
    int stats_period_seconds;
} TestSettings;

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
    char base_log_filename_str[MAX_LOG_FILENAME_LENGTH];

    /// @brief The SDK log file string.
    char sdk_log_filename_str[MAX_LOG_FILENAME_LENGTH];

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
    CloudWatchConfigData cloudwatch_config;
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
void LogComponentToString(const EnumStringKey* key_array, char* log_component_str, CdiLogComponent* log_component_ptr);

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
