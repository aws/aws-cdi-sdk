// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in cdi_test.c.
 */

#ifndef CDI_TEST_H__
#define CDI_TEST_H__

#include "curses.h"
#include <stdbool.h>

#include "cdi_core_api.h"
#include "test_args.h"
#include "test_configuration.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Default log level (ie. kLogInfo, kLogDebug, etc).
#define DEFAULT_LOG_LEVEL           (kLogDebug)

/// @brief Default log component (ie. kLogComponentGeneric, kLogComponentPayloadConfig, etc).
#define DEFAULT_LOG_COMPONENT       (kLogComponentProbe)

/// @brief Macro for finding the number of entries in an array of strings.
#define ARRAY_OF_STR_SIZE(thisarray) sizeof(thisarray)/sizeof(thisarray[0])

extern CdiLoggerHandle test_app_logger_handle;

/// @brief Send the log message to application's log for the connection specified by "connection_info_ptr->app_file_log_handle".
#define TEST_LOG_CONNECTION(log_level, ...) \
    CdiLogger(connection_info_ptr->app_file_log_handle, kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, \
              __VA_ARGS__)

/// @brief The number of bytes in a test pattern word.
#define BYTES_PER_PATTERN_WORD  (sizeof(uint64_t))

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Return the pointer to the global test settings structure.
 *
 * @return The pointer to the global test settings structure.
 */
GlobalTestSettings* GetGlobalTestSettings(void);

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
bool CreateStringFromArray(const char* array_of_strings_ptr[], int num_entries, const char* separator_str,
                           char* concat_str, int concat_max_len);

/**
 * Print an array of strings in this form "[STRING1, STRING2, STRING3, ...]"
 *
 * @param   array_ptr  Pointer to array of strings.
 * @param   size       The number of strings in the array.
 */
void PrintArrayOfStrings(const char* array_ptr[], int size);

/**
 * Search through an array of strings and return true if a given string is found.
 *
 * @param   array_ptr  Pointer to array of strings.
 * @param   size       The number of strings in the array.
 * @param   str        The string we are looking for in array_ptr.
 * @param   index_ptr  Pointer to the index variable for where the string was found in the array.
 * @return             True if string is found; false if string is not found.
 */
bool IsStringInArray(const char* array_ptr[], int size, const char* str, int* index_ptr);

/**
 * Check a string to see if it is a base-10 number.
 * @param   str             The string we are checking to see if it represents a base-10 number.
 * @param   base10_num_ptr  The integer representation of the number string in str. Set to NULL if the return number in
 *                          base10_num_ptr is not needed.
 * @return                  True if string represents a base-10 number; false if string does not
 */
bool IsBase10Number(const char* str, int* base10_num_ptr);

/**
 * Check a string to see if it is a 32 bit base-N number.
 * @param   str            The string we are checking to see if it represents a base-N number.The integer representation
 *                         of the number string in str. Set to NULL if the return number in num_ptr is not needed.
 * @param   baseN_num_ptr  The integer representation of the number string in str. Set to NULL if the return number in
 * @param   base           The numerical base (N) to use for the compare.
 * @return                 True if string represents a base-N number; false if string does not
 */
bool IsBaseNNumber(const char* str, int* baseN_num_ptr, const int base);

/**
 * Check a string to see if it is a 64 bit base-N number.
 * @param   str            The string we are checking to see if it represents a base-N number.The integer representation
 *                         of the number string in str. Set to NULL if the return number in num_ptr is not needed.
 * @param   baseN_num_ptr  The 64 bit integer representation of the number string in str. Set to NULL if the return
 *                         number is not being used
 * @param   base           The numerical base (N) to use for the compare.
 * @return                 True if string represents a base-N number; false if string does not
 */
bool Is64BitBaseNNumber(const char* str, uint64_t* baseN_num_ptr, const int base);

#endif // CDI_TEST_H__
