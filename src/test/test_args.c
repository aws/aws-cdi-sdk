// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions used for capturing command line arguments and sanitizing them and
 * converting them to cdi_test program test settings.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "test_args.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"
#include "cdi_log_enums.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_test.h"
#include "cdi_utility_api.h"
#include "optarg.h"
#include "riff.h"
#include "test_common.h"
#include "test_console.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * Enum for max and min IP port numbers.
 **/
enum PortNumLimits {
    kPortNumMin = 1,
    kPortNumMax = UINT16_MAX
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/**
  * Enum/String array that contains valid test pattern modes.
 */
static const CdiEnumStringKey patterns_key_array[] = {
    /// SAME pattern stores uses the pattern_start value unmodified for every pattern word.
    { kTestPatternSame,       "SAME" },
    /// INC pattern will start at pattern_start and increment the value by one for every pattern word.
    { kTestPatternInc,        "INC" },
    /// SHR pattern starts at pattern_start and does a circular right shift by one for every pattern word.
    { kTestPatternSHR,        "SHR" },
    /// SHL pattern starts at pattern_start and does a circular left shift by one for every pattern word.
    { kTestPatternSHL,        "SHL" },
    /// NONE pattern means pattern is not set and either file_read is being used or on rx there is no payload data
    /// checking.
    { kTestPatternNone,       "NONE" },
    /// IGNORE pattern means pattern is not set and either file_read is being used or on rx there is no payload
    /// validation (no payload data checking).
    { kTestPatternIgnore,     "IGNORE" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Function used to convert a test pattern enum value to a string.
 *
 * @param enum_value Value to convert to a string.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
static const char* TestPatternEnumToString(TestPatternType enum_value)
{
    return CdiUtilityEnumValueToString(patterns_key_array, enum_value);
}

/**
 * @brief Function used to convert a test protocol enum value to a string.
 *
 * @param enum_value Value to convert to a string.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
static const char* TestProtocolEnumToString(CdiConnectionProtocolType enum_value)
{
    return CdiUtilityEnumValueToString(CdiUtilityKeyGetArray(kKeyConnectionProtocolType), enum_value);
}

/**
 * @brief Function used to convert a test pattern string to a matching enum value.
 *
 * @param name_str Pointer to string name of enumerated value.
 *
 * @return Returned enumerated value. If no match was found, CDI_INVALID_ENUM_VALUE is returned.
 */
static TestPatternType TestPatternStringToEnum(const char* name_str)
{
    return CdiUtilityStringToEnumValue(patterns_key_array, name_str);
}

/**
 * @brief Function used to convert a test protocol string to a matching enum value.
 *
 * @param name_str Pointer to string name of enumerated value.
 *
 * @return Returned enumerated value. If no match was found, CDI_INVALID_ENUM_VALUE is returned.
 */
static CdiConnectionProtocolType TestProtocolStringToEnum(const char* name_str)
{
    return CdiUtilityStringToEnumValue(CdiUtilityKeyGetArray(kKeyConnectionProtocolType), name_str);
}

/**
 * @brief Prints the main usage message.
 *
 * @param opt_array_ptr Pointer to the options array, where all usage information is stored.
 * @param opt_ptr Pointer to optional argument.
 */
static void PrintUsageVideo(const OptDef* opt_array_ptr, const OptArg* opt_ptr)
{
    CdiAvmBaselineProfileVersion version = {
        .major = 1,
        .minor = 0
    };
    if (opt_ptr->num_args) {
        if (kCdiStatusOk != CdiAvmValidateBaselineVersionString(kCdiAvmVideo, opt_ptr->args_array[0], &version)) {
            TestConsoleLog(kLogError, "Invalid --help_video version [%s].", opt_ptr->args_array[0]);
            return;
        }
    }

    TestConsoleLog(kLogInfo, "Usage for --avm_video option:");
    PrintOption(&opt_array_ptr[kTestOptionAVMVideo]);
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Choices for each argument:");
    TestConsoleLog(kLogInfo, "  [version]              - xx.xx (Optional AVM profile version)");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "  Data shown for AVM version %02d.%02d:", version.major, version.minor);
    TestConsoleLog(kLogInfo, "  <width>                - any integer");
    TestConsoleLog(kLogInfo, "  <height>               - any integer");
    TestConsoleLog(kLogInfo, "  <sampling type>        - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoSamplingType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <alpha channel>        - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoAlphaChannelType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <bit depth>            - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoBitDepthType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <rate numerator>       - any integer");
    TestConsoleLog(kLogInfo, "  <rate denominator>     - any integer");
    TestConsoleLog(kLogInfo, "  <colorimetry>          - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoColorimetryType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <interlace>            - true or false");
    TestConsoleLog(kLogInfo, "  <segmented>            - true or false");
    TestConsoleLog(kLogInfo, "  <TCS>                  - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoTcsType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <encoding range>       - any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmVideoRangeType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <PAR width>            - any integer");
    TestConsoleLog(kLogInfo, "  <PAR height>           - any integer");
    TestConsoleLog(kLogInfo, "  <start vertical pos>   - any integer");
    TestConsoleLog(kLogInfo, "  <vertical size>        - any integer");
    TestConsoleLog(kLogInfo, "  <start horizontal pos> - any integer");
    TestConsoleLog(kLogInfo, "  <horizontal size>      - any integer");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Example:");
    TestConsoleLog(kLogInfo, "  --avm_video 1920 1080 YCbCr422 Unused 10bit 30 1 BT2020 true false PQ Narrow 3 4 21 "
                   "1059 100 1820");
    TestConsoleLog(kLogInfo, "");
}

/**
 * @brief Prints the audio usage message.
 *
 * @param opt_array_ptr Pointer to the options array, where all usage information is stored.
 * @param opt_ptr Pointer to option argument.
 */
static void PrintUsageAudio(const OptDef* opt_array_ptr, const OptArg* opt_ptr)
{
    CdiAvmBaselineProfileVersion version = {
        .major = 1,
        .minor = 0
    };
    if (opt_ptr->num_args) {
        if (kCdiStatusOk != CdiAvmValidateBaselineVersionString(kCdiAvmAudio, opt_ptr->args_array[0], &version)) {
            TestConsoleLog(kLogError, "Invalid --help_audio version [%s].", opt_ptr->args_array[0]);
            return;
        }
    }

    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Usage for --avm_audio option:");
    PrintOption(&opt_array_ptr[kTestOptionAVMAudio]);
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Choices for each argument:");
    TestConsoleLog(kLogInfo, "  [version]              - xx.xx (Optional AVM profile version)");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "  Data shown for AVM version %02d.%02d:", version.major, version.minor);
    TestConsoleLog(kLogInfo, "  <channel grouping>     - Any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmAudioChannelGroupingType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <sample rate kHz>      - Any of the following strings:");
    PrintKeyArrayNames(CdiAvmKeyGetArray(kKeyAvmAudioSampleRateType, &version), OPTARG_AVM_USAGE_LIST_INDENT);
    TestConsoleLog(kLogInfo, "  <language code>        - Any two or three character string or \"none\".");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Examples:");
    TestConsoleLog(kLogInfo, "  --avm_audio 51 48kHz none");
    TestConsoleLog(kLogInfo, "  --avm_audio M 96kHz fr");
}

/**
 * @brief Prints help on RIFF format and usage in cdi_test.
 *
 * @param opt_ptr Pointer to option argument.
 */
static void PrintRiffHelp(const OptArg* opt_ptr)
{
    if (opt_ptr->num_args) {
        ReportRiffFileContents(opt_ptr->args_array[0], 100, kRiffDumpRaw);
        return;
    }

    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "The RIFF file format is made up of chunks. Every chunk consists of a");
    TestConsoleLog(kLogInfo, "four character code followed by a 32 bit integer that indicates the");
    TestConsoleLog(kLogInfo, "size in bytes of the chunk data. The first chunk is the RIFF chunk");
    TestConsoleLog(kLogInfo, "which also has a form type. For cdi_test the only currently");
    TestConsoleLog(kLogInfo, "supported form type is 'CDI '. The RIFF chunk data is made of a");
    TestConsoleLog(kLogInfo, "subchunk for each payload to be sent. Each subchunk header is");
    TestConsoleLog(kLogInfo, "identified with a four character code 'ANC '.");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "                            RIFF format");
    TestConsoleLog(kLogInfo, "                               bytes");
    TestConsoleLog(kLogInfo, "   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15");
    TestConsoleLog(kLogInfo, "  'R' 'I' 'F' 'F' <chunk size 4B><form ='CDI '><Chunk1 = 'ANC '>");
    TestConsoleLog(kLogInfo, "  <chunk1 size 4B><payload data is chunk1 size bytes long ......");
    TestConsoleLog(kLogInfo, "  ...............................................................");
    TestConsoleLog(kLogInfo, "  ...............................><Chunk2='ANC '><chunk2 size 4B>");
    TestConsoleLog(kLogInfo, "  <payload number 2 is chunk2 size in bytes .....................");
    TestConsoleLog(kLogInfo, "  ///////////////////////////////////////////////////////////////");
    TestConsoleLog(kLogInfo, "  <ChunkN='ANC '><ChunkN size 4B><ChunkN data of chunk-n size   >");
    TestConsoleLog(kLogInfo, "  ...............................................................");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "NOTE: If the transmitter is sending RIFF payloads the receiver must also use");
    TestConsoleLog(kLogInfo, "      the --riff option or payload size errors could be generated.");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "For additional RIFF file information please see "
                             "https://johnloomis.org/cpe102/asgn/asgn1/riff.html.");
    TestConsoleLog(kLogInfo, "");
}

#ifndef CDI_NO_MONITORING
/**
 * @brief Prints the statistics help message.
 */
static void PrintStatsHelp(const OptDef* opt_array_ptr)
{
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Usage for --stats_... options:");
    PrintOption(&opt_array_ptr[kTestOptionStatsConfigCloudWatch]);
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "To specify the stats gathering period for a connection, use the --stats_period option.");
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo,
                   "To enable CloudWatch use the global --stats_cloudwatch option. Choices for each argument:");
    TestConsoleLog(kLogInfo, "  <namespace>            - Name of CloudWatch namespace. If \"NULL\", \"%s\" is used.",
                   CLOUDWATCH_DEFAULT_NAMESPACE_STRING);
    TestConsoleLog(kLogInfo, "  <region>               - Name of CloudWatch region. If \"NULL\", region where test is");
    TestConsoleLog(kLogInfo, "                           running is used.");
    TestConsoleLog(kLogInfo, "  <dimension domain>     - Name of CloudWatch dimension called \"Domain\".");
    TestConsoleLog(kLogInfo, "Examples:");
    TestConsoleLog(kLogInfo, "  --stats_period 60");
    TestConsoleLog(kLogInfo, "  --stats_cloudwatch MyNameSpace us-west-2 MyStream");
    TestConsoleLog(kLogInfo, "  --stats_cloudwatch NULL NULL MyStream");
}
#else
static void PrintStatsHelp(const OptDef* unused)
{
    (void)unused;
    TestConsoleLog(kLogInfo, "CloudWatch statistics gathering is not available.");
}
#endif

/**
 * User-defined command-line options. These are the short options and the long options for our command line arguments.
 * Keep these organized, so they're easy to change and also easy to debug. The last element is required to be 0's.
 *
 * NOTE: Must keep this table in sync with typedef enum TestOptionNames in test_args.h.
 */
