// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions that are common to test applications.
*/

#include "test_common.h"

#include <assert.h>
#include <stdlib.h> // For strtol().

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief The maximum number of command line tokens allowed in a text file when using the @ command line option.
#define MAX_CMD_TOKENS_IN_FILE        (10000)

/// @brief Define TestConsoleLog.
#define TestConsoleLog SimpleConsoleLog

/**
 * Structure use to hold command line state data.
 *
 */
struct CommandLineState {
    const char* cmd_array[MAX_CMD_TOKENS_IN_FILE]; ///< Array of pointers to command line option strings.
    char* file_buffer_str;                         ///< Pointer to buffer that holds command line file data.
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief A helper function that takes in a string of command-line tokens derived from a file. It will parse the string
 *   and store the token substrings into an array of the format identical to the command-line parameter, argv_ptr.
 *
 * @param file_buffer_str The string of command-line tokens.
 * @param cmd_array Pointer to the resulting array of strings.
 * @return The number of elements in the resulting cmd_array.
 */
static int FormatFileString(char* file_buffer_str, const char** cmd_array)
{
    // Start parsing through the file_buffer_str, look for #, ", \n, \r, EOF.
    // Offset array to 1 to account for retaining initial argv_ptr[0]'s argument.
    int i = 1;
    bool skip_line = false;
    bool is_quoted = false;
    bool token_started = false;

    // Read string until '\0'.
    while (*file_buffer_str != '\0') {
        // If the skip_line flag is true, store nothing until a '\n'.
        if (skip_line) {
            if ('\n' == *file_buffer_str) {
                skip_line = false;
            }

        // If the is_quoted flag is true, continue reading until a terminating '"'.
        } else if (is_quoted) {
            if ('"' == *file_buffer_str) {
                *file_buffer_str = '\0';
                is_quoted = false;
            }

        // Arrived at a commented line, set the skip_line flag which forces to keep reading until a '\n' or '\r'.
        } else if ('#' == *file_buffer_str) {
            skip_line = true;

        // If in the middle of a token.
        } else {
            if (token_started) {
                // If a whitespace char is encountered, terminate the current token, replace whitespace with '\0'.
                if ((' ' == *file_buffer_str || '\n' == *file_buffer_str || '\r' == *file_buffer_str) && !is_quoted) {
                    *file_buffer_str = '\0';
                    token_started = false;
                }
                else if ('"' == *file_buffer_str && !is_quoted) {
                    is_quoted = true;
                }

            // Token has not started.
            } else {
                // If current char is not a whitespace char, store the address, set the token_started flag.
                if (' ' != *file_buffer_str && '\n' != *file_buffer_str && '\r' != *file_buffer_str) {
                    if ('"' == *file_buffer_str) {
                        is_quoted = true;
                    }
                    token_started = true;
                    if (i < MAX_CMD_TOKENS_IN_FILE) {
                        // If we are starting a quoted string, we want the first character of the stored string to be
                        // the character after the quote, so add 1.
                        if (is_quoted && (*file_buffer_str != '\0')) {
                            cmd_array[i++] = file_buffer_str + 1;
                        // Otherwise, if it is not a quoted string, just store the current character location as the
                        // start of the string.
                        } else {
                            cmd_array[i++] = file_buffer_str;
                        }
                    } else {
                        // Exit this loop since we have overrun the max buffer index.
                        break;
                    }
                }
            }
        }

        // Increment position in the string.
        file_buffer_str++;
    }

    return i;
}

/**
 * @brief A helper function that reads the contents of a file into a buffer.
 *
 * @param file_name_str A string that represents the file name.
 * @param file_handle The handle of the file to open/read.
 * @param file_buffer_str Pointer to a string that will hold the file contents.
 * @return True if successful, false if unsuccessful.
 */
static bool FileToString(const char* file_name_str, CdiFileID file_handle, char** file_buffer_str)
{
    bool ret = true;
    int64_t offset = 0;
    uint64_t file_size = 0;

    // Get file size from seek and tell.
    ret = CdiOsFSeek(file_handle, offset, SEEK_END);
    if (ret) {
        ret = CdiOsFTell(file_handle, &file_size);
    }

    // Reset file pointer.
    if (ret) {
        ret = CdiOsFSeek(file_handle, 0, SEEK_SET);
    }

    // If ret is false, we could not get the file size.
    if (!ret) {
        TestConsoleLog(kLogError, "Could not get file size for [%s].", file_name_str);
    }

    // If file size is 0, do not attempt to read and allocate memory.
    if ((0 == file_size) && ret) {
        TestConsoleLog(kLogError, "The file [%s] does not contain any information.", file_name_str);
        ret = false;
    }

    // If no errors during file size detection, and the file size is not 0.
    if (ret) {

        // Malloc a buffer which will be the contents of the file.
        *file_buffer_str = (char*)CdiOsMemAllocZero(file_size+1);

        // Read file into the allocated string buffer.
        uint32_t bytes_read = 0;
        ret = CdiOsRead(file_handle, *file_buffer_str, file_size, &bytes_read);

        // Verify the number of bytes read is the same as the file size.
        if (ret) {
            if(bytes_read != file_size) {
                TestConsoleLog(kLogError, "The number of bytes read does not match the file size of [%s].", file_name_str);
                ret = false;
            }
        } else {
            TestConsoleLog(kLogError, "Could not read the contents of file [%s] into buffer.", file_name_str);
        }
    }

    return ret;
}

/**
 * @brief This function attempts to take the file buffer and create and array of command strings.
 *   If successful, it will substitute the command-line parameters, argc and argv_ptr.
 *
 * @param file_name_str A string that represents the file name.
 * @param file_buffer_str Pointer to a string that holds the file contents.
 * @return True if successful, false if unsuccessful.
 */
static bool CopyFileContentsToBuffer(const char* file_name_str, char** file_buffer_str)
{
    bool ret = false;

    // Open the file.
    CdiFileID file_handle;
    ret = CdiOsOpenForRead(file_name_str, &file_handle);

    // If file exists, attempt to copy contents to a string.
    if (ret) {

        // Copy file contents to a string.
        ret = FileToString(file_name_str, file_handle, file_buffer_str);

        // Close the file.
        ret = (CdiOsClose(file_handle) && ret);

    } else {
        TestConsoleLog(kLogError, "File [%s] could not be opened for reading.", file_name_str);
    }

    return ret;
}

/**
 * @brief This function attempts to open and read file into a buffer. If the buffer contains valid command parameters,
 * it will substitute the command-line parameters, argc and argv_ptr.
 *
 * @param file_buffer_str Pointer to a string that will hold the file contents.
 * @param argc Pointer to the number of command-line arguments.
 * @param argv_ptr Pointer to the array of string from the command-line arguments.
 * @param cmd_array_ptr Pointer to the array of strings that will be used in place of the arguments in argv_ptr.
 *
 * @return true if successful, false if unsuccessful.
 */
static bool CommandsFromFile(char** file_buffer_str, const char** cmd_array_ptr, int *argc, const char*** argv_ptr)
{
    bool ret = false;

    // Move to second arg of the command-line parameters; first arg will have the exe location as its parameter.
    const char** arg_command_array = *argv_ptr;
    arg_command_array++;

    // Point to the string in the second command-line arg.
    const char* arg_command_string = *arg_command_array;

    // Move one char beyond '@' to point to file name.
    arg_command_string++;

    // Copy file contents into a buffer.
    ret = CopyFileContentsToBuffer(arg_command_string, file_buffer_str);

    // With a string copied from the file, create an array of strings of the commands.
    if (ret) {
        // Create array of strings to mimic argv_ptr.
        // Store argv_ptr[0] into the cmd_array for consistency.
        cmd_array_ptr[0] = **argv_ptr;
        *argc = FormatFileString(*file_buffer_str, cmd_array_ptr);

        // Check the number of returned arguments. If the number of arguments is 1, throw an error and
        // free the allocated memory. Else, assign the cmd_array to argv_ptr.
        if (1 == *argc) {
            TestConsoleLog(kLogError, "There are no valid arguments in the command file: [%s].", arg_command_string);
            ret = false;
        } else if (*argc >= MAX_CMD_TOKENS_IN_FILE) {
            TestConsoleLog(kLogError, "There are too many command line tokens in the command file [%s]. "
                                      "The maximum is [%d].",
                           arg_command_string, MAX_CMD_TOKENS_IN_FILE);
            ret = false;
        } else {
            *argv_ptr = cmd_array_ptr;
            ret = true;
        }
    }

    // If any failures are detected in this function, free the allocated memory.
    if (!ret && *file_buffer_str) {
        CdiOsMemFree(*file_buffer_str);
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void SimpleConsoleLog(CdiLogLevel log_level, const char* format_str, ...)
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

bool TestStringToInt(const char* str, int* num_ptr, char** ret_end_str)
{
    bool ret = true;
    char* end_str = NULL;
    *num_ptr = (int)strtol(str, &end_str, 10);

    if (str == end_str) {
        TestConsoleLog(kLogError, "Failed to convert[%s] to integer value.", str);
        ret = false;
    }

    if (ret_end_str) {
        *ret_end_str = end_str;
    }

    return ret;
}

bool TestCommandLineParserCreate(int* argc_ptr, const char*** argv_ptr, CommandLineHandle* ret_handle)
{
    bool ret = true;
    CommandLineState* state_ptr = NULL;

    // If argc is not 2, do not attempt to parse a file for command-line parameters. If a file is specified with valid
    // commands, overwrite argc and argv_ptr to use values generated by CommandsFromFile(). Check if file parsing is
    // enabled by looking for the '@' delimiter as the first character.
    if (2 == *argc_ptr && '@' == (*argv_ptr)[1][0]) {
        state_ptr = (CommandLineState*)CdiOsMemAllocZero(sizeof(CommandLineState));
        if (NULL == state_ptr) {
            TestConsoleLog(kLogError, "Failed to allocate memory.");
            ret = false;
        } else {
            ret = CommandsFromFile(&state_ptr->file_buffer_str, state_ptr->cmd_array, argc_ptr, argv_ptr);
            if (!ret) {
                CdiOsMemFree(state_ptr);
                state_ptr = NULL;
            }
        }
    }

    *ret_handle = (CommandLineHandle)state_ptr;

    return ret;
}

void TestCommandLineParserDestroy(CommandLineHandle handle)
{
    CommandLineState* state_ptr = (CommandLineState*)handle;
    if (handle) {
        if (state_ptr->file_buffer_str) {
            CdiOsMemFree(state_ptr->file_buffer_str);
        }
        CdiOsMemFree(state_ptr);
    }
}
