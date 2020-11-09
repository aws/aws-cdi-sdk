// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions used for option parsing, originally designed for the cdi_test
 * program.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "optarg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cdi_logger_api.h"
#include "cdi_test.h"
#include "test_console.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Enum for labeling different command line arguments as the command line is parsed.
 */
typedef enum {
    kArgError, ///< An argument error was detected.
    kArgOnly,  ///< An orphaned argument has been found without a parent option.
    kOptShort, ///< A short option has been found on the command line.
    kOptLong   ///< A long option has been found on the command line.
} OptionTypes;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Search the user options array for a given option. The type (long/short) indicates which option array
 * to search (the long options or the short ones), and the len indicates how many characters to match.
 *
 * @param opt_str  The option we are going to search for in the user options array.
 * @param type The type of option we are searching for; long or short.
 * @param opt_array_ptr The user-defined options array we will search.
 * @param found_opt_ptr If a match is found, this is a structure describing the option short name and number of expected
 *                      arguments.
 * @return Return true if a match is found, otherwise return false.
 */
static bool SearchOptions(const char* opt_str, int type, const OptDef* opt_array_ptr, OptArg* found_opt_ptr)
{
    bool ret = false;
    int i;
    TestConsoleLog(kLogDebug, "Searching for the option [%s]", opt_str);

    // Loop through the user-defined options array and check either long or short options to see if we can find a
    // match.
    for (i = 0; opt_array_ptr[i].short_name_str; i++) // loop while short_name_str != 0
    {
        TestConsoleLog(kLogDebug, "Checking option [%s]", type == kOptLong ?
                 opt_array_ptr[i].long_name_str : opt_array_ptr[i].short_name_str);
        TestConsoleLog(kLogDebug, "Comparing [%s] with [%s]", kOptLong ?
                 opt_array_ptr[i].long_name_str : opt_array_ptr[i].short_name_str, opt_str);

        // If we are looking for a long option and we find it in the options array. It's possible for there to not be a
        // long name for an option, so account for that.
        if ((type == kOptLong) && (opt_array_ptr[i].long_name_str != NULL) &&
            (CdiOsStrCmp(opt_array_ptr[i].long_name_str, opt_str) == 0))
        {
            TestConsoleLog(kLogDebug, "Found match.");
            ret = true;
            break;
        }

        // If we are looking for a short option and we find it in the options array.
        if ((type == kOptShort) && (CdiOsStrCmp(opt_array_ptr[i].short_name_str, opt_str) == 0))
        {
            TestConsoleLog(kLogDebug, "Found match.");
            ret = true;
            break;
        }
    }

    if (ret) {
        found_opt_ptr->num_args = opt_array_ptr[i].num_args;
        CdiOsStrCpy(found_opt_ptr->short_name_str, sizeof(found_opt_ptr->short_name_str),
                    opt_array_ptr[i].short_name_str);
        found_opt_ptr->option_index = i;
        memset(found_opt_ptr->args_array, 0, sizeof(found_opt_ptr->args_array));
    }

    return ret;
}

/**
 * @brief Takes a command line argument and checks if it conforms to expected formatting and if it is a valid option or
 * argument. If it is formatted as an option, check the user-defined options array. If it is a valid user option, then
 * fill out the found_opt_ptr structure with name and number of arguments.
 *
 * @param arg_str The command line argument we want to check.
 * @param opt_array_ptr The user-defined options array we will search if we determine arg_str is a properly-formatted
   option.
 * @param found_opt_ptr If a match is found, this is a structure describing the option.
 * @param expecting_opt When true, we are expecting the arg_str input is a long or short option; when false, we are
   expecting it to be an argument.
 * @return Return 1 if we get an error; 0 if arg_str is valid.
 */