static OptDef my_options[] =
{
    // Fields:
    // short option (required),   long option (optional),   number of arguments (required),
    //      argument string (required),   pointer to array of strings of arg choices (optional),
    //      description string (required)
    { "l",    "log",          1, "<log file path>",  NULL,
        "Global option. The base file name and path used for logging. This test application uses\n"
        "one log file and the SDK uses one log file. Only one of --log or --logs options\n"
        "can be used. If no log file is specified, all output goes to stdout."},
    { "L",    "logs",         1, "<base log path>",  NULL,
        "Global option. The base file name and path used for logging. In addition to two global\n"
        "log files (one for this test app and one for the SDK), each connection uses\n"
        "unique log files (one for this test app and one for the SDK). Only one of --log\n"
        "and --logs options can be used. If no log file is specified, all output goes to\n"
        "stdout."},
    { "err",  "stderr",       0, NULL,               NULL,
        "Global option. Cause errors to be sent to stderr in addition to log files."},
    { "mwin", "multiwindow",  0, NULL,               NULL,
        "Global option. Enable multi-window console mode. Uses the callback log."},
    { "name", "connection_name", 1, "<name>",        NULL,
        "Assign a connection a unique connection name string."},
    { "tx",   "tx",           1, "<protocol>",       NULL,
        "Choose transmitter mode (default RAW) for this connection. AVM mode requires one\n"
        "of --avm_video, --avm_audio, or --avm_anc options also be used."},
    { "rx",   "rx",           1, "<protocol>",       NULL,
        "Choose receiver mode (default RAW) for this connection. AVM mode requires one of\n"
        "avm_video, --avm_audio, or --avm_anc options also be used."},
    { "vid",  "avm_video",   18, "<video args>",    NULL, /* Can use 18 or 19 arguments */
        "Set video parameters for AVM stream. The <protocol> argument of --tx or --rx must be\n"
        "AVM. Except for version, all parameters are required and must be specified in this order:\n"
        "[version] <width> <height> <sampling type> <alpha channel> <bit depth>\n"
        "<rate numerator> <rate denominator> <colorimetry> <interlace> <segmented>\n"
        "<TCS> <encoding range> <PAR width> <PAR height> <start vertical position>\n"
        "<vertical size> <start horizontal position> <horizontal size>\n"
        "Use --help_video option for more detailed help for this option."},
    { "aud",  "avm_audio",     3, "<audio args>",     NULL, /* Can use 3 or 4 arguments */
        "Set audio parameters for AVM stream. The <protocol> argument of --tx or --rx must be\n"
        "AVM. Except for version, all parameters are required and must be specified in this order:\n"
        "[version] <channel grouping> <sample rate kHz> <language code>\n"
        "Use --help_audio for more detailed help for this option."},
    { "anc",  "avm_anc",       0, NULL,               NULL, /* Can use 0 or 1 argument */
        "Indicates AVM data type is ancillary for this stream. The <protocol> argument of\n"
        "--tx, or --rx must be AVM. Optionally, may specify baseline profile version [xx.xx]."},
    { "id",   "id",           1, "<stream id>",      NULL,
        "Assign a unique ID to a stream. Applies only to AVM connections and is required\n"
        " for them. The value must be between 0 and 65535, inclusive."},
    { "cskp", "config_skip",  1, "<transactions>",   NULL,
        "In AVM mode, stream-specific option to always send (or receive) config data on\n"
        "the first transaction. Then skip this number of transactions before sending\n"
        "(or receiving) config data again."},
    { "ka",   "keep_alive",   0, NULL,               NULL,
        "For the given connection, Tx continues sending payloads and Rx continues receiving payloads\n"
        "even when a payload error is detected. This option is disabled by default."},
    { "ad",   "adapter",      1, "<adapter type>",   NULL,
        "Global option. Choose an adapter for the test to run all connections on."},
    { "bt",   "buffer_type",  1, "<buffer type>",    NULL,
        "Choose a buffer type for all streams on this connection to use to send packets.\n"
        "Refer to API documentation for a description of each buffer type."},
    { "lip",  "local_ip",     1, "<ip address>",     NULL,
        "Global option. Set the IP address of the local network adapter."},
    { "dpt",  "dest_port",    1, "<port num>",       NULL,
        "Set a connection-specific destination port."},
    { "rip",  "remote_ip",    1, "<ip address>",     NULL,
        "Only for Tx connections, the IP address of the remote network adapter."},
    { "bip",  "bind_ip",    1, "<ip address>",     NULL,
        "The IP address of the network adapter to bind to. If not used, the default adapter is used."},
    { "tc", "thread_conn",    1, "<id>",       NULL,
        "Share a single poll thread with all connections that use this ID. ID must be > 0."},
    { "core", "core",         1, "<core num>",       NULL,
        "Set the desired CPU core for this connection."},
    { "psz",  "payload_size", 1, "<byte_size>",      NULL,
        "Set the size (in bytes) for a stream's payload. If --riff is being used\n"
        "with --file_read then this sets the maximum allowable payload size."},
    { "tnum", "num_transactions", 1, "<count>",      NULL,
        "Set the number of transactions for this connection. If this option is not\n"
        "specified or it is set to 0, it will run forever."},
    { "rt",   "rate",         1, "<rate num/den>",   NULL,
        "Set the data rate for this connection as 'numerator/denominator' or 'numerator'\n"
        "for integer rates. No whitespaces are allowed in the 'numerator/denominator'\n"
        "string."},
    { "to",   "tx_timeout",   1, "<microseconds>",   NULL,
        "Set the transmit timeout for a payload in this connection in microseconds. This\n"
        "option directly controls the max_latency_microsecs parameter in the\n"
        "Cdi..Tx..Payload() API function calls, and its default is set by --rate.\n"},
    { "rbd",  "rx_buffer_delay",   1, "<milliseconds>",   NULL,
        "Set the receive buffer delay for a payload in this connection in milliseconds. This\n"
        "option directly controls the buffer_delay_ms setting in the CdiRxConfigData used when\n"
        "creating a connection, and its default is 0 or \"disabled\" (no buffer). To enable and\n"
        "use the SDK default value specify \"automatic\" (see CDI_ENABLED_RX_BUFFER_DELAY_DEFAULT_MS).\n"
        "The maximum allowable value is defined by CDI_MAXIMUM_RX_BUFFER_DELAY_MS."},
    { "pat",  "pattern",      1, "<pattern choice>", patterns_key_array,
        "Choose a pattern mode for a stream's test data.\n"
        "All payloads will contain this same repeating pattern starting at the value given\n"
        "by --pat_start and continuing throughout the payload. However, the first payload\n"
        "word will increment for each payload in order to make each payload unique.\n"
        "Defaults to INC for Tx and NONE for Rx connections. Use NONE on Rx to disable\n"
        "payload data checking or if Tx is not the CDI test app. Use IGNORE to disable\n"
        "all payload data, count, and RTP timestamp checking.\n"
        "SAME:data doesn't change, INC:increment, SHR/SHL:barrel shift right/left.\n"},
    { "pst",  "pat_start",    1, "<64-bit hex>",     NULL,
        "The 64-bit hex pattern start value for this stream (without '0x', i.e --pat_start\n"
        "0123456789ABCDEF) This option is only relevant if --pattern does not equal NONE."},
    { "riff", "riff",         0,  NULL,              NULL,
        "This option specifies that the file passed to --file_read or --file_write will\n"
        "be treated as a RIFF file. RIFF formatted files specify the size of each payload\n"
        "instead of using --payload_size for fixed payload sizes. The receiver must also use the\n"
        "--riff option if the transmitter is sending RIFF payloads or else receiver\n"
        "payload size checking will fail.\n"
        "NOTE: See --help_riff for more information on expected file formatting."},
    { "fr",   "file_read",    1, "<file path>",      NULL,
        "Specifies a data file to use for payload data for a stream instead of an\n"
        "algorithmic pattern. When this option is used, the --pattern option must not be\n"
        "used or set to NONE."},
    { "fw",   "file_write",   1, "<file path>",      NULL,
        "For Rx connections only, specifies a file to write a stream's received data to."},
    { "X",    "new_conn",     0, NULL,               NULL,
        "Create a new connection with a single endpoint. All options that follow modify this\n"
        "new connection until the option is used again. This or --new_conns option is required\n"
        "to precede all connection settings."},
    { "XS",    "new_conns",     0, NULL,               NULL,
        "Create a new connection with multiple endpoints. All options that follow modify\n"
        "this new connection until the option is used again. This or --new_conn option is\n"
        "required to precede all connection settings."},
    { "S",    "new_stream",   0, NULL,               NULL,
        "Create a new stream. All options that follow modify this new stream until this\n"
        "option is used again. This --new_stream option is required to precede all stream\n"
        "settings."},
    { "ct",   "conn_timeout", 1, "<seconds>",        NULL,
        "Global option. Set the global connection timeout in seconds. If left unspecified,\n"
        "the default connection timeout is 10 minutes (600sec)."},
    { "ll",   "log_level",    1, "<log level>",      NULL,
        "Global option. Set the log level. Default to DEBUG."},
    { "lc",   "log_component",1, "<log component>",  NULL,
        "Global option. Sets the SDK component type for logging. Multiple types can be utilized by\n"
        "separating the arguments with spaces and enclosing in double quotes.\n"
        "For example: \"PROBE PAYLOAD_CONFIG\".\n"
        "GENERIC is always on by default and should not be included in the command-line."},
    { "nl",   "num_loops",    1, "<number of loops>",NULL,
        "Global option. Set the number of times the test application will run through all\n"
        "transactions on all connections. This is useful for step-debugging. A value of 0\n"
        "will run forever."},
    { "stp",  "stats_period", 1, "<period_sec>",     NULL,
        "Set the connection-specific statistics gathering period in seconds."},
#ifndef CDI_NO_MONITORING
    { "st",   "stats_cloudwatch", 3, "<stats args>", NULL,
        "Global option. Set the CloudWatch statistics gathering parameters. All parameters are\n"
        "required and must be specified in this order:\n"
        "<namespace> <region> <dimension domain>\n"
        "Use --help_stats for more detailed help for this option."},
#endif
    { "nopud", "no_payload_user_data", 0, NULL,      NULL,
        "Global option. To implement certain checks cdi_test uses the payload_user_data field that\n"
        "is part of each payload. When cdi_test is used as a receiver for CDI from an application\n"
        "other than cdi_test, these checks are expected to fail.\n"
        "Use --no_payload_user_data to disable these checks."},
    { "h",    "help",         0, NULL,               NULL, "Print the usage message."},
    { "hv",   "help_video",   0, NULL,               NULL, "Print the specific usage message for the --avm_video option."},
    { "ha",   "help_audio",   0, NULL,               NULL, "Print the specific usage message for the --avm_audio option."},
    { "hr",   "help_riff",    0, NULL,               NULL, "Print information related the formatting of the RIFF files."},
    { "hs",   "help_stats",   0, NULL,               NULL, "Print the specific usage message for the --stats option."},
    { "v",    "version",      0, NULL,               NULL, "Print the version of the CDI SDK."},
    // NOTE: Must keep this table in sync with typedef enum TestOptionNames in test_args.h.
    //
    // Make sure the last element is 0 so we can know where options end.
    { NULL,   NULL,           0, NULL,               NULL, NULL }
};

/**
 * @brief Initialize the options table with choice string arrays for the arg_choices_array_ptr fields.
 */
static void InitOptionsTable(void)
{
    my_options[kTestOptionTransmit].arg_choices_array_ptr     = CdiUtilityKeyGetArray(kKeyConnectionProtocolType);
    my_options[kTestOptionReceive].arg_choices_array_ptr      = CdiUtilityKeyGetArray(kKeyConnectionProtocolType);
    my_options[kTestOptionAdapter].arg_choices_array_ptr      = CdiUtilityKeyGetArray(kKeyAdapterType);
    my_options[kTestOptionBufferType].arg_choices_array_ptr   = CdiUtilityKeyGetArray(kKeyBufferType);
    my_options[kTestOptionLogLevel].arg_choices_array_ptr     = CdiUtilityKeyGetArray(kKeyLogLevel);
    my_options[kTestOptionLogComponent].arg_choices_array_ptr = CdiUtilityKeyGetArray(kKeyLogComponent);
}

/**
 * Check a string to see if it is a 32 bit base-N number.
 * @param  str             The string we are checking to see if it represents a base-N number. The integer representation
 *                         of the number string in str. Set to NULL if the return number in num_ptr is not needed.
 * @param  base_n_num_ptr  The integer representation of the number string in str. Set to NULL if the return number is
 *                         not needed.
 * @param  base            The numerical base (N) to use for the compare.
 * @return                 True if string represents a base-N number; false if string does not
 */
static bool IsBaseNNumber(const char* str, int* base_n_num_ptr, const int base)
{
    char* end_ptr = NULL;
    int base_n_num = (int)strtol(str, &end_ptr, base);

    // The pointer to the return number is optional, so check for NULL pointer.
    if (base_n_num_ptr != NULL) {
        *base_n_num_ptr = base_n_num;
    }

    return 0 == *end_ptr;
}


/**
 * Check a string to see if it is a base-10 number.
 * @param   str             The string we are checking to see if it represents a base-10 number.
 * @param   base10_num_ptr  The integer representation of the number string in str. Maye be NULL when the return number
 *                          is not needed.
 * @return                  True if string represents a base-10 number; false if string does not
 */
