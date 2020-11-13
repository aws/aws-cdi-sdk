// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in optarg.c.
 */

#ifndef CDI_OPTARG_H__
#define CDI_OPTARG_H__

#include <stdbool.h>

#include "cdi_core_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief The maximum number of arguments allowed for any command line option.
#define OPTARG_MAX_OPTION_ARGS (20)

/// @brief The maximum length of any option string.
#define OPTARG_MAX_OPTION_LENGTH (15)

/// @brief The maximum length of any short option string.
#define OPTARG_MAX_SHORT_OPTION_LENGTH (4)

/// @brief The maximum length of any option argument string.
#define OPTARG_MAX_ARG_STR_LENGTH (16)

/// @brief The maximum length of any option description string.
#define OPTARG_MAX_DESCRIPTION_STRING_LEN (800)

/// @brief The maximum length of an IP address string.
#define OPTARG_MAX_IP_STRING_LEN (20)

/// @brief The maximum number of chars in an array containing command line choices (i.e. platforms_array).
#define OPTARG_OPTION_ARRAY_MAX_CHAR_LEN (400)

/// @brief The number of chars to indent the description field in the main usage message.
#define OPTARG_USAGE_DESCRIPTION_INDENT (1 + OPTARG_MAX_SHORT_OPTION_LENGTH + 5 + OPTARG_MAX_OPTION_LENGTH + 1 + OPTARG_MAX_ARG_STR_LENGTH + 3)

/// @brief The number of chars to indent the options list in the audio and video usage messages.
#define OPTARG_AVM_USAGE_LIST_INDENT (29)

/**
 * @brief A structure that is used by the user to define a single option. An array of these option structs is passed to
 * GetOpt() for command line parsing.
 */
typedef struct
{
    /// The short multi-character short name of the option (short opt).
    const char* short_name_str;
    /// The multiple-character long name of the option (long opt).
    const char* long_name_str;
    /// The number of expected arguments for the option.
    int num_args;
    /// A string that describes the arguments to the option.
    const char* argument_str;
    /// Pointer to array of strings of option argument choices.
    const EnumStringKey* arg_choices_array_ptr;
    /// A string that describes the function of the option.  Max length is OPTARG_MAX_DESCRIPTION_STRING_LEN.
    const char* description_str;
} OptDef;

/**
 * @brief A structure that describes a single command line option and its associated arguments.
 */
typedef struct
{
    /// The index of this option.
    int option_index;
    /// The short name of the option (short opt).
    char short_name_str[OPTARG_MAX_SHORT_OPTION_LENGTH];
    /// The number of arguments discovered for the option.
    int num_args;
    /// An array of all the arguments discovered for this option (up to OPTARG_MAX_OPTION_ARGS).
    const char* args_array[OPTARG_MAX_OPTION_ARGS];
} OptArg;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************
/**
 * Print all the name_str elements of a key-value array in the format: <key_array[0].name_str, key_array[1].name_str ... >
 *
 * @param key_array A key-value array to search for name_str.
 * @param indent The number of space to indent before printing the array of names from the name_str member of each key-value pair.
 */
void PrintKeyArrayNames(const EnumStringKey* key_array, const int indent);

/**
 * Print the usage message of a single option based on the user-defined usage options.
 *
 * @param option_ptr Pointer to a single user-defined option.
 */
void PrintOption(const OptDef* option_ptr);

/**
 * Print the usage message based on the user-defined usage options.
 *
 * @param opt_array_ptr Pointer to array of user-defined options.
 * @param has_error True if we got an error and wish to print an error statement after printing usage.
 */
void PrintUsage(const OptDef* opt_array_ptr, bool has_error);

/**
 * A user-facing function that takes in the argv system command line args array and an index and finds the next option
 * and its associated arguments, incrementing the index accordingly.  Returns false for any error condition or if the
 * end of the arguments array has been reached.  The contents of index_ptr are incremented as options and arguments
 * are processed and will be equal to argc on a normal exit after processing all options and arguments.  However,
 * if we hit the end of the argv array and are still expecting arguments to the last option, we will increment
 * the contents of index_ptr beyond argc so that the calling routine can detect that it was not a normal exit.
 *
 * @param argc The system command line argument count variable.
 * @param argv_ptr The pointer to the system command line arguments array.
 * @param index_ptr The pointer to the current argv index, which gets incremented by the function as options and arguments
 *               are retrieved.  Should match argc after all argv elements have been processed.
 * @param opt_array_ptr Pointer to array of user-defined options.
 * @param this_opt_ptr A structure describing the single option (and arguments) retrieved.
 * @return True for success; false for failure or if we are at the end of argv.
 */
bool GetOpt(int argc, const char* argv_ptr[], int* index_ptr, OptDef* opt_array_ptr, OptArg* this_opt_ptr);

#endif // CDI_OPTARG_H__