static int CheckArg(const char* arg_str, const OptDef* opt_array_ptr, OptArg* found_opt_ptr, bool expecting_opt)
{
    int opt_type = kArgError;
    const int opt_len = strlen(arg_str);
    const char* opt_str = arg_str;

    // We don't allow empty options.
    if (opt_len == 0) {
        return kArgError;
    }

    // If we are here, then there is at least one character in the string, so this access is safe.
    const char char1 = arg_str[0];

    // If this is not an option argument.
    if (char1 != '-') {
        if (expecting_opt)
            return kArgError;
        else
            return kArgOnly;
    }

    // If we are here, then we know that the first character is a dash.

    // If this is just a '-', illegal short option.
    if (opt_len < 2) {
        TestConsoleLog(kLogDebug, "ERROR: Illegal option [%s].", arg_str);
        return kArgError;
    }

    // If we are here, then there are at least two characters in the string, so this access is safe.
    const char char2 = arg_str[1];

    if (char2 != '-') {
        // If it is more than OPTARG_MAX_SHORT_OPTION_LENGTH letters, error out.
        if (opt_len > OPTARG_MAX_SHORT_OPTION_LENGTH+1) {  // one is added for the dash character
            TestConsoleLog(kLogInfo, "Invalid short option [%s]. Short options can only have [%d] letters.",
                           arg_str, OPTARG_MAX_SHORT_OPTION_LENGTH);
            return kArgError;
        }
        TestConsoleLog(kLogDebug, "This is a short option.");
        opt_type = kOptShort;
        opt_str += 1;
    } else {
        TestConsoleLog(kLogDebug, "This is a long option.");
        opt_type = kOptLong;
        opt_str += 2;
    }

    // Separate the '-' or '--' from the long option string.
    TestConsoleLog(kLogDebug, "arg is [%s]", arg_str);
    TestConsoleLog(kLogDebug, "option is [%s]", opt_str);

    // Now search for the option and return it in a struct if it exists or error out.
    if(!SearchOptions(opt_str, opt_type, opt_array_ptr, found_opt_ptr)) {
        TestConsoleLog(kLogError, "Unknown option [%s]", arg_str);
        return kArgError;
    }

    // Otherwise, if we can find the option in our structure, find out how many arguments we are supposed to
    // return.
    TestConsoleLog(kLogDebug, "Valid option [%s] found.", arg_str);
    return opt_type;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void PrintKeyArrayNames(const EnumStringKey* key_array, const int indent)
{
    // If key_array exists, then scan through it printing each name_str member.
    // Note that we comma separate the list and don't want to print ", " after the last member.
    // Watch for overrunning the buffer.
    if (key_array) {

        char msg_buffer[OPTARG_OPTION_ARRAY_MAX_CHAR_LEN] = { 0 };
        int msg_index = 0;
        bool status = true;

        // Print the indent.
        int buffer_space_left = sizeof(msg_buffer) - msg_index;
        if (buffer_space_left < 0) {
            TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
            status = false;
        } else {
            msg_index += snprintf(&msg_buffer[msg_index], buffer_space_left, "%*s", indent, "");
            // Print the opening bracket.
            buffer_space_left = sizeof(msg_buffer) - msg_index;
        }


        if (buffer_space_left >= 0 && status) {
            msg_index += snprintf(&msg_buffer[msg_index], buffer_space_left, "<");

            // For each member of key_array, print the name_str member.
            for (int i = 0; CDI_INVALID_ENUM_VALUE != key_array[i].enum_value; i++) {
                buffer_space_left = sizeof(msg_buffer) - msg_index;
                if (buffer_space_left < 0 || status == false) {
                    status = false;
                    break;
                }
                const char* const name_ptr = key_array[i].name_str;
                int buffer_space_requested = snprintf(&msg_buffer[msg_index], buffer_space_left, "%s", name_ptr);

                // Be careful that we won't overrun the buffer.
                // Buffer space requested does not include null terminator, but buffer_space_left does.
                if (buffer_space_requested >= buffer_space_left) {
                    TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
                    break;
                } else {
                    msg_index += buffer_space_requested;
                }

                // Write the character(s) that come after this entry. Terminating characters of this entry are either ", "
                // or ">", depending on if we are at the end or not.
                buffer_space_left = sizeof(msg_buffer) - msg_index;
                if (buffer_space_left < 0 || status == false) {
                    status = false;
                    break;
                }

                // Separate items by ", ", except for the last element of the array, before the NULL element.
                if (key_array[i + 1].name_str != NULL) {
                    buffer_space_requested = snprintf(&msg_buffer[msg_index], buffer_space_left, ", ");
                } else {
                    buffer_space_requested = snprintf(&msg_buffer[msg_index], buffer_space_left, ">");
                }
                if (buffer_space_requested >= buffer_space_left) {
                    TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
                    break;
                } else {
                    msg_index += buffer_space_requested;
                }
            }

            // Alert the user that the full message will not be able to be displayed.
            if (status == false) {
                TestConsoleLog(kLogError, "Failed to concatenate all entries in choices array.");
            }

            // Now write the buffer to the log.
            msg_buffer[OPTARG_OPTION_ARRAY_MAX_CHAR_LEN-1] = '\0';  // to be safe in case of overflow above
            TestConsoleLog(kLogInfo, msg_buffer);
        }
    }
}

void PrintOption(const OptDef* option_ptr)
{
    if (option_ptr) {
        //
        // We print the first line of each option as follows:
        //
        //     -s | --long            < options >     : Description
        // <--A-><-B-><--    C    --> <--    D    --> : <--  variable len     -->
        // Where:
        //   A = OPTARG_MAX_SHORT_OPTION_LENGTH+1
        //   B = 5
        //   C = OPTARG_MAX_OPTION_LENGTH
        //   D = OPTARG_MAX_ARG_STR_LENGTH
        //
        //
        // Multi-line descriptions after the first line are formatted as follows:
        //
        //                                              Description (continued after any newlines).
        // <--   OPTARG_USAGE_DESCRIPTION_INDENT+2  --><--  variable len     -->
        //

        // Print the option information, checking for line breaks in the description field and properly formatting new
        // lines for the help message. Copy string and parse the copy since the tokenizer modifies it.

        // Create a copy of the description field because string tokenizer (strtok) modifies the string it operates on,
        // and we don't want that.
        char desc_copy[OPTARG_MAX_DESCRIPTION_STRING_LEN];
        CdiOsStrCpy(desc_copy, sizeof(desc_copy), option_ptr->description_str);

        // Scan the description for new lines; on the first line print all the option info, and on following lines only
        // print the continuation of the description, indented to the start of the description field (see above).
        char* desc_ptr = strtok(desc_copy, "\n");
        bool first_line = true;
        while (desc_ptr != NULL) {
            if (first_line) {
                // Short options are hardcoded below to be no more than 4 spaces, based on the #define
                TestConsoleLog(kLogInfo, "%1s%-*s%5s%-*s%1s%-*s%3s%s",
                        "-",
                        OPTARG_MAX_SHORT_OPTION_LENGTH, option_ptr->short_name_str,
                        option_ptr->long_name_str == NULL ? "" : " | --",
                        OPTARG_MAX_OPTION_LENGTH, option_ptr->long_name_str == NULL ? "" : option_ptr->long_name_str,
                        " ",
                        OPTARG_MAX_ARG_STR_LENGTH, option_ptr->argument_str  == NULL ? "" : option_ptr->argument_str,
                        " : ",
                        desc_ptr); // required option, so it will not be NULL.
                first_line = false;
            } else {
                // Required option, so it will not be NULL.
                TestConsoleLog(kLogInfo, "%*s%s", OPTARG_USAGE_DESCRIPTION_INDENT+2, "", desc_ptr);
            }
            desc_ptr = strtok(NULL, "\n");
        }

        // On next line, if there is a non-null pointer to an arg choices array, then we print it.
        PrintKeyArrayNames(option_ptr->arg_choices_array_ptr, OPTARG_USAGE_DESCRIPTION_INDENT+2);
    }
}

void PrintUsage(const OptDef* opt_array_ptr, bool has_error)
{
    int i;

    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "Usage:");
    TestConsoleLog(kLogInfo, "");
    // We know the last entry in the table is all 0's, and also that short options are required for all options, so we
    // can loop on short option names until we hit 0 and be sure to get them all.
    for (i = 0; opt_array_ptr[i].short_name_str; i++) {
        PrintOption(&opt_array_ptr[i]);
    }

    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "== Using a File As a Command-line Argument ==");
    TestConsoleLog(kLogInfo, "%s %28s %s", "@file_name", "", ": Put command-line arguments into a file to easily manage different test profiles.");
    TestConsoleLog(kLogInfo, "%40s%s", "", ":   Use the @ character to use the file name. Ex: @cdi_cmd.txt.");
    TestConsoleLog(kLogInfo, "%40s%s", "", ":   Use the # character to comment lines within the file.");
    TestConsoleLog(kLogInfo, "");

    if (has_error) {
        TestConsoleLog(kLogError, "Error(s) occurred in command line parsing. Check your command line parameters.");
        TestConsoleLog(kLogInfo, "");
    }
}