static bool IsBase10Number(const char* str, int* base10_num_ptr)
{
    return IsBaseNNumber(str, base10_num_ptr, 10);
}


/**
 * Check a string to see if it is a 64 bit base-N number.
 * @param  str             The string we are checking to see if it represents a base-N number.The integer representation
 *                         of the number string in str. Set to NULL if the return number in num_ptr is not needed.
 * @param  base_n_num_ptr  The 64 bit integer representation of the number string in str. May be NULL when the return
 *                         number is not needed.
 * @param  base            The numerical base (N) to use for the compare.
 * @return                 True if string represents a base-N number; false if string does not
 */
bool Is64BitBaseNNumber(const char* str, uint64_t* base_n_num_ptr, const int base)
{
    char* end_ptr = NULL;
    uint64_t base_n_num = strtoull(str, &end_ptr, base);

    // The pointer to the return number is optional, so check for NULL pointer.
    if (base_n_num_ptr != NULL) {
        *base_n_num_ptr = base_n_num;
    }

    return 0 == *end_ptr;
}


/**
 * @brief Converts a string to a base-10 number if it can be found at the start of the string. If a number is found,
 * then the end_str pointer points to the character after the number and the number is stored in the num_ptr.  If a
 * number is not found, then end_str pointer points to the start of the string, and the number returned is 0.
 *
 * @param str The pointer to the string to be evaluated.
 * @param num_ptr The pointer to the integer location we will store the result of the conversion.
 * @param end_str The pointer to the remaining string after the converted number has been removed from the beginning.
 *
 * @return True if a number conversion was successful; false if no base-10 number was found.
 */
static bool IntStringToInt(const char* str, int* num_ptr, char** end_str)
{
    return TestStringToInt(str, num_ptr, end_str);
}

/**
 * @brief Check a string provided to see if the entire thing can be converted to an int.
 *
 * @param int_str The string to be evaluated.
 * @param result_ptr The integer result after attempting to interpret the input string.
 *
 * @return True if success, false if the int_str can not be interpreted as a int.
 */
static bool IsIntStringValid(const char* int_str, int* result_ptr)
{
    bool ret = false;
    char* end_str = NULL;

    if (IntStringToInt(int_str, result_ptr, &end_str)) {
        if (end_str == (int_str + strlen(int_str))) {
            ret = true;
        }
    }

    return ret;
}

/**
 * @brief Check a string provided to see if the entire thing can be converted to a bool.
 * Check for 'true' and 'false' as well as '0' and '1' as valid strings.
 *
 * @param bool_str The string to be evaluated.
 * @param result The boolean result after attempting to interpret the input string as a boolean.
 *
 * @return True if success, false if the bool_str can not be interpreted as a bool.
 */
static bool IsBoolStringValid(const char* bool_str, bool* result)
{
    bool ret = false;

    if (1 == strlen(bool_str)) {
        if ('1' == bool_str[0]) {
            ret = true;
            *result = true;
        } else if ('0' == bool_str[0]) {
            ret = true;
            *result = false;
        }
    } else if (0 == CdiOsStrCaseCmp(bool_str, "true")) {
        ret = true;
        *result = true;
    } else if (0 == CdiOsStrCaseCmp(bool_str, "false")) {
        ret = true;
        *result = false;
    }

    return ret;
}

/**
 * @brief Check a string provided to see if it is a valid ip address string of the form: ip.ip.ip.ip
 *
 * @param ip_addr_str The string to be evaluated.
 *
 * @return True if success, false if the ip_addr_str is not of the proper syntax or if it does not contain integers.
 */
static bool IsIPAddrValid(const char* ip_addr_str)
{
    int ip_idx = 0;

    // Loop through the IP address, checking that each segment is an integer.
    // If it isn't an integer, then return false.
    // If we loop through 4 valid segments, then the following character better be the terminator, or it's an error.
    const char* input_str_ptr = ip_addr_str;
    char* end_str = NULL;
    int dummy_int;
    bool syntax_error = false;
    do {
        if (IntStringToInt(input_str_ptr, &dummy_int, &end_str)) {
            input_str_ptr = end_str + 1;
            ip_idx++;
        } else {
            syntax_error = true;
        }

    } while ((*end_str != '\0') && (*end_str == '.') && (ip_idx < 4) && !syntax_error);

    // We exited on either the end of the string or having found 4 segments.
    // Make sure both are true.
    return (!syntax_error && (*end_str == '\0') && (ip_idx == 4));
}

/**
 * @brief Check a string provided by the --rate option to see if it is a valid rate string of either the form
 * rate_numerator/rate_denominator or simply rate_numerator if rate_numerator is an integer value.
 * Converts the numerator and denominator to integers and stores them at the provided pointer locations.
 *
 * @param rate_str Pointer to the string to be evaluated.
 * @param rate_numerator_ptr The pointer to where we should store the numerator integer.
 * @param rate_denominator_ptr The pointer to where we should store the denominator integer.  If only rate numerator was
 *                     provided (rate numerator is an integer value), then this value will be set to 1.
 *
 * @return True if success, false if the rate_str is not of the proper syntax or if it does not contain integers for the
 * numerator and denominator (if a denominator is provided).
 */
static bool IsRateValid(const char* rate_str, int* rate_numerator_ptr, int* rate_denominator_ptr)
{
    bool syntax_error = true;
    char* end_str = NULL;

    // If the numerator is a valid integer.
    if (IntStringToInt(rate_str, rate_numerator_ptr, &end_str)) {

        // If there is nothing past the numerator, then we imply that the denominator is 1.
        if (*end_str == '\0') {
            *rate_denominator_ptr = 1;
            syntax_error = false;

        // Otherwise, check that the denominator is separated by '/' from the numerator, and that there is a valid
        // denominator with nothing after it.
        } else if (*end_str == '/') {
            if (IntStringToInt(end_str+1, rate_denominator_ptr, &end_str)) {
                if (*end_str == '\0') {
                    syntax_error = false;
                }
            }
        }
    }

    return !syntax_error;
}

/**
 * @brief Calculates the frame rate period and RTP video/ancillary timestamp periods for this connection and assigns
 * to the TestSettings for each connection. This function is called only if the rate settings have been verified.
 *
 * @param test_settings_ptr Pointer to the TestSettings structure for each connection.
 */
static void SetConnectionRatePeriods(TestSettings* test_settings_ptr)
{
    // Calculate all necessary periods the test application will use.

    // Frame rate in microseconds used for pacing payloads.
    test_settings_ptr->rate_period_microseconds = ((1000000 * test_settings_ptr->rate_denominator) /
                                                  test_settings_ptr->rate_numerator);

    // Frame rate period in nanoseconds used for fallback audio rtp time period if actual sample time cannot be
    // calculated.
    test_settings_ptr->rate_period_nanoseconds = (((uint64_t)CDI_NANOSECONDS_PER_SECOND * test_settings_ptr->rate_denominator) /
                                                 test_settings_ptr->rate_numerator);

    // How many 90kHz video samples can fit into the frame time.
    test_settings_ptr->video_anc_ptp_periods_per_payload = (PCR_VIDEO_SAMPLE_RATE * test_settings_ptr->rate_denominator)
                                                           / test_settings_ptr->rate_numerator;
}


/**
 * @brief Converts the CdiAvmAudioSampleRate enum into a period value in nanoseconds.
 *
 * @param sample_rate       Enum value of sample rate.
 * @param ret_period_as_ptr Period of sample rate in attoseconds.
 * @param ret_rate_val_ptr  The integer hertz value of the audio sample rate.
 *
 * @return If successful return true or false if sample_rate is 'Unspecified' so no conversion is possible.
 */
static bool AudioSamplePeriodAttoseconds(CdiAvmAudioSampleRate sample_rate, uint64_t* ret_period_as_ptr,
                                         uint32_t* ret_rate_val_ptr)
{
    // Periods range from 10.42us at 96kHz to 31.25us at 32kHz so setting the period value stored to maximize precision.
    // Since rtp timestamps not being sampled against a system clock to keep time in sync accross sources, it is
    // important to have a high precision on the period.
    uint64_t sample_period_as = 0;
    bool return_val = true;
    uint32_t rate_val = 1;

    // The sample_period_as equals attoseconds per second + rounding factor divided by frequency. The rounding factor
    // is 1/2 the sample frequency.
    switch (sample_rate) {
        case kCdiAvmAudioSampleRate48kHz:
            sample_period_as = (ATTOSECONDS_PER_SECOND + 24000) / 48000;
            rate_val = 48000;
            break;
        case kCdiAvmAudioSampleRate96kHz:
            sample_period_as = (ATTOSECONDS_PER_SECOND + 48000) / 96000;
            rate_val = 96000;
            break;
    }

    if (ret_period_as_ptr) {
        *ret_period_as_ptr = sample_period_as;
    } else {
        return_val = false;
    }

    if (ret_rate_val_ptr) {
        *ret_rate_val_ptr = rate_val;
    } else {
        return_val = false;
    }

    return return_val;
}

/**
 * @brief This function searches the log_component array for a matching CdiLogComponent.
 *
 * @param log_component_array_ptr A Pointer to the log component array maintained in global_test_settings.
 * @param component_ptr Pointer to the log component that is to be searched in the log_component array.
 *
 * @return bool True if found in the array, false if not found in array.
 */
static bool LogComponentExists(CdiLogComponent* log_component_array_ptr, CdiLogComponent* component_ptr) {
    bool ret = false;

    for (int i=0; i < kLogComponentLast; i++) {
        if (*component_ptr == log_component_array_ptr[i]) {
            ret = true;
            break;
        }
    }

    return ret;
}

/**
 * @brief This function parses the command-line test arguments for the log components and inserts the valid components
 * into the GlobalTestSettings structure.
 *
 * @param component_str Pointer to the log component that is to be searched in the log_component array.
 * @param log_component_array_ptr Pointer to the log component array maintained in global_test_settings.
 *
 * @return bool True if found in the array, false if not found in array.
 */
static bool GetLogComponents(const char* component_str, CdiLogComponent* log_component_array_ptr) {
    bool ret = true;

    // Get the string length of the component argument.
    int string_len = strlen(component_str);

    // Verify that there are contents in the string, and that the contents do not exceed the max allowable length.
    if (string_len < 1) {
        ret = false;
    } else if (string_len > MAX_CHARACTERS_LOG_COMPONENTS-1) {
        ret = false;
        TestConsoleLog(kLogError, "Invalid --log_component (-lc) arguments [%s]: Exceeds maximum number of characters "
                        "for this command. This indicates duplicates or incorrectly used arguments.", component_str);
    }

    // Parse the component string for valid component settings.
    if (ret) {
        char input_str_cpy[MAX_CHARACTERS_LOG_COMPONENTS];
        const CdiEnumStringKey* log_component_key_array = CdiUtilityKeyGetArray(kKeyLogComponent);

        // Make a copy of the component argument and parse for the ' ' separator character.
        CdiOsStrCpy(input_str_cpy, sizeof(input_str_cpy), component_str);
        char* entry_str = strtok(input_str_cpy, " ");

        // Automatically contains the Generic component, offset the array by 1 as a result.
        int i = 1;

        // Loop through the optarg entries, prepare for multiple entries, only increment if a valid component or a
        // non-duplicate is encountered.
        while (entry_str && i < kLogComponentLast) {

            // Search for existing component.
            CdiLogComponent component = CdiUtilityStringToEnumValue(log_component_key_array, entry_str);
            if (CDI_INVALID_ENUM_VALUE != (int)component) {

                // If the component does not already exist, add to the array.
                if (!LogComponentExists(log_component_array_ptr, &component)) {
                    log_component_array_ptr[i] = component;
                    i++;
                } else if (kLogComponentGeneric == component) {
                    TestConsoleLog(kLogWarning, "--log_component (-lc) argument [%s] is applied by default.",
                                    entry_str);
                } else {
                    TestConsoleLog(kLogWarning, "--log_component (-lc) argument [%s] is a duplicate entry.",
                                    entry_str);
                }
                entry_str = strtok(NULL, " ");
            } else {
                TestConsoleLog(kLogError, "Invalid --log_component (-lc) argument [%s]. See list of options in help "
                                          "message.", entry_str);
                ret = false;
                break;
            }
        }
    }

    return ret;
}

