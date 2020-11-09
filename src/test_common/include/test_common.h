// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_common.c.
 */

#ifndef TEST_COMMON_H__
#define TEST_COMMON_H__

#include <stdbool.h>

#include "cdi_logger_api.h"
#include "cdi_log_enums.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Forward reference of structure to create pointers later.
typedef struct CommandLineState CommandLineState;
/// Forward reference of structure to create pointers later.
typedef struct CommandLineState* CommandLineHandle;

/// @brief How often to update stdout with payload progress.
#define PAYLOAD_PROGRESS_UPDATE_FREQUENCY   (60)

/// @brief Default value for protocol type.
#define DEFAULT_PROTOCOL_TYPE               (kTestProtocolRaw)

/// @brief Default number of transactions.
#define DEFAULT_NUM_TRANSACTIONS            (1000)

/// @brief Default payload size.
#define DEFAULT_PAYLOAD_SIZE                (5184000)

/**
 * Enum for connection protocol types.
 */
typedef enum {
    kTestProtocolRaw, ///< Raw connection
    kTestProtocolAvm, ///< Audio, Video and Metadata (AVM) connection
} TestConnectionProtocolType;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Convert a string to an integer.
 *
 * @param str Pointer to string to convert.
 * @param num_ptr Address where to write converted integer value.
 * @param ret_end_str Optional address where to write pointer to string after last character parsed.
 *
 * @return true if successful, otherwise false.
 */
bool TestStringToInt(const char* str, int* num_ptr, char** ret_end_str);

/**
 * Create an instance of a command line parser.
 *
 * @param argc_ptr Pointer to number of command line arguments.
 * @param argv_ptr Pointer pointer to array of pointers to command line arguments.
 * @param ret_handle Address where to write returned handle of parser.
 *
 * @return true if successful, otherwise false.
 */
bool TestCommandLineParserCreate(int* argc_ptr, const char*** argv_ptr, CommandLineHandle* ret_handle);

/**
 * Destroy an instance of the command line parser.
 *
 * @param handle Instance handle returned from TestCommandLineParserCreate().
 */
void TestCommandLineParserDestroy(CommandLineHandle handle);

#ifdef USE_CONSOLE_LOGGER
extern void TestConsoleLog(CdiLogLevel log_level, const char* format_str, ...);
#else
/**
 * Not using the console logger, so implement this API here so it can be used from within test_common.c and test
 * applications that use this file.
 *
 * @param log_level The log level for this message.
 * @param format_str Format string specifier.
 * @param ...  The remaining parameters contain a variable length list of arguments.
 */
static inline void TestConsoleLog(CdiLogLevel log_level, const char* format_str, ...)
{
    if (CdiLoggerIsEnabled(NULL, kLogComponentGeneric, log_level)) {
        va_list vars;

        va_start(vars, format_str);
        vprintf(format_str, vars); // Send to stdout.
        printf("\n\r");
        fflush(stdout);
        va_end(vars);
    }
}
#endif

#endif // TEST_COMMON_H__