bool GetOpt(int argc, const char* argv[], int* index_ptr, OptDef* opt_array_ptr, OptArg* this_opt_ptr)
{
    int last_opt_index;

    // If we still have args left in argv, then parse.
    if (*index_ptr < argc) {
        bool advance_index = true;
        // Parse the next option and any subsequent arguments if there isn't an error.
        if (CheckArg(argv[*index_ptr], opt_array_ptr, this_opt_ptr, true) != kArgError) {
            last_opt_index = *index_ptr;
            int thisarg;

            // Command line options that have 1 optional argument.
            int num_optional_args = 0;
            if (0 ==CdiOsStrCmp(argv[last_opt_index], "--avm_video") ||
                0 ==CdiOsStrCmp(argv[last_opt_index], "--avm_audio") ||
                0 ==CdiOsStrCmp(argv[last_opt_index], "--avm_anc") ||
                0 ==CdiOsStrCmp(argv[last_opt_index], "--help_video") ||
                0 ==CdiOsStrCmp(argv[last_opt_index], "--help_audio")) {
                num_optional_args = 1;
            }

            TestConsoleLog(kLogDebug, "Found option [%s] with [%d] arguments.", argv[last_opt_index],
                           this_opt_ptr->num_args);

            // Check to make sure we have the correct number of arguments.
            for (thisarg = 0; thisarg < this_opt_ptr->num_args + num_optional_args; thisarg++) {
                (*index_ptr)++;

                // Make sure we aren't at the end of the command line arguments, but are expecting more.
                if (*index_ptr == argc) {
                    if (thisarg >= this_opt_ptr->num_args) {
                        break;
                    }
                    TestConsoleLog(kLogError, "Option [%s] requires [%d] arguments.", argv[last_opt_index],
                             this_opt_ptr->num_args);
                    // Increment the index_ptr so that we can detect that this is not a normal exit by comparing
                    // argc and index_ptr.
                    (*index_ptr)++;
                    return false;
                }

                // If we are expecting an argument to an option, but got another option, then error out.
                OptArg temp_opt;
                if (CheckArg(argv[*index_ptr], opt_array_ptr, &temp_opt, false) != kArgOnly) {
                    // Don't error out of 1 or more optional arguments were not provided.
                    if (thisarg >= this_opt_ptr->num_args) {
                        advance_index = false; // We are at next option, so don't advance the index.
                        break;
                    }
                    TestConsoleLog(kLogInfo, "Got option [%s] when expecting argument to option [%s].",
                                   argv[*index_ptr], argv[last_opt_index]);
                    return false;
                } else {
                    // Otherwise, collect the argument into our this_opt_ptr struct.
                    this_opt_ptr->args_array[thisarg] = argv[*index_ptr];
                }

            }
            this_opt_ptr->num_args = thisarg; // Update actual number of arguments provided (some may be optional).
            for (thisarg = 0; thisarg < this_opt_ptr->num_args; thisarg++) {
                TestConsoleLog(kLogDebug, "arg [%d] for option [%s] is [%s]", thisarg, argv[last_opt_index],
                               this_opt_ptr->args_array[thisarg]);
            }
        } else {
            // unknown option
            return false;
        }
        if (advance_index) {
            (*index_ptr)++;
        }
    } else {
         // No more args to parse.
        return false;
    }

    return true;
}