/**
 * @brief A helper function that takes in an AVM type setting chosen by the user via --avm_video/audio/anc/raw
 *   and sets the avm_data_type field in the test settings array, and also increments the number of types chosen.
 *   If the total types is greater than 1, then error out since we only allow one AVM type for now.
 *
 * @param stream_settings_ptr Pointer to the stream settings array.
 * @param avm_data_type The AVM data type chosen by the user.
 * @param avm_types_ptr Pointer to the AVM types counter.
 * @return True if only one AVM type has been chosen, false if more than one has been chosen.
 */
static bool AvmTypeSetAndIncrement(StreamSettings* const stream_settings_ptr,
                                   const CdiBaselineAvmPayloadType avm_data_type, int* avm_types_ptr)
{
    bool ret = true;

    if (*avm_types_ptr > 0) {
        TestConsoleLog(kLogError, "Only one of --avm_video (-vid), --avm_audio (-aud), or avm_anc (-anc) options can be "
                                  "used in a single AVM stream.");
        ret = false;
    }
    (*avm_types_ptr)++;
    stream_settings_ptr->avm_data_type = avm_data_type;

    return ret;
}

/**
 *  @brief Verify that stream identifiers are unique. Helper for VerifyTestSettings.
 *
 * @param test_settings_ptr A pointer to the test settings data structure for the current connection to be evaluated.
 *
 * @return true if stream identifiers are unique, false otherwise.
 */
static bool IsUniqueStreamIdentifiers(TestSettings* const test_settings_ptr) {
    bool is_unique = true;

    int n = test_settings_ptr->number_of_streams;
    int* stream_ids = CdiOsMemAllocZero(n * sizeof(int));
    assert(stream_ids);

    for (int i = 0; i < n; i++) {
        stream_ids[i] = test_settings_ptr->stream_settings[i].stream_id;
    }

    for (int i = 0; i < n; i++) {
        int stream_id = test_settings_ptr->stream_settings[i].stream_id;
        for (int j = 0; j < i; j++) {
            if (stream_id == stream_ids[j]) {
                TestConsoleLog(kLogError, "Stream identifier[%d] is used more than once.", stream_id);
                is_unique = false;
            }
        }
    }
    CdiOsMemFree(stream_ids);

    return is_unique;
}

/**
 * @brief After all settings for a given connection have been collected by the options parser, this function will check
 * that they are all legal, and error out if they are not.
 *
 * @param test_settings_ptr A pointer to the test settings data structure for the current connection to be evaluated.
 *
 * @return true if all test settings are valid and complete; false if there are argument errors.
 */
static bool VerifyTestSettings(TestSettings* const test_settings_ptr) {
    bool arg_error = false;
    const char* connection_name_str = test_settings_ptr->connection_name_str;

    // Check the thread core setting, and let the user know if it isn't specified.
    if (OPTARG_INVALID_CORE == test_settings_ptr->thread_core_num) {
        TestConsoleLog(kLogInfo, "Connection[%s]: The (optional) --core (-core) argument not specified, so this "
                                "connection will not be pinned to a core.", connection_name_str);
    }

    // Check the connection name.
    if ((NULL == test_settings_ptr->connection_name_str) || (0 == strlen(test_settings_ptr->connection_name_str))) {
        TestConsoleLog(kLogInfo, "Connection[%s]: The (optional) --connection_name (-name) argument not specified, one"
                       " will be automatically generated.\n For receive connections, the destination port will be used."
                       " For transmit connections, the destination IP address\n and destination port will be used.",
                       CdiGetEmptyStringIfNull(test_settings_ptr->connection_name_str));
    }

    // Check to make sure num_transactions is set.
    if (0 == test_settings_ptr->num_transactions) {
        TestConsoleLog(kLogInfo, "Connection[%s]: The --num_transactions (-tnum) option is either unspecified or set "
                                 "to 0. Setting test to run forever.", connection_name_str);
    }

    // Check to make sure the test rate is set.
    if (0 == test_settings_ptr->rate_numerator) {
        TestConsoleLog(kLogError, "Connection[%s]: The --rate (-rt) option is required.", connection_name_str);
        arg_error = true;
    }

    // Check to make sure the timeout is set, but default if it is not to using the rate value specified.
    if (!arg_error && 0 == test_settings_ptr->tx_timeout) {
        test_settings_ptr->tx_timeout = (1000000 * test_settings_ptr->rate_denominator) /
                                                   test_settings_ptr->rate_numerator;
        TestConsoleLog(kLogWarning, "Connection[%s]: The (optional) --tx_timeout (-to) option not specified, so "
                                    "defaulting to [%d] microseconds.", connection_name_str,
                                    test_settings_ptr->tx_timeout);
    }

    // Check the log file name.
    if ('\0' == GetGlobalTestSettings()->base_log_filename_str[0]) {
        TestConsoleLog(kLogInfo, "Connection[%s]: No --log argument given, logging to console.", connection_name_str);
    }

    // Check --tx (-tx) and --rx (-rx) options for validity and to decide what mode we're in. Make sure one and only one
    // of --tx or --rx options is set for this connection.
    if (test_settings_ptr->tx == test_settings_ptr->rx) {
        TestConsoleLog(kLogError, "Connection[%s]: You must use one (and only one) of the --tx (-tx) or --rx (-rx) "
                                  "options.", connection_name_str);
        arg_error = true;
    } else {
        if (test_settings_ptr->rx) {
            // Receiver.
            TestConsoleLog(kLogInfo, "Connection[%s]: The --rx (-rx) option used, so this connection is in RX mode.",
                                     connection_name_str);
            if (NULL != test_settings_ptr->remote_adapter_ip_str) {
                TestConsoleLog(kLogError, "Connection[%s]: The --remote_ip (-rip) option cannot be used in RX mode.",
                                          connection_name_str);
                arg_error = true;
            }
        } else if (test_settings_ptr->tx) {
            // Transmitter.
            TestConsoleLog(kLogInfo, "Connection[%s]: The --tx (-tx) option used, so this connection is in TX mode.",
                                     connection_name_str);
            // For single endpoint connections, make sure --remote_ip (-rip) option is provided.
            if (!test_settings_ptr->multiple_endpoints && test_settings_ptr->remote_adapter_ip_str == NULL) {
                TestConsoleLog(kLogError, "Connection[%s]: The --remote_ip (-rip) option is required.",
                               connection_name_str);
                arg_error = true;
            }
        }

        // For single endpoint connections, make sure --dest_port (-dpt) option is provided.
        if (!test_settings_ptr->multiple_endpoints && 0 == test_settings_ptr->dest_port) {
            TestConsoleLog(kLogError, "Connection[%s]: The --dest_port (-dpt) option is required and must be non-zero.",
                                      connection_name_str);
            arg_error = true;
        }
    }

    // Check the buffer type.
    if (CDI_INVALID_ENUM_VALUE == (int)test_settings_ptr->buffer_type) {
        TestConsoleLog(kLogWarning, "Connection[%s]: The (optional) --buffer_type (-bt) option not specified, so "
                                    "defaulting to SGL.", connection_name_str);
        test_settings_ptr->buffer_type = kCdiSgl;
    }

    if (0 == test_settings_ptr->number_of_streams) {
        TestConsoleLog(kLogError, "Connection[%s]: You must create at least one stream for this connection using the "
                                  "--new_stream (-S) option", connection_name_str);
        arg_error = true;
    }

    // Check for unique stream identifiers.
    if (!arg_error && kProtocolTypeAvm == test_settings_ptr->connection_protocol) {
        arg_error = !IsUniqueStreamIdentifiers(test_settings_ptr);
    }

    // Check options specified for each stream.
    for (int stream_index = 0; stream_index < test_settings_ptr->number_of_streams; stream_index++) {
        StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[stream_index];
        // Check that if we are in AVM mode, the user has selected (at least) one data type.
        if (kProtocolTypeAvm == test_settings_ptr->connection_protocol) {
            if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->avm_data_type) {
                TestConsoleLog(kLogError, "Connection[%s]: The connection protocol was set as [%s], so you must use "
                                          "--avm_video, --avm_audio, or --avm_anc to set the data type.",
                                          connection_name_str, CdiUtilityKeyEnumToString(kKeyConnectionProtocolType,
                                                         test_settings_ptr->connection_protocol));
                arg_error = true;
            }
            if (stream_settings_ptr->stream_id < 0) {
                TestConsoleLog(kLogError, "Connection[%s]: The --id (-id) argument is required and must be nonnegative "
                                          "for protocol type AVM", connection_name_str);
                arg_error = true;
            }
        } else {
            // Make sure the user does not set a data type if they are not using AVM mode.
            if (CDI_INVALID_ENUM_VALUE != (int)stream_settings_ptr->avm_data_type) {
                TestConsoleLog(kLogError, "Connection[%s]: The connection protocol was set as [%s], so you must NOT "
                                          "use --avm_video, --avm_audio, or --avm_anc to set the data type.",
                                          connection_name_str, CdiUtilityKeyEnumToString(kKeyConnectionProtocolType,
                                          test_settings_ptr->connection_protocol));
                arg_error = true;
            }
            if (CDI_INVALID_ENUM_VALUE != stream_settings_ptr->stream_id) {
                TestConsoleLog(kLogError, "Connection[%s]: The --id (-id) argument cannot be used with protocol type "
                                          "RAW.", connection_name_str);
                arg_error = true;
            }
        }

        // Check to make sure the payload size is set.
        if (0 == stream_settings_ptr->payload_size) {
            TestConsoleLog(kLogError, "Connection[%s]: The --payload_size (-psz) option is required.",
                                      connection_name_str);
            arg_error = true;
        }

        // Check the pattern type.
        if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->pattern_type) {
            if (NULL != stream_settings_ptr->file_read_str) {
                stream_settings_ptr->pattern_type = kTestPatternNone;
            } else if (test_settings_ptr->tx) {
                TestConsoleLog(kLogInfo, "Connection[%s]: In tx mode. No --file_read or --pattern (-pat) options were "
                                         "specified. Defaulting to --pattern INC.", connection_name_str);
                stream_settings_ptr->pattern_type = kTestPatternInc;
            } else {
                TestConsoleLog(kLogInfo,
                               "Connection[%s]: No --file_read or --pattern options were specified. Received data will "
                               "not be checked. Defaulting to --pattern NONE.", connection_name_str);
                stream_settings_ptr->pattern_type = kTestPatternNone;
            }
        // We error out if the --file_read option is used but a test pattern is specified.
        } else if ((NULL != stream_settings_ptr->file_read_str) &&
                   (kTestPatternNone != stream_settings_ptr->pattern_type)) {
            TestConsoleLog(kLogError, "Connection[%s]: A --pattern was set but --file_read option (-fr) was also used.",
                                      connection_name_str);
            arg_error = true;
        }

        // Check that the pattern start value is set, but default it if it is not.
        if (0 == stream_settings_ptr->pattern_start) {
            TestConsoleLog(kLogWarning, "Connection[%s]: The (optional) --pat_start (-pst) option not specified, so "
                                        "defaulting to 0.", connection_name_str);
            stream_settings_ptr->pattern_start = 0;
        }

        if (test_settings_ptr->multiple_endpoints) {
            // For connections that use multiple endpoints, ensure remote IP and dest port were set.
            if (NULL == stream_settings_ptr->remote_adapter_ip_str) {
                TestConsoleLog(kLogError,
                    "Connection[%s]: For --new_conns (-XS) connections, the --remote_ip (-rip) argument is required.",
                    connection_name_str);
                arg_error = true;
            }
            if (0 == stream_settings_ptr->dest_port) {
                TestConsoleLog(kLogError,
                    "Connection[%s]: For --new_conns (-XS) connections, the --dest_port (-dpt) argument is required "
                    "and cannot be 0.", connection_name_str);
                arg_error = true;
            }
        }
    }

    if (arg_error) {
        TestConsoleLog(kLogError, "Errors detected in command line options for connection[%s].", connection_name_str);
    }

    return !arg_error;
}

/**
 * Use GetOpt to parse through the command line options looking for help commands, and run them if they are found.
 *
 * @param argc The system command line argument count variable.
 * @param argv_ptr The pointer to the system command line arguments array.
 * @param opt_ptr The pointer to the options structure that contains the available options.
 *
 * @return ProgramExecutionStatus to indicate whether the program should continue and if not, what exit status to have.
 */
static ProgramExecutionStatus ParseHelpOptions(int argc, const char** argv_ptr, OptArg* opt_ptr)
{
    ProgramExecutionStatus rv = kProgramExecutionStatusContinue;
    int opt_index = 1;
    while ((kProgramExecutionStatusContinue == rv) && (opt_index < argc)) {
        if (!GetOpt(argc, argv_ptr, &opt_index, my_options, opt_ptr)) {
            rv = kProgramExecutionStatusExitError;
        }
        switch ((TestOptionNames)opt_ptr->option_index) {
            case kTestOptionHelp:
                PrintUsage(my_options, false);
                rv = kProgramExecutionStatusExitOk;
                break;
            case kTestOptionHelpVideo:
                PrintUsageVideo(my_options, opt_ptr);
                rv = kProgramExecutionStatusExitOk;
                break;
            case kTestOptionHelpAudio:
                PrintUsageAudio(my_options, opt_ptr);
                rv = kProgramExecutionStatusExitOk;
                break;
            case kTestOptionHelpRiff:
                PrintRiffHelp(opt_ptr);
                rv = kProgramExecutionStatusExitOk;
                break;
            case kTestOptionHelpStats:
                PrintStatsHelp(my_options);
                rv = kProgramExecutionStatusExitOk;
                break;
            case kTestOptionVersion:
                TestConsoleLog(kLogInfo, "CDI SDK Version: %d.%d.%d", CDI_SDK_VERSION, CDI_SDK_MAJOR_VERSION,
                               CDI_SDK_MINOR_VERSION);
                rv = kProgramExecutionStatusExitOk;
                break;
            default:
                // Add do-nothing default statement to keep compiler from complaining about not enumerating all cases.
                break;
        }
    }
    return rv;
}

/**
 * Process a "--log" or a "--logs" command line option, setting the single connection log file flag and make a copy of
 * the log filename in the global test settings.
 *
 * @param is_single_file true if using the "--log" command line option (single log file), otherwise use false.
 * @param filename_str Pointer to log filename string to save.
 *
 * Return true if success, otherwise the log filename has already been set and false is returned.
 */
static bool ProcessLogFilenameOption(bool is_single_file, const char* filename_str)
{
    bool ret = true;
    GlobalTestSettings* settings_ptr = GetGlobalTestSettings();
    settings_ptr->use_single_connection_log_file = is_single_file;

    if ('\0' == settings_ptr->base_log_filename_str[0]) {
        // Copy the filename string to the global test settings.
        CdiOsStrCpy(settings_ptr->base_log_filename_str, sizeof(settings_ptr->base_log_filename_str), filename_str);
        if (settings_ptr->base_log_method != kLogMethodCallback) {
            settings_ptr->base_log_method = kLogMethodFile;
        }

        // Verify that the directory of the user-provided path exists.
        char filename[CDI_MAX_LOG_FILENAME_LENGTH] = {0};
        char directory[CDI_MAX_LOG_FILENAME_LENGTH] = {0};
        if (!CdiOsSplitPath(settings_ptr->base_log_filename_str, filename, CDI_MAX_LOG_FILENAME_LENGTH,
                            directory, CDI_MAX_LOG_FILENAME_LENGTH)) {
            CDI_LOG_THREAD(kLogError, "CdiOsSplitPath failed, filename or directory buffers are too small.");
            ret = false;
        }

        // Check if directory exists.
        if (ret) {
            // If directory is not current directory.
            if (directory[0] != '\0') {
                ret = CdiOsIsPathWriteable(directory);
            } else {
                ret = CdiOsIsPathWriteable("./");
            }
        }

    } else {
        TestConsoleLog(kLogError, "Can only use one of --log (-l) or --logs (-L) options across all connections.");
        ret = false;
    }

    return ret;
}

/**
 * Use GetOpt to parse through the command line options looking for global options, and assign them to global data
 * structures when found.
 *
 * @param argc The system command line argument count variable.
 * @param argv_ptr The pointer to the system command line arguments array.
 * @param opt_ptr The pointer to the options structure that contains the available options.
 *
 * @return False if any help options are found, or if errors are encountered.  True if no help options are found.
 */
static bool ParseGlobalOptions(int argc, const char** argv_ptr, OptArg* opt_ptr)  {
    int opt_index = 1;
    bool arg_error = false;
    GlobalTestSettings* global_test_settings_ptr = GetGlobalTestSettings();
    CdiAdapterData* adapter_data_ptr = &global_test_settings_ptr->adapter_data;
    const CdiEnumStringKey* log_level_key_array = CdiUtilityKeyGetArray(kKeyLogLevel);

    // Set default global options.
    global_test_settings_ptr->connection_timeout_seconds = CONNECTION_WAIT_TIMEOUT_SECONDS;

    while (!arg_error && (opt_index < argc)) {
        arg_error = !GetOpt(argc, argv_ptr, &opt_index, my_options, opt_ptr);
        switch ((TestOptionNames)opt_ptr->option_index) {
            case kTestOptionLogSingleFile:
                if (!ProcessLogFilenameOption(true, opt_ptr->args_array[0])) { // true= single file
                    arg_error = true;
                }
                break;
            case kTestOptionLogMultipleFiles:
                if (!ProcessLogFilenameOption(false, opt_ptr->args_array[0])) { // false= multiple files
                    arg_error = true;
                }
                break;
            case kTestOptionUseStderr:
                global_test_settings_ptr->use_stderr = true;
                break;
            case kTestOptionMultiWindowConsole:
                GetGlobalTestSettings()->use_multiwindow_console = true;
                break;
            case kTestOptionLocalIP:
                if (NULL != adapter_data_ptr->adapter_ip_addr_str) {
                    TestConsoleLog(kLogError, "Option --local_ip (-lip) already specified [%s] and can only be "
                                              "specified once.",
                                   adapter_data_ptr->adapter_ip_addr_str);
                    arg_error = true;
                } else {
                    adapter_data_ptr->adapter_ip_addr_str = opt_ptr->args_array[0];
                    if (!IsIPAddrValid(opt_ptr->args_array[0])) {
                        TestConsoleLog(kLogError, "Invalid --local_ip (-lip) argument [%s].", opt_ptr->args_array[0]);
                        arg_error = true;
                    }
                }
                break;
            case kTestOptionAdapter:
                if (CDI_INVALID_ENUM_VALUE != (int)adapter_data_ptr->adapter_type) {
                    TestConsoleLog(kLogError, "Option --adapter (-ad) already specified [%s] and can only be specified "
                                              "once.",
                                   CdiUtilityKeyEnumToString(kKeyAdapterType, adapter_data_ptr->adapter_type));
                    arg_error = true;
                } else {
                    adapter_data_ptr->adapter_type = CdiUtilityKeyStringToEnum(kKeyAdapterType, opt_ptr->args_array[0]);
                    if (CDI_INVALID_ENUM_VALUE == (int)adapter_data_ptr->adapter_type) {
                        TestConsoleLog(kLogError, "Invalid --adapter (-ad) argument [%s]. See list of options in help "
                                                  "message.",
                                 opt_ptr->args_array[0]);
                        arg_error = true;
                    }
                }
                break;
            case kTestOptionConnectionTimeout:
                if(!IsIntStringValid(opt_ptr->args_array[0], &global_test_settings_ptr->connection_timeout_seconds)) {
                    TestConsoleLog(kLogWarning, "Invalid --conn_timeout (-ct) argument [%s].", opt_ptr->args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionLogLevel:
                global_test_settings_ptr->log_level = CdiUtilityStringToEnumValue(log_level_key_array,
                                                                                  opt_ptr->args_array[0]);
                if (CDI_INVALID_ENUM_VALUE == (int)global_test_settings_ptr->log_level) {
                    TestConsoleLog(kLogError, "Invalid --log_level (-ll) argument [%s]. See list of options in help "
                                    "message.", opt_ptr->args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionLogComponent:
                if (!GetLogComponents(opt_ptr->args_array[0], global_test_settings_ptr->log_component)) {
                    arg_error = true;
                }
                break;
            case kTestOptionNumLoops:
                if (!IsBase10Number(opt_ptr->args_array[0], (int*)(&global_test_settings_ptr->num_loops))) {
                    TestConsoleLog(kLogError, "Invalid --num_loops (-nl) argument [%s].", opt_ptr->args_array[0]);
                    arg_error = true;
                }
                break;
#ifndef CDI_NO_MONITORING
            case kTestOptionStatsConfigCloudWatch:
                global_test_settings_ptr->use_cloudwatch = true;

                // Collect statistics parameters into stats_config structure. If "NULL" was specified set string to "\0".
                if (0 != CdiOsStrCaseCmp(opt_ptr->args_array[0], "NULL")) {
                    global_test_settings_ptr->cloudwatch_config.namespace_str = opt_ptr->args_array[0];
                }

                // If "NULL" was specified, then skip the value.
                if (0 != CdiOsStrCaseCmp(opt_ptr->args_array[1], "NULL")) {
                    global_test_settings_ptr->cloudwatch_config.region_str = opt_ptr->args_array[1];
                }

                // A dimension domain string must be provided.
                if (0 != CdiOsStrCaseCmp(opt_ptr->args_array[2], "NULL")) {
                    global_test_settings_ptr->cloudwatch_config.dimension_domain_str = opt_ptr->args_array[2];
                } else {
                    TestConsoleLog(kLogError, "CloudWatch dimension domain string cannot be NULL");
                    arg_error = true;
                }
                break;
#endif
            case kTestOptionNoPayloadUserData:
                global_test_settings_ptr->no_payload_user_data = true;
                break;

            default:
                // Add do-nothing default statement to keep compiler from complaining about not enumerating all cases.
                break;
        }
    }

    if (!arg_error) {
        // Verify that an adapter has been chosen, and if that adapter type also requires a local IP address, we check
        // for that too.
        if (CDI_INVALID_ENUM_VALUE == (int)adapter_data_ptr->adapter_type) {
            TestConsoleLog(kLogError, "The --adapter (-ad) option is required.");
            arg_error = true;
        } else if (NULL == adapter_data_ptr->adapter_ip_addr_str) {
            TestConsoleLog(kLogError, "The adapter type [%s] requires a local IP address via the --local_ip (-lip) "
                            "option.", CdiUtilityKeyEnumToString(kKeyAdapterType, adapter_data_ptr->adapter_type));
            arg_error = true;
        }
    }

    return !arg_error;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************
void LogComponentToString(const CdiEnumStringKey* key_array, char* log_component_str, CdiLogComponent* log_component_ptr)
{
    int msg_index = 0;
    int buffer_space_left = MAX_CHARACTERS_LOG_COMPONENTS - msg_index;
    msg_index += snprintf(&log_component_str[msg_index], buffer_space_left, "\"");

    for (int i = 0; CDI_INVALID_ENUM_VALUE != (int)log_component_ptr[i] && i < kLogComponentLast; i++) {
        buffer_space_left = MAX_CHARACTERS_LOG_COMPONENTS - msg_index;

        const char* const name_ptr = CdiUtilityEnumValueToString(key_array, log_component_ptr[i]);
        int buffer_space_requested = snprintf(&log_component_str[msg_index], buffer_space_left, "%s", name_ptr);

        // Be careful that we won't overrun the buffer.
        // Buffer space requested does not include null terminator, but buffer_space_left does.
        if (buffer_space_requested >= buffer_space_left) {
            TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
            break;
        } else {
            msg_index += buffer_space_requested;
        }

        // Write the character that come after this entry.
        buffer_space_left = MAX_CHARACTERS_LOG_COMPONENTS - msg_index;
        if (buffer_space_left < 0) {
            TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
            break;
        }

        // Separate items by " ", except for the last element of the array.
        if ((i + 1) < kLogComponentLast) {
            if (CDI_INVALID_ENUM_VALUE != (int)log_component_ptr[i+1]) {
                buffer_space_requested = snprintf(&log_component_str[msg_index], buffer_space_left, " ");
            } else {
                buffer_space_requested = snprintf(&log_component_str[msg_index], buffer_space_left, "\"");
            }
        } else {
            buffer_space_requested = snprintf(&log_component_str[msg_index], buffer_space_left, "\"");
        }

        // Verify we still have room left in the buffer.
        if (buffer_space_requested >= buffer_space_left) {
            TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
            break;
        } else {
            msg_index += buffer_space_requested;
        }
    }

    // Ensure the component string is terminated properly.
    log_component_str[MAX_CHARACTERS_LOG_COMPONENTS-1] = '\0';
}

void PrintTestSettings(const TestSettings* const test_settings_ptr, const int num_connections) {
    GlobalTestSettings* global_test_settings_ptr = GetGlobalTestSettings();
    CdiAdapterData* adapter_data_ptr = &global_test_settings_ptr->adapter_data;
    const CdiEnumStringKey* log_level_key_array = CdiUtilityKeyGetArray(kKeyLogLevel);
    const CdiEnumStringKey* log_component_key_array = CdiUtilityKeyGetArray(kKeyLogComponent);
    char log_components_array[MAX_CHARACTERS_LOG_COMPONENTS] = {0};
    LogComponentToString(log_component_key_array, log_components_array, global_test_settings_ptr->log_component);

    TestConsoleLog(kLogInfo, "");

    // Output global test settings.
    TestConsoleLog(kLogInfo, "Global Test Settings:");
    TestConsoleLog(kLogInfo, "    Test Loops       : %d", global_test_settings_ptr->num_loops);
    TestConsoleLog(kLogInfo, "    Payload user data: %s",
                    CdiUtilityBoolToString(!global_test_settings_ptr->no_payload_user_data));
    TestConsoleLog(kLogInfo, "    Multiple Logs    : %s",
                    CdiUtilityBoolToString(!global_test_settings_ptr->use_single_connection_log_file));
    TestConsoleLog(kLogInfo, "    Log Base Name    : %s",\
                    CdiGetEmptyStringIfNull(global_test_settings_ptr->base_log_filename_str));
    TestConsoleLog(kLogInfo, "    Log Callback     : %s",
                    CdiUtilityBoolToString(global_test_settings_ptr->base_log_method == kLogMethodCallback));
    TestConsoleLog(kLogInfo, "    Log Level        : %s",
                    CdiGetEmptyStringIfNull(CdiUtilityEnumValueToString(log_level_key_array,
                                        global_test_settings_ptr->log_level)));
    TestConsoleLog(kLogInfo, "    Log Component : %s", log_components_array);
#ifndef CDI_NO_MONITORING
    TestConsoleLog(kLogInfo, "    CloudWatch Enabled: %s",
                   CdiUtilityBoolToString(global_test_settings_ptr->use_cloudwatch));
    TestConsoleLog(kLogInfo, "        Namespace     : %s",
                   CdiGetEmptyStringIfNull(global_test_settings_ptr->cloudwatch_config.namespace_str));
    TestConsoleLog(kLogInfo, "        Region        : %s",
                   CdiGetEmptyStringIfNull(global_test_settings_ptr->cloudwatch_config.region_str));
    TestConsoleLog(kLogInfo, "     Dimension Domain : %s",
                   CdiGetEmptyStringIfNull(global_test_settings_ptr->cloudwatch_config.dimension_domain_str));
#endif
    // Output connection based test settings.
    TestConsoleLog(kLogInfo, "");
    for (int i = 0; i < num_connections; i++) {
        const char* connection_name_str = test_settings_ptr[i].connection_name_str;
        TestConsoleLog(kLogInfo, "Test Settings, Connection[%s], %s:", connection_name_str,
                       test_settings_ptr[i].tx ? "Tx" : "Rx");
        if (test_settings_ptr[i].tx) {
            TestConsoleLog(kLogInfo, "    Tx           : %s",
                           TestProtocolEnumToString(test_settings_ptr[i].connection_protocol));
        } else {
            TestConsoleLog(kLogInfo, "    Tx           : not enabled");
        }

        if (test_settings_ptr[i].rx) {
            TestConsoleLog(kLogInfo, "    Rx           : %s",
                           TestProtocolEnumToString(test_settings_ptr[i].connection_protocol));
        } else {
            TestConsoleLog(kLogInfo, "    Rx           : not enabled");
        }

        TestConsoleLog(kLogInfo, "    Conn Name    : %s",
                       CdiGetEmptyStringIfNull(test_settings_ptr[i].connection_name_str));
        TestConsoleLog(kLogInfo, "    Keep Alive   : %s", CdiUtilityBoolToString(test_settings_ptr[i].keep_alive));
        TestConsoleLog(kLogInfo, "    Adapter      : %s",
                       CdiGetEmptyStringIfNull(CdiUtilityKeyEnumToString(kKeyAdapterType,
                                                                      adapter_data_ptr->adapter_type)));
        TestConsoleLog(kLogInfo, "    Buff Type    : %s", CdiGetEmptyStringIfNull(CdiUtilityKeyEnumToString(kKeyBufferType,
                                                                               test_settings_ptr[i].buffer_type)));
        TestConsoleLog(kLogInfo, "    Local IP     : %s", CdiGetEmptyStringIfNull(adapter_data_ptr->adapter_ip_addr_str));
        if (!test_settings_ptr[i].multiple_endpoints) {
            TestConsoleLog(kLogInfo, "    Dest Port    : %d", test_settings_ptr[i].dest_port);
            TestConsoleLog(kLogInfo, "    Remote IP    : %s",
                        CdiGetEmptyStringIfNull(test_settings_ptr[i].remote_adapter_ip_str));
            TestConsoleLog(kLogInfo, "    Bind IP      : %s",
                        CdiGetEmptyStringIfNull(test_settings_ptr[i].bind_ip_addr_str));
        }
        if (test_settings_ptr[i].shared_thread_id > 0) {
            TestConsoleLog(kLogInfo, "    Shared Thread ID : %d", test_settings_ptr[i].shared_thread_id);
        }
        if (test_settings_ptr[i].thread_core_num == OPTARG_INVALID_CORE) {
            TestConsoleLog(kLogInfo, "    Core         : unpinned");
        } else {
            TestConsoleLog(kLogInfo, "    Core         : %d", test_settings_ptr[i].thread_core_num);
        }
        if (0 == test_settings_ptr[i].num_transactions) {
            TestConsoleLog(kLogInfo, "    Transactions : infinite.");
        } else {
            TestConsoleLog(kLogInfo, "    Transactions : %d", test_settings_ptr[i].num_transactions);
        }
        TestConsoleLog(kLogInfo, "    Rate         : %d/%d", test_settings_ptr[i].rate_numerator,
                       test_settings_ptr[i].rate_denominator);
        TestConsoleLog(kLogInfo, "    Tx Timeout   : %d", test_settings_ptr[i].tx_timeout);
        if (-1 == test_settings_ptr[i].rx_buffer_delay_ms) {
            TestConsoleLog(kLogInfo, "    Rx Buf Delay : -1 (enabled automatic default [%d]ms)", CDI_ENABLED_RX_BUFFER_DELAY_DEFAULT_MS);
        } else {
            TestConsoleLog(kLogInfo, "    Rx Buf Delay : %d", test_settings_ptr[i].rx_buffer_delay_ms);
        }
        TestConsoleLog(kLogInfo, "    Stats Period : %d", test_settings_ptr[i].stats_period_seconds);
        TestConsoleLog(kLogInfo, "    # of Streams : %d", test_settings_ptr[i].number_of_streams);
        for (int j=0; j<test_settings_ptr[i].number_of_streams; j++) {
            const StreamSettings* stream_settings_ptr = &test_settings_ptr[i].stream_settings[j];
            if (kProtocolTypeAvm == test_settings_ptr[i].connection_protocol) {
                // NOTE: Payload type is generic for all profile versions, so using NULL here for version.
                TestConsoleLog(kLogInfo, "    Stream[%d]    : AVM %s", j,
                              CdiAvmKeyEnumToString(kKeyAvmPayloadType, stream_settings_ptr->avm_data_type, NULL));
                TestConsoleLog(kLogInfo, "        Stream ID    : %d", stream_settings_ptr->stream_id);
                if (test_settings_ptr[i].multiple_endpoints) {
                    TestConsoleLog(kLogInfo, "        Dest Port    : %d", stream_settings_ptr->dest_port);
                    TestConsoleLog(kLogInfo, "        Remote IP    : %s",
                                CdiGetEmptyStringIfNull(stream_settings_ptr->remote_adapter_ip_str));
                }
                TestConsoleLog(kLogInfo, "        Payload Size : %d", stream_settings_ptr->payload_size);
                if (kCdiAvmVideo == stream_settings_ptr->avm_data_type) {
                    TestConsoleLog(kLogInfo, "        Config       : v%02d:%02d %dx%d, %s, Alpha %s, %s, Rate %d/%d, %s, %s, %s,",
                                   stream_settings_ptr->video_params.version.major,
                                   stream_settings_ptr->video_params.version.minor,
                                   stream_settings_ptr->video_params.width,
                                   stream_settings_ptr->video_params.height,
                                   CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType,
                                                        stream_settings_ptr->video_params.sampling, NULL),
                                   CdiAvmKeyEnumToString(kKeyAvmVideoAlphaChannelType,
                                                        stream_settings_ptr->video_params.alpha_channel, NULL),
                                   CdiAvmKeyEnumToString(kKeyAvmVideoBitDepthType,
                                                        stream_settings_ptr->video_params.depth, NULL),
                                   stream_settings_ptr->video_params.frame_rate_num,
                                   stream_settings_ptr->video_params.frame_rate_den,
                                   CdiAvmKeyEnumToString(kKeyAvmVideoColorimetryType,
                                                        stream_settings_ptr->video_params.colorimetry, NULL),
                                   stream_settings_ptr->video_params.interlace ? "Interlaced" : "Progressive",
                                   stream_settings_ptr->video_params.segmented ? "Segmented" : "Non-segmented");
                    TestConsoleLog(kLogInfo, "                       %s %s, PAR %dx%d, V Start/Length %d/%d, "
                                   "H Start/Length %d/%d",
                                   CdiAvmKeyEnumToString(kKeyAvmVideoTcsType,
                                                             stream_settings_ptr->video_params.tcs, NULL),
                                   CdiAvmKeyEnumToString(kKeyAvmVideoRangeType,
                                                             stream_settings_ptr->video_params.range, NULL),
                                   stream_settings_ptr->video_params.par_width,
                                   stream_settings_ptr->video_params.par_height,
                                   stream_settings_ptr->video_params.start_vertical_pos,
                                   stream_settings_ptr->video_params.vertical_size,
                                   stream_settings_ptr->video_params.start_horizontal_pos,
                                   stream_settings_ptr->video_params.horizontal_size);

                } else if (kCdiAvmAudio == stream_settings_ptr->avm_data_type) {
                    // Make a NUL terminated string that can be easily
                    char language_str[4] = { '\0', '\0', '\0', '\0' };
                    strncpy(language_str, stream_settings_ptr->audio_params.language, 3);
                    TestConsoleLog(kLogInfo, "        Config       : v%02d:%02d Grouping: %s, Rate %s, Language %s",
                                   stream_settings_ptr->video_params.version.major,
                                   stream_settings_ptr->video_params.version.minor,
                                   CdiAvmKeyEnumToString(kKeyAvmAudioChannelGroupingType,
                                                         stream_settings_ptr->audio_params.grouping, NULL),
                                   CdiAvmKeyEnumToString(kKeyAvmAudioSampleRateType,
                                                         stream_settings_ptr->audio_params.sample_rate_khz, NULL),
                                   language_str);
                }
                TestConsoleLog(kLogInfo, "        Config Skip  : %d", stream_settings_ptr->config_skip);
            } else {
                TestConsoleLog(kLogInfo, "    Stream[%d] : RAW", j);
                TestConsoleLog(kLogInfo, "        Payload Size : %d", stream_settings_ptr->payload_size);
            }
            TestConsoleLog(kLogInfo, "        Pattern      : %s",
                           CdiGetEmptyStringIfNull(TestPatternEnumToString(stream_settings_ptr->pattern_type)));
            TestConsoleLog(kLogInfo, "        Pat Start    : 0x%llx", stream_settings_ptr->pattern_start);
            TestConsoleLog(kLogInfo, "        File Read    : %s",
                           CdiGetEmptyStringIfNull(stream_settings_ptr->file_read_str));
            TestConsoleLog(kLogInfo, "        File Write   : %s",
                           CdiGetEmptyStringIfNull(stream_settings_ptr->file_write_str));
        }
        TestConsoleLog(kLogInfo, "");
    }
}

ProgramExecutionStatus GetArgs(int argc, const char** argv_ptr, TestSettings* test_settings_ptr,
                               int* num_connections_found)
{
    int opt_index = 1;
    int connection_index = 0;
    int stream_index = 0;
    bool first_new_connection = true;
    bool first_new_stream = true;
    bool arg_error = false;
    int check_val = 0;
    bool check_val_bool = 0;
    int avm_types = 0;
    OptArg opt = { 0 };

    // Load the options table my_options with the key-value arrays that define the choices available for each option.
    // These can't be loaded statically because they require a lookup function to access.
    InitOptionsTable();

    // Parse Help Options.  If it returns OptionError, it has either found an error and printed a usage message.
    // Since this function is the first one to walk the argv array, it will find any options irregularities (such as
    // missing arguments, or invalid options), and print a help usage message if it does. If OptionHelpPrinted was
    // returned, help was requested by the user. This should not be considered an error condition so the program exit
    // code should be 0.
    ProgramExecutionStatus status = ParseHelpOptions(argc, argv_ptr, &opt);
    if (status != kProgramExecutionStatusContinue) {
        return status;
    }

    // Parse global command line options.
    arg_error = !ParseGlobalOptions(argc, argv_ptr, &opt);

    // Parse the remaining connection-specific command line options:
    // Loop through all command line arguments and parse into test settings data structures. If option arguments do not
    // conform to input requirements, then exit the loop early.
    while (!arg_error && (opt_index < argc)) {
        bool got_global_option = false;
        int current_option_index = opt_index;
        arg_error |= !GetOpt(argc, argv_ptr, &opt_index, my_options, &opt);
        if (arg_error) { break; }
        StreamSettings* stream_settings_ptr = &test_settings_ptr[connection_index].stream_settings[stream_index];
        bool is_parsing_stream_option = (0 != test_settings_ptr[connection_index].number_of_streams);
        switch ((TestOptionNames)opt.option_index) {
            case kTestOptionStreamID:
                // Stream ID is only valid for protocol AVM.
                if (kProtocolTypeAvm != test_settings_ptr[connection_index].connection_protocol) {
                    TestConsoleLog(kLogError, "Invalid --id (-id) argument. Stream ID is only valid for AVM payloads.");
                    arg_error = true;
                }
                // Check that stream ID is less than 65536.
                if (!arg_error && !IsIntStringValid(opt.args_array[0], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --id (-id) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                if (!arg_error && (check_val > UINT16_MAX)) {
                    TestConsoleLog(kLogError, "Stream ID [%d] set with --id (-id) option must be less than [%d].",
                                   check_val, UINT16_MAX+1);
                    arg_error = true;
                } else {
                    test_settings_ptr[connection_index].stream_settings[stream_index].stream_id = check_val;
                }
                break;
            case kTestOptionConnectionName:
                CdiOsStrCpy(test_settings_ptr[connection_index].connection_name_str,
                            CDI_MAX_CONNECTION_NAME_STRING_LENGTH, opt.args_array[0]);
                break;
            case kTestOptionTransmit:
                test_settings_ptr[connection_index].tx = true;
                test_settings_ptr[connection_index].connection_protocol = TestProtocolStringToEnum(opt.args_array[0]);
                if (CDI_INVALID_ENUM_VALUE == (int)test_settings_ptr[connection_index].connection_protocol) {
                    TestConsoleLog(kLogError, "Invalid --tx (-tx) argument [%s]. See list of options in help message.",
                             opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionReceive:
                if (test_settings_ptr[connection_index].multiple_endpoints) {
                    TestConsoleLog(kLogError,
                        "For --new_conns (-XS) connections, the --rx (-rx) argument cannot be used. It is only valid "
                        " with --tx (-tx) argument.");
                    arg_error = true;
                }
                test_settings_ptr[connection_index].rx = true;
                test_settings_ptr[connection_index].connection_protocol = TestProtocolStringToEnum(opt.args_array[0]);
                if (CDI_INVALID_ENUM_VALUE == (int)test_settings_ptr[connection_index].connection_protocol) {
                    TestConsoleLog(kLogError, "Invalid --rx (-rx) argument [%s]. See list of options in help message.",
                             opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionKeepAlive:
                test_settings_ptr[connection_index].keep_alive = true;
                break;
            case kTestOptionBufferType:
                if (is_parsing_stream_option) {
                        TestConsoleLog(kLogError, "--buffer_type is not a stream option. Specify for a connection.");
                        arg_error = true;

                } else {
                    test_settings_ptr[connection_index].buffer_type = CdiUtilityKeyStringToEnum(kKeyBufferType,
                                                                                                opt.args_array[0]);
                    // Check if the user-supplied buffer type is valid.
                    if (CDI_INVALID_ENUM_VALUE == (int)test_settings_ptr[connection_index].buffer_type) {
                        TestConsoleLog(kLogError, "Invalid --buffer_type (-bt) argument [%s]. See list of options in help "
                                                  "message.", opt.args_array[0]);
                        arg_error = true;
                    }
                }
                break;
            case kTestOptionRemoteIP:
                if (!is_parsing_stream_option) {
                    test_settings_ptr[connection_index].remote_adapter_ip_str = opt.args_array[0];
                    if (test_settings_ptr[connection_index].multiple_endpoints) {
                        TestConsoleLog(kLogError,
                                       "The --remote_ip (-rip) argument cannot be used with --new_conns (-XS) option.");
                        arg_error = true;
                    }
                } else {
                    stream_settings_ptr->remote_adapter_ip_str = opt.args_array[0];
                    if (!test_settings_ptr[connection_index].multiple_endpoints) {
                        TestConsoleLog(kLogError,
                            "For --new_conn (-X) connections, the --remote_ip (-rip) argument cannot be used with "
                            "--new_stream (-S) option.");
                        arg_error = true;
                    }
                }
                if (!arg_error && !IsIPAddrValid(opt.args_array[0])) {
                    TestConsoleLog(kLogError, "The --remote_ip (-rip) argument [%s] is invalid.", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionBindIP:
                if (!is_parsing_stream_option) {
                    test_settings_ptr[connection_index].bind_ip_addr_str = opt.args_array[0];
                    if (test_settings_ptr[connection_index].multiple_endpoints) {
                        TestConsoleLog(kLogError,
                                       "The --bind_ip (-bip) argument cannot be used with --new_conns (-XS) option.");
                        arg_error = true;
                    }
                } else {
                    TestConsoleLog(kLogError,
                                    "The --bind_ip (-bip) argument cannot be used with --new_conns (-XS) option.");
                        arg_error = true;
                }
                if (!arg_error && !IsIPAddrValid(opt.args_array[0])) {
                    TestConsoleLog(kLogError, "The --bind_ip (-bip) argument [%s] is invalid.", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionDestPort:
            {
                int dest_port = 0;
                if (!IsIntStringValid(opt.args_array[0], &dest_port)) {
                    TestConsoleLog(kLogError, "Invalid --dest_port (-dpt) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                if (!arg_error && ((dest_port < kPortNumMin) || (dest_port > kPortNumMax))) {
                    TestConsoleLog(kLogError, "Invalid --dest_port (-dpt) argument [%d]. Valid range is between [%d] "
                                            "and [%d].", dest_port, kPortNumMin, kPortNumMax);
                    arg_error = true;
                }
                if (!is_parsing_stream_option) {
                    test_settings_ptr[connection_index].dest_port = dest_port;
                    if (test_settings_ptr[connection_index].multiple_endpoints) {
                        TestConsoleLog(kLogError,
                                       "The --dest_port (-dpt) argument cannot be used with --new_conns (-XS) option.");
                        arg_error = true;
                    }
                } else {
                    stream_settings_ptr->dest_port = dest_port;
                    if (!test_settings_ptr[connection_index].multiple_endpoints) {
                        TestConsoleLog(kLogError,
                            "For --new_conn (-X) connections, the --dest_port (-dpt) argument cannot be used with "
                            "--new_stream (-S) option.");
                        arg_error = true;
                    }
                }
            }
                break;
            case kTestOptionShareThread:
                // Check to make sure the thread ID value is a number.
                if (!IsBase10Number(opt.args_array[0], &test_settings_ptr[connection_index].shared_thread_id)) {
                    TestConsoleLog(kLogError, "Invalid --tid (-thread_id) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionCore:
                // Check to make sure the core value is a number.
                if (!IsBase10Number(opt.args_array[0], &test_settings_ptr[connection_index].thread_core_num)) {
                    TestConsoleLog(kLogError, "Invalid --core (-core) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionPayloadSize:
                // Check to make sure the payload size is a number.
                if (!IsBase10Number(opt.args_array[0], &stream_settings_ptr->payload_size)) {
                    TestConsoleLog(kLogError, "Invalid --payload_size (-psz) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionNumTransactions:
                // Check to make sure the test num_transactions is a number.
                if (!IsBase10Number(opt.args_array[0], &test_settings_ptr[connection_index].num_transactions)) {
                    TestConsoleLog(kLogError, "Invalid --num_transactions (-tnum) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionRate:
                // Check to make sure the test rate is a number.
                if (!IsRateValid(opt.args_array[0], &test_settings_ptr[connection_index].rate_numerator,
                                 &test_settings_ptr[connection_index].rate_denominator)) {
                    TestConsoleLog(kLogError, "Invalid --rate (-rt) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                } else {
                    // Rate is verified, set the frame rate in microseconds.
                    SetConnectionRatePeriods(&test_settings_ptr[connection_index]);
                }
                break;
            case kTestOptionAVMVideo:
            {
                // Collect video parameters into video_params data structure.
                int i = 0;
                // If an optional argument was provided, try to parse it first. NOTE: opt.num_args is the number of
                // options provided on command line, while my_options[opt.option_index].num_args is the number of
                // required options.
                stream_settings_ptr->video_params.version.major = 1;
                stream_settings_ptr->video_params.version.minor = 0;
                if (opt.num_args > my_options[opt.option_index].num_args) {
                    if (!CdiAvmParseBaselineVersionString(opt.args_array[0],
                                                          &stream_settings_ptr->video_params.version)) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'version'.",
                                    opt.args_array[0]);
                        arg_error = true;
                    }
                    i++;
                }

                if (!IsIntStringValid(opt.args_array[i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'width'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.width = (uint16_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'height'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.height = (uint16_t)check_val;
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.sampling =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoSamplingType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.sampling) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'sampling type'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.alpha_channel =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoAlphaChannelType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.alpha_channel) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'alpha channel type'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.depth =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoBitDepthType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.depth) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'bit depth'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'rate numerator'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.frame_rate_num = (uint32_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'rate denominator'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.frame_rate_den = (uint32_t)check_val;
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.colorimetry =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoColorimetryType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.colorimetry) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'colorimetry'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error && !IsBoolStringValid(opt.args_array[++i], &check_val_bool)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'interlace'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.interlace = (bool)check_val_bool;
                }

                if (!arg_error && !IsBoolStringValid(opt.args_array[++i], &check_val_bool)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'segmented'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.segmented = (bool)check_val_bool;
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.tcs =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoTcsType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.tcs) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'TCS'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error) {
                    stream_settings_ptr->video_params.range =
                        CdiAvmKeyStringToEnum(kKeyAvmVideoRangeType, opt.args_array[++i],
                                              &stream_settings_ptr->video_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->video_params.range) {
                        TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'encoding range'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    }
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'PAR width'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.par_width = (uint32_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'PAR height'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.par_height = (uint32_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'start vertical pos'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.start_vertical_pos = (uint16_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'vertical size'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.vertical_size = (uint16_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'start horizontal pos'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.start_horizontal_pos = (uint16_t)check_val;
                }

                if (!arg_error && !IsIntStringValid(opt.args_array[++i], &check_val)) {
                    TestConsoleLog(kLogError, "Invalid --avm_video (-vid) argument [%s] for 'horizontal size'.",
                                   opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->video_params.horizontal_size = (uint16_t)check_val;
                }

                arg_error |= !AvmTypeSetAndIncrement(stream_settings_ptr, kCdiAvmVideo, &avm_types);
            }
                break;
            case kTestOptionAVMAudio:
            {
                // Collect audio parameters into audio_params data structure.
                int i = 0;
                // If an optional argument was provided, try to parse it first. NOTE: opt.num_args is the number of
                // options provided on command line, while my_options[opt.option_index].num_args is the number of
                // required options.
                stream_settings_ptr->audio_params.version.major = 1;
                stream_settings_ptr->audio_params.version.minor = 0;
                if (opt.num_args > my_options[opt.option_index].num_args) {
                    if (!CdiAvmParseBaselineVersionString(opt.args_array[0],
                                                          &stream_settings_ptr->audio_params.version)) {
                        TestConsoleLog(kLogError, "Invalid --avm_audio (-aud) argument [%s] for 'version'.",
                                    opt.args_array[0]);
                        arg_error = true;
                    }
                    i++;
                }

                // Convert channel group to enum.
                CdiAvmAudioChannelGrouping group_val = CdiAvmKeyStringToEnum(kKeyAvmAudioChannelGroupingType,
                        opt.args_array[i],
                        &stream_settings_ptr->audio_params.version);
                if (CDI_INVALID_ENUM_VALUE == (int)group_val) {
                    TestConsoleLog(kLogError, "Invalid --avm_audio (-aud) argument [%s] for 'groupings'.  Run "
                                   "--help_audio for --avm_audio usage.", opt.args_array[i]);
                    arg_error = true;
                } else {
                    stream_settings_ptr->audio_params.grouping = group_val;
                }

                // Convert the sample rate string to an enum.
                if (!arg_error) {
                    stream_settings_ptr->audio_params.sample_rate_khz =
                        CdiAvmKeyStringToEnum(kKeyAvmAudioSampleRateType, opt.args_array[++i],
                                              &stream_settings_ptr->audio_params.version);
                    if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->audio_params.sample_rate_khz) {
                        TestConsoleLog(kLogError, "Invalid --avm_audio (-aud) argument [%s] for 'sample rate kHz'.",
                                       opt.args_array[i]);
                        arg_error = true;
                    } else {
                        // Translate the audio sample rate into numerical values to allow for calculating PTP timestamps
                        // from audio payload size.
                        if (!AudioSamplePeriodAttoseconds(stream_settings_ptr->audio_params.sample_rate_khz,
                                                          &stream_settings_ptr->audio_sample_period_attoseconds,
                                                          &stream_settings_ptr->rtp_sample_rate)) {
                            // If the sample rate is unspecified then fall back to incrementing one frame time per
                            // payload for rtp time.
                            stream_settings_ptr->do_not_use_audio_rtp_time = true;
                        }
                    }
                }

                // Extract the language code.
                if (!arg_error) {
                    // Zero pad the entire array of language code.
                    memset(stream_settings_ptr->audio_params.language, 0,
                           sizeof(stream_settings_ptr->audio_params.language));
                    if (0 != CdiOsStrCaseCmp(opt.args_array[++i], "none")) {
                        if (3 < strlen(opt.args_array[i])) {
                            TestConsoleLog(kLogError, "Invalid --avm_audio (-aud) argument [%s] for 'language code'.",
                                           opt.args_array[i]);
                            arg_error = true;
                        } else {
                            strncpy(stream_settings_ptr->audio_params.language, opt.args_array[i],
                                    sizeof(stream_settings_ptr->audio_params.language));
                        }
                    }  // else memset() did the work already
                }

                // Set the AVM type and increment a counter so we know how many AVM types the user entered.
                arg_error |= !AvmTypeSetAndIncrement(stream_settings_ptr, kCdiAvmAudio, &avm_types);
            }
                break;
            case kTestOptionAVMAncillary:
                // If an optional argument was provided, try to parse it first. NOTE: opt.num_args is the number of
                // options provided on command line, while my_options[opt.option_index].num_args is the number of
                // required options.
                stream_settings_ptr->ancillary_data_params.version.major = 1;
                stream_settings_ptr->ancillary_data_params.version.minor = 0;
                if (opt.num_args > my_options[opt.option_index].num_args) {
                    if (kCdiStatusOk != CdiAvmValidateBaselineVersionString(kCdiAvmAncillary, opt.args_array[0],
                                                          &stream_settings_ptr->ancillary_data_params.version)) {
                        TestConsoleLog(kLogError, "Invalid --avm_anc (-anc) version [%s].", opt.args_array[0]);
                        arg_error = true;
                    }
                }

                // Set the AVM type and increment a counter so we know how many AVM types the user entered.
                arg_error |= !AvmTypeSetAndIncrement(stream_settings_ptr, kCdiAvmAncillary, &avm_types);
                break;
            case kTestOptionConfigSkip:
                if (!IsIntStringValid(opt.args_array[0], &stream_settings_ptr->config_skip)) {
                    TestConsoleLog(kLogError, "Invalid --config_skip (-cskp) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionTxTimeout:
                // Check to make sure the timeout is a number.
                if (!IsBase10Number(opt.args_array[0], &test_settings_ptr[connection_index].tx_timeout)) {
                    TestConsoleLog(kLogError, "Invalid --tx_timeout (-to) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionRxBufferDelay:
                if (0 == CdiOsStrCaseCmp(opt.args_array[0], "automatic")) {
                    test_settings_ptr[connection_index].rx_buffer_delay_ms = -1; // -1= Use automatic SDK value.
                } else if (0 == CdiOsStrCaseCmp(opt.args_array[0], "disabled")) {
                    test_settings_ptr[connection_index].rx_buffer_delay_ms = 0; // 0= Disable.
                } else {
                    if (!IsBase10Number(opt.args_array[0], &test_settings_ptr[connection_index].rx_buffer_delay_ms)) {
                        TestConsoleLog(kLogError, "Invalid --rx_buffer_delay (-rbd) argument [%s].", opt.args_array[0]);
                        arg_error = true;
                    } else if (test_settings_ptr[connection_index].rx_buffer_delay_ms > CDI_MAXIMUM_RX_BUFFER_DELAY_MS) {
                        TestConsoleLog(kLogError, "Maximum [%d] --rx_buffer_delay (-rbd) argument exceeded.",
                                       CDI_MAXIMUM_RX_BUFFER_DELAY_MS);
                        arg_error = true;
                    }
                }
                break;
            case kTestOptionPattern:
                stream_settings_ptr->pattern_type = TestPatternStringToEnum(opt.args_array[0]);
                if (CDI_INVALID_ENUM_VALUE == (int)stream_settings_ptr->pattern_type) {
                    TestConsoleLog(kLogError, "Invalid --pattern (-pat) argument [%s]. See list of options in help "
                                   "message.", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionPatternStart:
                // Parse and check the pattern start value.
                if (!Is64BitBaseNNumber(opt.args_array[0], &stream_settings_ptr->pattern_start, 16)) {
                    TestConsoleLog(kLogError, "Invalid --pat_start (-pst) argument [%s].", opt.args_array[0]);
                    arg_error = true;
                }
                break;
            case kTestOptionUseRiffFile:
                stream_settings_ptr->riff_file = true;
                break;
            case kTestOptionFileRead:
                stream_settings_ptr->file_read_str = opt.args_array[0];
                break;
            case kTestOptionFileWrite:
                stream_settings_ptr->file_write_str = opt.args_array[0];
                break;
            case kTestOptionNewConnection:
            case kTestOptionNewConnectionMultipleEndpoints:
                // If -X or -XS is used as the last option, error out.
                if (argc == opt_index) {
                    TestConsoleLog(kLogError,
                                   "Option --new_conn (-X) or --new_conns (-XS) found as the last argument.");
                    arg_error = true;
                } else {
                    // Otherwise, initialize the connection variables and begin parsing options for this new connection.
                    if (!first_new_connection) {
                        // Increment the index so we are now collecting settings for the next connection.
                        connection_index++;
                        if (CDI_MAX_SIMULTANEOUS_CONNECTIONS == connection_index) {
                            TestConsoleLog(kLogError, "Exceeded maximum simultaneous connections[%d].",
                                           CDI_MAX_SIMULTANEOUS_CONNECTIONS);
                            arg_error = true;
                        }
                    }
                    // Initialize some variables for this connection.
                    avm_types = 0;
                    test_settings_ptr[connection_index].buffer_type = CDI_INVALID_ENUM_VALUE;
                    test_settings_ptr[connection_index].connection_protocol = CDI_INVALID_ENUM_VALUE;
                    test_settings_ptr[connection_index].shared_thread_id = OPTARG_INVALID_CORE;
                    test_settings_ptr[connection_index].thread_core_num = OPTARG_INVALID_CORE;
                    test_settings_ptr[connection_index].stats_period_seconds = REFRESH_STATS_PERIOD_SECONDS;
                    if (kTestOptionNewConnectionMultipleEndpoints == (TestOptionNames)opt.option_index) {
                        test_settings_ptr[connection_index].multiple_endpoints = true;
                    }
                }
                first_new_connection = false;
                first_new_stream = true;
                stream_index = 0;
                break;
            case kTestOptionNewStream:
                // If -S is used as the last option, error out.
                if (argc == opt_index) {
                    TestConsoleLog(kLogError, "Option --new_stream (-S) found as the last argument.");
                    arg_error = true;
                } else {
                    // Otherwise, if we get a -S we initialize the connection variables and begin parsing options for
                    // this new connection.
                    if (!first_new_stream) {
                        // Increment the index so we are now collecting settings for the next connection.
                        stream_index++;
                        // Use the Tx maximum connections for comparison because it will always be smaller than Rx.
                        if (stream_index == CDI_MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION) {
                            TestConsoleLog(kLogError, "Exceeded maximum simultaneous streams[%d].",
                                           CDI_MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION);
                            arg_error = true;
                        }
                    }
                    avm_types = 0;
                    test_settings_ptr[connection_index].number_of_streams = stream_index + 1;
                    test_settings_ptr[connection_index].stream_settings[stream_index].avm_data_type = CDI_INVALID_ENUM_VALUE;
                    test_settings_ptr[connection_index].stream_settings[stream_index].stream_id = CDI_INVALID_ENUM_VALUE;
                    test_settings_ptr[connection_index].stream_settings[stream_index].pattern_type = CDI_INVALID_ENUM_VALUE;
                }
                first_new_stream = false;
                break;
            case kTestOptionStatsConfigPeriod:
                // Collect statistics parameters into stats_config structure.
                if(!IsIntStringValid(opt.args_array[0], &test_settings_ptr[connection_index].stats_period_seconds)) {
                    TestConsoleLog(kLogError, "Invalid --stats_period (-stp) argument [%s] for 'period seconds'.",
                                   opt.args_array[0]);
                    arg_error = true;
                }
                break;
            // Specify all global options here for full case enumeration.  The compiler will complain if new cases are
            // added to the TestOptionNames typedef but not added here.
            case kTestOptionLogSingleFile:
            case kTestOptionLogMultipleFiles:
            case kTestOptionUseStderr:
            case kTestOptionMultiWindowConsole:
            case kTestOptionLocalIP:
            case kTestOptionAdapter:
            case kTestOptionHelp:
            case kTestOptionHelpVideo:
            case kTestOptionHelpAudio:
            case kTestOptionHelpRiff:
            case kTestOptionHelpStats:
            case kTestOptionVersion:
            case kTestOptionLogComponent:
            case kTestOptionConnectionTimeout:
            case kTestOptionLogLevel:
            case kTestOptionNumLoops:
#ifndef CDI_NO_MONITORING
            case kTestOptionStatsConfigCloudWatch:
#endif
            case kTestOptionNoPayloadUserData:
                got_global_option = true;
        }
        if (!got_global_option && first_new_connection) {
            // Make sure no non-global options were specified before we get here.
            TestConsoleLog(kLogError,
                "You must specify --new_conn (-X) or --new_conns (-XS) options before any connection-specific "
                "options[%s].", argv_ptr[current_option_index]);
            arg_error = true;
        }
    }

    // Make sure the user specified at least one connection.
    if (!arg_error && first_new_connection) {
        TestConsoleLog(kLogError, "You must specify at least one connection using the --new_conn (-X) or --new_conns "
                       "(-XS) options.");
        arg_error = true;
    }

    // Now verify that all test settings entered conform to input requirements.
    if (!arg_error) {
        // Loop through test settings and verify connection-specific test settings for all connections.
        for (int i=0; i<=connection_index; i++) {
            arg_error = !VerifyTestSettings(&test_settings_ptr[i]);
        }
    }

    // Pass back how many connections we found.
    *num_connections_found = connection_index + 1; // convert from zero-based to one-based

    return arg_error ? kProgramExecutionStatusExitError : kProgramExecutionStatusContinue;
}
