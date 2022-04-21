// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for a logger and associated logs.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_logger_api.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include "cdi_core_api.h"
#include "cdi_os_api.h"
#include "list_api.h"
#include "singly_linked_list_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Forward declaration to create pointer to log state when used.
typedef struct CdiLogState CdiLogState;

#define MAX_LOG_TIME_STRING_LENGTH             (64) ///< Maximum length of log time string.
#define MAX_LOG_FILENAME_LENGTH                (1024) ///< Maximum length of log filename string.
#define MULTILINE_LOG_MESSAGE_BUFFER_GROW_SIZE (20*CDI_MAX_LOG_STRING_LENGTH) ///< Maximum grow length of log buffer.
//
/// @brief Forward declaration to create pointer to logger state when used.
typedef struct CdiLoggerState CdiLoggerState;
/**
 * @brief Structure used to hold state data for a single logger.
 */
struct CdiLoggerState {
    CdiLogLevel default_log_level; ///< Default log level.
};

/**
 * @brief Structure used to hold component state data for a single component.
 */
typedef struct {
    bool log_enable;        ///< If true, this component is enabled.
    CdiLogLevel log_level;  ///< Log level for this component
} ComponentStateData;

/**
 * @brief Structure used to hold state data that is unique to a callback log. Calback log CdiLogState instances that
 *        use the same log message callback address and user callback parameter will share a single instance of this
 *        data.
 */
typedef struct {
    CdiLogCallbackData cb_data; ///< Callback data.
    int usage_ref_count;        ///< Count of CdiLogState objects using this data.
} LogCallbackData;

/**
 * @brief Structure used to hold state data that is unique to a file log. File log CdiLogState instances that use the
 *        same filename will share a single instance of this data.
 */
typedef struct {
    char filename_str[MAX_LOG_FILENAME_LENGTH]; ///< Name of log file.
    CdiFileID file_handle;                      ///< Handle to log file.
    int usage_ref_count;                        ///< Count of CdiLogState objects using this data.
} LogFileData;

typedef struct CdiLogState CdiLogState;
/**
 * @brief Structure used to hold state data for a single log of any type (stdout, callback or file).
 */
struct CdiLogState {
    CdiListEntry list_entry; ///< Used so this object can be stored in a list.

    CdiLoggerState* logger_state_ptr; ///< Which logger this log is associated with.

    /// @brief Connection handle to associate with this log. If NULL, the global log is assumed.
    CdiConnectionHandle connection_handle;

    /// @brief Indicates which structure of the union is valid.
    CdiLogMethod log_method;
    union {
        /// @brief The internal state of the structure is not used if log_method is kLogMethodStdout. Since there is
        /// only one stdout resource, we always use "stdout_multiline_state" directly.

        /// @brief The internal state of the structure if log_method is kLogMethodCallback.
        LogCallbackData* callback_data_ptr;
        /// @brief The internal state of the structure if log_method is kLogMethodFile.
        LogFileData* file_data_ptr;
    };

    ComponentStateData component_state_array[kLogComponentLast]; ///< Array of component state data.
};

/**
 * @brief Structure used to hold a buffer for a multiline log message.
 */
struct CdiMultilineLogBufferState {
    CdiSinglyLinkedListEntry list_entry; ///< Used so this object can be stored in a list.
    char* buffer_ptr;                    ///< Pointer to log buffer.
    int buffer_size;                     ///< Size of log buffer.
    int current_write_index;             ///< Current write index in buffer
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// @brief Statically allocated mutex used to make initialization of logger data thread-safe.
static CdiStaticMutexType logger_context_mutex_lock = CDI_STATIC_MUTEX_INITIALIZER;

/// @brief Logger module initialization reference count. If zero, logger has not been initialized. If 1 or greater,
/// logger has been initialized.
static int initialization_ref_count = 0;

static CdiCsID log_state_list_lock = NULL;      ///< Lock used to protect multi-thread access to the log state list.
static CdiList log_state_list;                  ///< List of log state objects (CdiLogState).

static CdiLogHandle stdout_log_handle = NULL;   ///< stdout log handle.
static LogFileData stdout_log_file_data;        ///< stdout log file data.

static bool log_thread_data_valid = false;      ///< If true, log_thread_data is valid (it can be zero and be valid).
static CdiThreadData log_thread_data = 0;       ///< Data used to hold pointer to CdiLogState for each thread.

/// Lock used to protect multi-thread access to multiline_free_list.
static CdiCsID multiline_free_list_lock = NULL;

/// List of pointers to multiline free log line structures (CdiMultilineLogBufferState).
static CdiSinglyLinkedList multiline_free_list = { 0 };

/// Array of global, default component state data.
static ComponentStateData global_component_state_array[kLogComponentLast];

/// Enable output to stderr in addition to log files (if log files are enabled).
static bool stderr_enable = false;

/// Log level to output to stderr.
static CdiLogLevel stderr_log_level = kLogLast;

/// String length of this session's time-date.
static int time_string_length = -1;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Get the log handle to use. If the specified handle is NULL, check the global handle. If that handle is also NULL then
 * default to using the stdout log handle.
 *
 * @param handle Current Log handle.
 *
 * @return Log handle to use.
 */
static CdiLogHandle GetLogHandleToUse(CdiLogHandle handle)
{
    if (NULL == handle) {
        handle = CdiLogGlobalGet();
    }
    if (NULL == handle) {
        handle = stdout_log_handle;
    }
    return handle;
}

/**
 * Allocate memory for a log buffer. If the pointer to the buffer is currently NULL, a new buffer is created.
 * Otherwise the size of the existing buffer is increased by re-allocating memory and copying the existing data to the
 * new buffer.
 *
 * @param state_ptr
 *
 * @return bool True if successfully grew buffer, otherwise false.
 */
static bool LogBufferGrow(CdiMultilineLogBufferState* state_ptr)
{
    bool ret = true;

    if (NULL == state_ptr->buffer_ptr) {
        state_ptr->buffer_ptr = (char*)CdiOsMemAllocZero(MULTILINE_LOG_MESSAGE_BUFFER_GROW_SIZE);
        if (NULL == state_ptr->buffer_ptr) {
            ret = false;
        } else {
            state_ptr->buffer_size = MULTILINE_LOG_MESSAGE_BUFFER_GROW_SIZE;
        }
    } else {
        char* new_buffer_ptr = (char*)CdiOsMemAllocZero(state_ptr->buffer_size +
                               MULTILINE_LOG_MESSAGE_BUFFER_GROW_SIZE);
        if (NULL == new_buffer_ptr) {
            ret = false;
        } else {
            // Copy the existing buffer to the new location, free the old buffer and setup to use the new one.
            memcpy(new_buffer_ptr, state_ptr->buffer_ptr, state_ptr->buffer_size);
            CdiOsMemFree(state_ptr->buffer_ptr);
            state_ptr->buffer_ptr = new_buffer_ptr;
            state_ptr->buffer_size += MULTILINE_LOG_MESSAGE_BUFFER_GROW_SIZE;
        }
    }

    if (!ret) {
        // To prevent recursive logging, using stdout here.
        CDI_LOG_HANDLE(stdout_log_handle, kLogError, "Failed to grow memory for a multiline log message buffer");
    }

    return ret;
}

/**
 * Get a log buffer from the dynamic pool.
 *
 * @return CdiMultilineLogBufferState* Pointer to returned log buffer. If NULL, unable to allocate memory.
 */
static CdiMultilineLogBufferState* LogBufferGet(void)
{
    CdiOsCritSectionReserve(multiline_free_list_lock); // Lock access to multiline_free_list
    CdiMultilineLogBufferState* state_ptr = (CdiMultilineLogBufferState*)CdiSinglyLinkedListPopHead(&multiline_free_list);
    CdiOsCritSectionRelease(multiline_free_list_lock); // Unlock access to multiline_free_list

    if (state_ptr == NULL) {
        // Grow the list.
        state_ptr = (CdiMultilineLogBufferState*)CdiOsMemAllocZero(sizeof(CdiMultilineLogBufferState));
        if (NULL == state_ptr) {
            // To prevent recursive logging, using stdout here.
            CDI_LOG_HANDLE(stdout_log_handle, kLogError, "Failed to allocate memory for a new multiline log buffer.");
        } else if (!LogBufferGrow(state_ptr)) {
            CdiOsMemFree(state_ptr);
            state_ptr = NULL;
        }
    }

    if (state_ptr) {
        state_ptr->current_write_index = 0;
        if (state_ptr->buffer_ptr) {
            *state_ptr->buffer_ptr = '\0';
        }
    }

    return state_ptr;
}

/**
 * Return a log buffer to the dynamic pool.
 *
 * @param log_buffer_ptr Pointer to log buffer to return to pool.
 */
static void LogBufferPut(CdiMultilineLogBufferState* log_buffer_ptr)
{
    CdiOsCritSectionReserve(multiline_free_list_lock); // Lock access to multiline_free_list
    CdiSinglyLinkedListPushHead(&multiline_free_list, &log_buffer_ptr->list_entry);
    CdiOsCritSectionRelease(multiline_free_list_lock); // Unlock access to multiline_free_list
}

/**
 * Given a log handle, return
 *
 * @param handle Log handle.
 *
 * @return Pointer to usage_ref_count, or NULL.
 */
static inline int GetUsageRefCount(CdiLogHandle handle)
{
    int usage_ref_count = 0;

    if (handle) {
        switch (handle->log_method) {
        case kLogMethodStdout:
        case kLogMethodFile:
            // Stdout and file log methods both use the same type of reference count variable.
            usage_ref_count = handle->file_data_ptr->usage_ref_count;
            break;
        case kLogMethodCallback:
            usage_ref_count = handle->callback_data_ptr->usage_ref_count;
            break;
        default:
            assert(false);
        }
    }

    return usage_ref_count;
}

/**
 * Given a log handle, increment or decrement its usage reference count and return the new value. NOTE: Before calling
 * this function, the critical section called "log_state_list_lock" must be reserved.
 *
 * @param handle Log handle.
 * @param increment true=increment, false=decrement
 *
 * @return New reference count.
 */
static inline int IncDecUsageRefCount(CdiLogHandle handle, bool increment)
{
    int* usage_ref_count_ptr = NULL;

    if (handle) {
        switch (handle->log_method) {
        case kLogMethodStdout:
        case kLogMethodFile:
            // Stdout and file log methods both use the same type of reference count variable.
            usage_ref_count_ptr = &handle->file_data_ptr->usage_ref_count;
            break;
        case kLogMethodCallback:
            usage_ref_count_ptr = &handle->callback_data_ptr->usage_ref_count;
            break;
        default:
            assert(false);
        }
    }

    if (usage_ref_count_ptr) {
        increment ? (*usage_ref_count_ptr)++ : (*usage_ref_count_ptr)--;
    }

    return *usage_ref_count_ptr;
}

/**
 * Get next entry in the log state list.
 *
 * @param iterator_ptr Pointer to CdiLogState list iterator. Must have been initialized using CdiListIteratorInit().
 *
 * @return Pointer to next CdiLogState object, otherwise NULL.
 */
static CdiLogState* ListGetNextEntry(CdiListIterator* iterator_ptr)
{
    CdiListEntry* entry_ptr = CdiListIteratorGetNext(iterator_ptr);

    CdiLogState* ret_state_ptr = NULL;
    // If entry exists, get the pointer to the container object (CdiLogState).
    if (entry_ptr) {
        ret_state_ptr = CONTAINER_OF(entry_ptr, CdiLogState, list_entry);
    }

    return ret_state_ptr;
}

/**
 * Search the log list for a matching log callback. NOTE: The lock called "log_state_list_lock" must be reserved before
 * using this function.
 *
 * @param callback_data_ptr Pointer to log callback data to search for.
 *
 * @return If found, a pointer to the matching LogCallbackData structure is returned. Otherwise, NULL is returned.
 */
static LogCallbackData* SearchForExistingLogCallbackInList(const CdiLogCallbackData* callback_data_ptr)
{
    LogCallbackData* ret_callback_data_ptr = NULL;

    CdiListIterator list_iterator;
    CdiListIteratorInit(&log_state_list, &list_iterator);

    // Walk through the list.
    CdiLogState* state_ptr = NULL;
    while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
        // If we find the desired entry (callback address and parameter match), return it.
        if (kLogMethodCallback == state_ptr->log_method &&
            state_ptr->callback_data_ptr->cb_data.log_msg_cb_ptr == callback_data_ptr->log_msg_cb_ptr &&
            state_ptr->callback_data_ptr->cb_data.log_user_cb_param == callback_data_ptr->log_user_cb_param) {

            ret_callback_data_ptr = state_ptr->callback_data_ptr;
            break;
        }
    }

    return ret_callback_data_ptr;
}

/**
 * Search the log list for a matching log file. NOTE: The lock called "log_state_list_lock" must be reserved
 * before using this function.
 *
 * @param log_filename_str Pointer to log filename to search for.
 *
 * @return If found, a pointer to the matching LogFileData structure is returned. Otherwise, NULL is returned.
 */
static LogFileData* SearchForExistingLogFileInList(const char* log_filename_str) {
    LogFileData* ret_log_file_data_ptr = NULL;

    CdiListIterator list_iterator;
    CdiListIteratorInit(&log_state_list, &list_iterator);

    // Walk through the list.
    CdiLogState* state_ptr = NULL;
    while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
        // If we find the desired entry (file log method and filenames match), return it.
        if (kLogMethodFile == state_ptr->log_method &&
            0 == strcmp(state_ptr->file_data_ptr->filename_str, log_filename_str)) {
            ret_log_file_data_ptr = state_ptr->file_data_ptr;
            break;
        }
    }

    return ret_log_file_data_ptr;
}

/**
 * Create the common log data for a new CdiLogState instance.
 *
 * @param logger_handle Handle of logger.
 * @param con_handle Handle of connection to associate this log to. Use NULL if no connection.
 * @param log_method Method to use for the log (file, callback or stdout).
 * @param ret_log_handle_ptr Pointer to returned log handle.
 *
 * @return True if successful, otherwise false.
 */
static bool CreateCommonLog(CdiLoggerHandle logger_handle, CdiConnectionHandle con_handle, CdiLogMethod log_method,
                            CdiLogHandle* ret_log_handle_ptr)
{
    bool ret = true;

    CdiLogState* state_ptr = (CdiLogState*)CdiOsMemAllocZero(sizeof(CdiLogState));
    if (NULL == state_ptr) {
        return false;
    }

    if (ret) {
        state_ptr->logger_state_ptr = logger_handle;
        state_ptr->connection_handle = con_handle;
        state_ptr->log_method = log_method;

        // Set default log enable and level for each component.
        for (int i = 0; i < kLogComponentLast; i++) {
            state_ptr->component_state_array[i].log_enable = global_component_state_array[i].log_enable;
            if (logger_handle) {
                state_ptr->component_state_array[i].log_level = logger_handle->default_log_level;
            } else {
                state_ptr->component_state_array[i].log_level = global_component_state_array[i].log_level;
            }
        }
    }

    if (!ret) {
        CdiLoggerDestroyLog((CdiLogHandle)state_ptr);
        state_ptr = NULL;
    }

    *ret_log_handle_ptr = (CdiLogHandle)state_ptr;

    return ret;
}

/**
 * Append line ending and new '\0' to a log message string at the specified character offset. Assumes the size of the
 * string cannot exceed buffer_size characters in length. It will overwrite the last character if char_count is at the
 * end of the buffer.
 *
 * @param log_msg_str Pointer to log message string.
 * @param buffer_size Size of log message buffer in bytes.
 * @param char_count Offset to place the linefeed and new '\0'.
 *
 * @return Adjusted character count.
 */
static int AppendLineEnding(char* log_msg_str, int buffer_size, int char_count)
{
    // Insert line feed onto log string and replace string terminator character.
    if (char_count >= (buffer_size - 2)) {
        char_count = buffer_size-2;
    }
    log_msg_str[char_count++] = '\n';
    log_msg_str[char_count++] = '\0';

    return char_count;
}

/**
 * Write a single log message line to the specified log.
 *
 * @param dest_log_buffer_str Log handle. If NULL, stdout is used.
 * @param dest_buffer_size Size of destination buffer in bytes.
 * @param log_level log level of this log message.
 * @param multiline True if this message is part of a multiline message.
 * @param log_str Pointer to log message string to write.
 */
static int WriteLineToBuffer(char* dest_log_buffer_str, int dest_buffer_size, CdiLogLevel log_level, bool multiline,
                             char* log_str)
{
    int char_count = 0;

    if (!multiline) {
        // Put timestamp at start of final log message.
        char_count = CdiOsGetLocalTimeString(dest_log_buffer_str, dest_buffer_size);

        // Store the length of the formatted time string, it will remain consistent for this connection.
        if (time_string_length < 0) {
            time_string_length = char_count;
        }
    } else {
        // Not using a timestamp, so just put blank spaces to account for its size so the log message are column
        // aligned.
        if (time_string_length < 0) {
            time_string_length = CdiOsGetLocalTimeString(dest_log_buffer_str, dest_buffer_size);
        }
        char_count = time_string_length;
        memset(dest_log_buffer_str, ' ', CDI_MIN(time_string_length, dest_buffer_size));
    }

    // See if we need to add a log type message to the final log message string.
    const char* log_level_str = NULL;
    switch (log_level) {
        case kLogWarning:
            log_level_str = "WARNING: ";
            break;
        case kLogError:
            log_level_str = "ERROR: ";
            break;
        case kLogCritical:
            log_level_str = "CRITICAL ERROR: ";
            break;
        case kLogFatal:
            log_level_str = "FATAL ERROR: ";
            break;
        default:
            ;
    }

    if (log_level_str) {
        // Append a log level message to the final log message string.
        char_count += CdiOsStrCpy(dest_log_buffer_str + char_count, dest_buffer_size - char_count, log_level_str);
    }

    // Append the original log message to the final log message string.
    char_count += CdiOsStrCpy(dest_log_buffer_str + char_count, dest_buffer_size - char_count, log_str);

    // Insert new line onto log string and replace string terminator character.
    return AppendLineEnding(dest_log_buffer_str, dest_buffer_size, char_count);
}

/**
 * Write a log message with optional source code function name and line number to a buffer.
 *
 * @param handle Log handle.
 * @param function_name_str Pointer to name of function the log line is being generated from. If NULL, function name
 *                          and source code line number are not used.
 * @param line_number Source code line number the log message is being generated from.
 * @param format_str Pointer to string used for formatting the message.
 * @param vars Variable length list of arguments used to generate the log message from the format_str.
 * @param dest_log_msg_buffer_str Pointer to where to write the formatted log message to.
 *
 * @return Number of characters written to the buffer, including the string terminator '\0'.
 */
static int LogToBuffer(CdiLogHandle handle, const char* function_name_str, int line_number, const char* format_str,
                       va_list vars, char* dest_log_msg_buffer_str)
{
    handle = GetLogHandleToUse(handle);

    *dest_log_msg_buffer_str = '\0';
    int char_count = 0;

    if (kLogMethodCallback == handle->log_method) {
        // Using callback log. Format the log message string before invoking the user-registered callback.
        char_count = vsnprintf(dest_log_msg_buffer_str, CDI_MAX_LOG_STRING_LENGTH, format_str, vars);
        // Return value is negative if there was a formatting error.
        if (char_count < 0) {
            char_count = 0;
        }
    } else {
        // Using file log or stdout. Add function name and source line if the function name exists.
        if (function_name_str) {
            char_count = snprintf(dest_log_msg_buffer_str, CDI_MAX_LOG_STRING_LENGTH, "[%s:%d] ", function_name_str,
                                  line_number);
            // Return value is negative if there was a formatting error.
            if (char_count < 0) {
                char_count = 0;
            }
        }

        // Add formatted string to log message string.
        int vs_count = vsnprintf(dest_log_msg_buffer_str + char_count,
                                 CDI_MAX_LOG_STRING_LENGTH - char_count, format_str, vars);
        // Return value is negative if there was a formatting error.
        if (vs_count > 0) {
            char_count += vs_count;
        }
    }

    return char_count;
}

/**
 * @brief Send the log message to stderr if enabled.
 *
 * @param file_handle File handle.
 * @param log_level log level of this log message.
 * @param log_str Pointer to log message string to send to stderr.
 * @param char_count Number of characters in the message string, not including the terminating '\0'.
 */
static void OutputToFileHandle(CdiFileID file_handle, CdiLogLevel log_level, const char* log_str, int char_count)
{
    bool use_stderr = (stderr_enable && log_level <= stderr_log_level);

    // We want to use the OS API function here if we are writing to a file OR we are using stdout and not using stderr.
    // Basically, we don't want to output to both stdout and stderr.
    if ((CDI_STDOUT != file_handle) || (CDI_STDOUT == file_handle && !use_stderr)) {
        CdiOsWrite(file_handle, log_str, char_count); // -1 excludes terminating '\0'
    }

    if (use_stderr) {
        CdiOsWrite(CDI_STDERR, log_str, char_count); // Output the string to stderr.
    }
}

/**
 * Write a single log message line to the specified log.
 *
 * @param handle Log handle. If NULL, stdout is used.
 * @param log_level log level of this log message.
 * @param multiline True if this message is part of a multiline message.
 * @param log_str Pointer to log message string to write.
 */
static void WriteLineToLog(CdiLogHandle handle, CdiLogLevel log_level, bool multiline, char* log_str)
{
    CdiFileID file_handle;
    if (NULL == handle) {
        file_handle = CDI_STDOUT;
    } else {
        file_handle = handle->file_data_ptr->file_handle;
    }

    char final_log_str[CDI_MAX_LOG_STRING_LENGTH];
    int char_count = WriteLineToBuffer(final_log_str, sizeof(final_log_str), log_level, multiline, log_str);

    OutputToFileHandle(file_handle, log_level, final_log_str, char_count - 1); // -1 excludes terminating '\0'
}

/**
 * Invoke a user registered log callback function to send a log message.
 *
 * @param handle Log handle. If NULL, stdout is used.
 * @param log_level log level of this log message.
 * @param component Component that generated the message.
 * @param function_name_str Pointer to name of function that generated the message.
 * @param line_number Source code line number that generated the message.
 * @param line_count Number of lines in the message.
 * @param message_str Pointer to log message string to write. If it contains multiple lines, each line must be
 *                    terminated with a '\0'.
 */
static void InvokeLogCallback(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level,
                              const char* function_name_str, int line_number, int line_count, const char* message_str)
{
    assert(kLogMethodCallback == handle->log_method);

    CdiLogMessageCbData cb_data = {
        .component = component,
        .log_level = log_level,
        .source_code_function_name_ptr = function_name_str,
        .source_code_line_number = line_number,
        .line_count = line_count,
        .message_str = message_str,
    };

    if (NULL == handle) {
        cb_data.connection_handle = NULL;
        cb_data.log_user_cb_param = 0;
    } else {
        cb_data.connection_handle = handle->connection_handle;
        cb_data.log_user_cb_param = handle->callback_data_ptr->cb_data.log_user_cb_param;
    }

    (handle->callback_data_ptr->cb_data.log_msg_cb_ptr)(&cb_data);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool CdiLoggerInitialize(void)
{
    bool ret = true;

    CdiOsStaticMutexLock(logger_context_mutex_lock);

    if (0 == initialization_ref_count) {
        // Initialize log state list.
        CdiListInit(&log_state_list);

        if (NULL == log_state_list_lock) {
            // Create critical section.
            if (!CdiOsCritSectionCreate(&log_state_list_lock)) {
                ret = false;
            }
        }

        // Initialize multiline free list.
        CdiSinglyLinkedListInit(&multiline_free_list);

        if (NULL == multiline_free_list_lock) {
            // Create critical section.
            if (!CdiOsCritSectionCreate(&multiline_free_list_lock)) {
                ret = false;
            }
        }


        // Set global default log enable and level for each component.
        for (int i = 0; i < kLogComponentLast; i++) {
            global_component_state_array[i].log_enable = (kLogComponentGeneric == i);
            global_component_state_array[i].log_level = kLogInfo;
        }

        stdout_log_file_data.filename_str[0] = '\0';
        stdout_log_file_data.usage_ref_count = 0;
        stdout_log_file_data.file_handle = CDI_STDOUT;

        if (ret && NULL == stdout_log_handle) {
            // Create a stdout log without a connection.
            CdiLogMethodData log_method_data;

            log_method_data.log_method = kLogMethodStdout;

            ret = CdiLoggerCreateLog(NULL, NULL, &log_method_data, &stdout_log_handle);
        }

        if (ret) {
            if (CdiOsThreadAllocData(&log_thread_data)) {
                log_thread_data_valid = true;
                CdiLoggerThreadLogSet(stdout_log_handle);
            } else {
                ret = false;
            }
        }

        if (ret) {
            CdiOsUseLogger();
        } else {
            CdiLoggerShutdown(false); // Do normal shutdown.
        }
    }

    if (ret) {
        initialization_ref_count++;
    }

    CdiOsStaticMutexUnlock(logger_context_mutex_lock);

    return ret;
}

bool CdiLoggerCreate(CdiLogLevel default_log_level, CdiLoggerHandle* ret_logger_handle_ptr)
{
    bool ret = true;
    CdiLoggerState* logger_state_ptr = NULL;

    CdiOsStaticMutexLock(logger_context_mutex_lock);

    if (0 == initialization_ref_count) {
        ret = false;
    } else {
        logger_state_ptr = (CdiLoggerState*)CdiOsMemAllocZero(sizeof(CdiLoggerState));
        if (NULL == logger_state_ptr) {
            ret = false;
        } else {
            logger_state_ptr->default_log_level = default_log_level;
        }
    }

    *ret_logger_handle_ptr = (CdiLoggerHandle)logger_state_ptr;

    CdiOsStaticMutexUnlock(logger_context_mutex_lock);

    return ret;
}

bool CdiLoggerCreateLog(CdiLoggerHandle logger_handle, CdiConnectionHandle con_handle,
                        const CdiLogMethodData* log_method_data_ptr, CdiLogHandle* ret_log_handle_ptr)
{
    bool ret = true;

    CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the log state list.

    CdiLogState* state_ptr = NULL;
    // This is a new file, so create a new entry for it.
    ret = CreateCommonLog(logger_handle, con_handle, log_method_data_ptr->log_method, &state_ptr);

    if (ret) {
        switch (log_method_data_ptr->log_method) {
            case kLogMethodStdout:
                // For stdout, we share a single static instance of the static data at "stdout_log_file_data".
                state_ptr->file_data_ptr = &stdout_log_file_data;
                break;
            case kLogMethodCallback:
                // Look to see if we are already using the callback.
                state_ptr->callback_data_ptr = SearchForExistingLogCallbackInList(&log_method_data_ptr->callback_data);
                if (NULL == state_ptr->callback_data_ptr) {
                    // This is a new callback, so create a new structure for it.
                    state_ptr->callback_data_ptr = (LogCallbackData*)CdiOsMemAllocZero(sizeof(LogCallbackData));
                    if (NULL == state_ptr->callback_data_ptr) {
                        ret = false;
                    }
                    if (ret) {
                        // Save a copy of the callback data.
                        state_ptr->callback_data_ptr->cb_data = log_method_data_ptr->callback_data;
                    }
                }
                break;
            case kLogMethodFile:
                // Look to see if we are already using the file.
                state_ptr->file_data_ptr = SearchForExistingLogFileInList(log_method_data_ptr->log_filename_str);
                if (NULL == state_ptr->file_data_ptr) {
                    // This is a new file, so create a new LogFileData structure for it.
                    state_ptr->file_data_ptr = (LogFileData*)CdiOsMemAllocZero(sizeof(LogFileData));
                    if (NULL == state_ptr->file_data_ptr) {
                        ret = false;
                    }
                    if (ret) {
                        // Save a copy of the filename.
                        CdiOsStrCpy(state_ptr->file_data_ptr->filename_str,
                                    sizeof(state_ptr->file_data_ptr->filename_str),
                                    log_method_data_ptr->log_filename_str);

                        // Open the log file.
                        ret = CdiOsOpenForWrite(state_ptr->file_data_ptr->filename_str,
                                                &state_ptr->file_data_ptr->file_handle);
                    }
                }
                break;
            default:
                ret = false;
        }
    }

    if (ret) {
        // Add the structure to log state list.
        CdiListAddTail(&log_state_list, &state_ptr->list_entry);

        // Increment the file reference count so when it reaches zero, it can be destroyed.
        IncDecUsageRefCount(state_ptr, true); // true= increment
    }

    CdiOsCritSectionRelease(log_state_list_lock); // Done with the log state list.

    if (!ret) {
        CdiLoggerDestroyLog((CdiLogHandle)state_ptr);
        state_ptr = NULL;
    }

    *ret_log_handle_ptr = (CdiLogHandle)state_ptr;

    return ret;
}

bool CdiLoggerCreateFileLog(CdiLoggerHandle logger_handle, const char* filename_str, CdiLogHandle* ret_log_handle_ptr)
{
    // Create a file log without a related connection.
    CdiLogMethodData log_method_data;

    log_method_data.log_method = kLogMethodFile;
    log_method_data.log_filename_str = filename_str;

    return CdiLoggerCreateLog(logger_handle, NULL, &log_method_data, ret_log_handle_ptr);
}

void CdiLogger(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level, const char* function_name_str,
               int line_number, const char* format_str, ...)
{
    handle = GetLogHandleToUse(handle);

    // Check if logging is enabled.
    if (CdiLoggerIsEnabled(handle, component, log_level)) {
        char log_message_str[CDI_MAX_LOG_STRING_LENGTH];
        va_list vars;
        va_start(vars, format_str);
        LogToBuffer(handle, function_name_str, line_number, format_str, vars, log_message_str);
        va_end(vars);

        if (kLogMethodCallback == handle->log_method) {
            InvokeLogCallback(handle, component, log_level, function_name_str, line_number, 1, log_message_str);
        } else {
            // Using file log or stdout.
            WriteLineToLog(handle, log_level, false, log_message_str);
        }
    }
}

void CdiLoggerMultilineBegin(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level,
                             const char* function_name_str, int line_number, CdiLogMultilineState* state_ptr)
{
    handle = GetLogHandleToUse(handle);

    memset(state_ptr, 0, sizeof(*state_ptr)); // Clear all the state data

    if (NULL == function_name_str) {
        function_name_str = "";
        line_number = 0;
    }

    // Check if logging is enabled.
    if (CdiLoggerIsEnabled(handle, component, log_level)) {
        state_ptr->logging_enabled = true;
        state_ptr->log_handle = handle;
        state_ptr->component = component;
        state_ptr->log_level = log_level;

        CdiOsStrCpy(state_ptr->function_name_str, CDI_MAX_LOG_FUNCTION_NAME_STRING_LENGTH, function_name_str);
        state_ptr->line_number = line_number;
        state_ptr->buffer_state_ptr = LogBufferGet();
    }
}

void CdiLoggerMultiline(CdiLogMultilineState* state_ptr, const char* format_str, ...)
{
    // Check if logging is enabled.
    if (state_ptr->logging_enabled) {
        // Ensure we have enough space to add another log message.
        if (state_ptr->buffer_state_ptr->buffer_size - state_ptr->buffer_state_ptr->current_write_index <
            CDI_MAX_LOG_STRING_LENGTH) {
            if (!LogBufferGrow(state_ptr->buffer_state_ptr)) {
                return; // Error is logged in LogBufferGrow().
            }
        }

        char *dest_buffer_str = state_ptr->buffer_state_ptr->buffer_ptr +
                                state_ptr->buffer_state_ptr->current_write_index;
        va_list vars;
        va_start(vars, format_str);

        int char_count = 0;
        if (kLogMethodCallback == state_ptr->log_handle->log_method) {
            // Using callback log. Just write the raw message (no function name, line or timestamps).
            char_count = LogToBuffer(state_ptr->log_handle, NULL, 0, format_str, vars, dest_buffer_str);
            // Use the trailing '\0' as a line separator.
            char_count++;
        } else {
            // For file or stdout log.
            char log_message_str[CDI_MAX_LOG_STRING_LENGTH];

            if (0 == state_ptr->line_count) {
                // For first line, optionally generate function name and source code line number information.
                LogToBuffer(state_ptr->log_handle, state_ptr->function_name_str, state_ptr->line_number, format_str,
                            vars, log_message_str);

                // Then, format the line with a timestamp and log level string, writing it to the multiline buffer.
                // This will add a trailing linefeed. We don't want to include the trailing '\0' (so -1 on count).
                char_count = WriteLineToBuffer(dest_buffer_str, CDI_MAX_LOG_STRING_LENGTH, state_ptr->log_level, false,
                                               log_message_str) - 1;
            } else {
                // Not first line. Don't generate function name, source code line number or timestamp. This will add a
                // trailing linefeed. We don't want to include the trailing '\0' (so -1 on count).
                vsnprintf(log_message_str, CDI_MAX_LOG_STRING_LENGTH, format_str, vars);
                char_count = WriteLineToBuffer(dest_buffer_str, CDI_MAX_LOG_STRING_LENGTH, state_ptr->log_level, true,
                                               log_message_str) - 1;
            }
        }

        va_end(vars);

        state_ptr->buffer_state_ptr->current_write_index += char_count; // Update write index
        state_ptr->line_count++;
    }
}

char* CdiLoggerMultilineGetBuffer(CdiLogMultilineState* state_ptr)
{
    if (state_ptr->logging_enabled) {
        state_ptr->buffer_used = true; // Set flag so CdiLoggerMultilineEnd() won't generate duplicate output.
        return state_ptr->buffer_state_ptr->buffer_ptr;
    } else {
        return NULL;
    }
}

void CdiLoggerMultilineEnd(CdiLogMultilineState* state_ptr)
{
    // Check if logging is enabled.
    if (state_ptr->logging_enabled) {
        if (!state_ptr->buffer_used) {
            if (kLogMethodCallback == state_ptr->log_handle->log_method) {
                // Using callback log.
                InvokeLogCallback(state_ptr->log_handle, state_ptr->component, state_ptr->log_level,
                                state_ptr->function_name_str, state_ptr->line_number,
                                state_ptr->line_count, state_ptr->buffer_state_ptr->buffer_ptr);
            } else {
                // Using file or stdout log. We don't need to exclude the trailing '\0', since it is not included as part
                // of current_write_index (see logic in CdiLoggerMultiline).
                OutputToFileHandle(state_ptr->log_handle->file_data_ptr->file_handle, state_ptr->log_level,
                                state_ptr->buffer_state_ptr->buffer_ptr,
                                state_ptr->buffer_state_ptr->current_write_index);
            }
        }

        CdiOsMemFree(state_ptr->buffer_state_ptr->buffer_ptr);
        state_ptr->buffer_state_ptr->buffer_ptr = NULL;
        state_ptr->buffer_state_ptr->buffer_size = 0;
        // Put entry back into the free list.
        LogBufferPut(state_ptr->buffer_state_ptr);
    }
}

void CdiLoggerLogFromCallback(CdiLogHandle handle, const CdiLogMessageCbData* cb_data_ptr)
{
    // Currently, this function is normally only used by a test application to validate the callback log API function.
    // It is not optimized and contains memcpy's.
    assert(kLogMethodCallback != handle->log_method);

    if (CdiLoggerIsEnabled(handle, cb_data_ptr->component, cb_data_ptr->log_level)) {
        // We need to generate a single log message that contains an optional function name and line number for the
        // first line. Multiline messages need to have each line separated with a line ending character. This is all
        // handled by the Multiline API functions, so we will just use them.
        CdiLogMultilineState m_state;
        CdiLoggerMultilineBegin(handle, cb_data_ptr->component, cb_data_ptr->log_level,
                                cb_data_ptr->source_code_function_name_ptr, cb_data_ptr->source_code_line_number,
                                &m_state);
        if (m_state.buffer_state_ptr->buffer_ptr) {
            // Walk through each line and write to the new single log message buffer.
            const char* line_str = cb_data_ptr->message_str;
            for (int i = 0; i < cb_data_ptr->line_count; i++) {
                CdiLoggerMultiline(&m_state, line_str);
                line_str += strlen(line_str) + 1; // Advance pointer to byte just past end of the current string.
            }
            CdiLoggerMultilineEnd(&m_state);
        }
    }
}

bool CdiLoggerThreadLogSet(CdiLogHandle handle)
{
    if (log_thread_data_valid) {
        return CdiOsThreadSetData(log_thread_data, (void*)handle);
    }
    return false;
}

void CdiLoggerThreadLogUnset(void)
{
    if (log_thread_data) {
        CdiOsThreadSetData(log_thread_data, NULL);
    }
}

CdiLogHandle CdiLoggerThreadLogGet(void)
{
    CdiLogHandle log_handle = NULL;

    if (log_thread_data_valid) {
        CdiOsThreadGetData(log_thread_data, (void**)&log_handle);
    }

    return log_handle;
}

bool CdiLoggerIsEnabled(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level)
{
    if (component >= kLogComponentLast) {
        return false;
    }

    handle = GetLogHandleToUse(handle);

    if (NULL == handle) {
        return false; // Even stdout logger doesn't exist, so cannot use it either.
    }

    if (!handle->component_state_array[component].log_enable) {
        return false;
    }

    return log_level <= handle->component_state_array[component].log_level;
}

CdiReturnStatus CdiLoggerComponentEnable(CdiLogHandle handle, CdiLogComponent component, bool enable)
{
    if (component >= kLogComponentLast) {
        return kCdiStatusInvalidParameter;
    }

    handle = GetLogHandleToUse(handle);

    handle->component_state_array[component].log_enable = enable;

    return kCdiStatusOk;
}

bool CdiLoggerComponentIsEnabled(CdiLogHandle handle, CdiLogComponent component)
{
    if (component >= kLogComponentLast) {
        return false;
    }

    handle = GetLogHandleToUse(handle);

    return handle->component_state_array[component].log_enable;
}

CdiReturnStatus CdiLoggerLevelSet(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel level)
{
    if (component >= kLogComponentLast || level >= kLogLast) {
        return kCdiStatusInvalidParameter;
    }

    handle = GetLogHandleToUse(handle);

    handle->component_state_array[component].log_level = level;

    return kCdiStatusOk;
}

CdiReturnStatus CdiLoggerComponentEnableGlobal(CdiLogComponent component, bool enable)
{
    // Update global state.
    global_component_state_array[component].log_enable = enable;

    // Update stdout state.
    if (stdout_log_handle) {
        stdout_log_handle->component_state_array[component].log_enable = enable;
    }

    CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the logger list.

    CdiListIterator list_iterator;
    CdiListIteratorInit(&log_state_list, &list_iterator);

    // Walk through the log list.
    CdiLogState* state_ptr = NULL;
    while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
        state_ptr->component_state_array[component].log_enable = enable;
    }

    CdiOsCritSectionRelease(log_state_list_lock); // Done with access to the logger list.

    return kCdiStatusOk;
}

CdiReturnStatus CdiLoggerLevelSetGlobal(CdiLogComponent component, CdiLogLevel level)
{
    // Update global state.
    global_component_state_array[component].log_level = level;

    // Update stdout state.
    if (stdout_log_handle) {
        stdout_log_handle->component_state_array[component].log_level = level;
    }

    CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the logger list.

    CdiListIterator list_iterator;
    CdiListIteratorInit(&log_state_list, &list_iterator);

    // Walk through the log list.
    CdiLogState* state_ptr = NULL;
    while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
        state_ptr->component_state_array[component].log_level = level;
    }

    CdiOsCritSectionRelease(log_state_list_lock); // Done with access to the logger list.

    return kCdiStatusOk;
}

CdiReturnStatus CdiLoggerStderrEnable(bool enable, CdiLogLevel level)
{
    stderr_enable = enable;
    stderr_log_level = level;

    return kCdiStatusOk;
}

void CdiLoggerDestroyLog(CdiLogHandle handle)
{
    if (handle) {
        CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the logger list.

        // If the decremented reference count is zero, then it is safe to destroy the object.
        if (0 == IncDecUsageRefCount(handle, false)) {

            if (kLogMethodFile == handle->log_method) {
                // File log. Don't want to close stdout, otherwise all future output will be suppressed.
                if (handle->file_data_ptr->file_handle != stdout) {
                    CdiOsClose(handle->file_data_ptr->file_handle);
                }
            }

            // Now, free the memory depending on type of log. Nothing special to do for kLogMethodStdout.
            if (kLogMethodCallback == handle->log_method) {
                CdiOsMemFree(handle->callback_data_ptr);
            } else if (kLogMethodFile == handle->log_method) {
                CdiOsMemFree(handle->file_data_ptr);
            }
        }

        // Remove the entry from the log state list, then delete it's memory.
        CdiListRemove(&log_state_list, &handle->list_entry);
        CdiOsMemFree(handle);

        CdiOsCritSectionRelease(log_state_list_lock); // Done with access to the logger list.
    }
}

void CdiLoggerDestroyLogger(CdiLoggerHandle logger_handle)
{
    if (logger_handle) {
        CdiOsMemFree(logger_handle);
    }
}

void CdiLoggerFlushAllFileLogs(void)
{
    CdiOsStaticMutexLock(logger_context_mutex_lock);

    if (initialization_ref_count) {
        CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the logger list.

        if (!CdiListIsEmpty(&log_state_list)) {
            CdiListIterator list_iterator;
            CdiListIteratorInit(&log_state_list, &list_iterator);

            // Walk through the list and flush the logs.
            CdiLogState* state_ptr = NULL;
            while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
                if (kLogMethodFile == state_ptr->log_method) {
                    if (state_ptr->file_data_ptr->file_handle) {
                        CdiOsFlush(state_ptr->file_data_ptr->file_handle);
                    }
                }
            }
        }
        CdiOsCritSectionRelease(log_state_list_lock); // Done with access to the logger list.
    }
    CdiOsStaticMutexUnlock(logger_context_mutex_lock);
}

void CdiLoggerShutdown(bool force)
{
    bool do_shutdown = force;

    CdiOsStaticMutexLock(logger_context_mutex_lock);

    if (force) {
        // Forcing shutdown, so force reference counter to zero.
        initialization_ref_count = 0;
    } else if (initialization_ref_count) {
        if (0 == --initialization_ref_count) {
            // Reference count is zero, so ok to perform the shutdown.
            do_shutdown = true;
        }
    }

    if (do_shutdown) {
        CdiLoggerThreadLogUnset();

        if (log_state_list_lock) {
            CdiOsCritSectionReserve(log_state_list_lock); // Lock access to the logger list.
        }

        // If stdout log handle exists, just remove it from our list so we can still use it for stdout in this function.
        if (stdout_log_handle) {
            CdiListRemove(&log_state_list, &stdout_log_handle->list_entry);
        }

        if (!CdiListIsEmpty(&log_state_list)) {
            // This list should have been empty.
            CdiListIterator list_iterator;
            CdiListIteratorInit(&log_state_list, &list_iterator);

            // Walk through the list and generate some useful info to aid in debugging.
            CdiLogState* state_ptr = NULL;
            while (NULL != (state_ptr = ListGetNextEntry(&list_iterator))) {
                if (force) {
                    CdiLoggerDestroyLog(state_ptr);
                } else {
                    if (kLogMethodFile == state_ptr->log_method) {
                        CdiLogger(stdout_log_handle, kLogComponentGeneric, kLogError, NULL, 0,
                                "List should be empty. Found file entry with file[%s]",
                                state_ptr->file_data_ptr->filename_str);
                    } else {
                        CdiLogger(stdout_log_handle, kLogComponentGeneric, kLogError, NULL, 0,
                                "List should be empty. Found entry with log method[%s]",
                                CdiUtilityKeyEnumToString(kKeyLogMethod, state_ptr->log_method));
                    }
                }
            }
            if (!force) {
                assert(false);
            }
        }

        if (log_state_list_lock) {
            CdiOsCritSectionRelease(log_state_list_lock); // Done with access to the logger list.
            CdiOsCritSectionDelete(log_state_list_lock);
            log_state_list_lock = NULL;
        }

        CdiOsCritSectionDelete(multiline_free_list_lock);
        multiline_free_list_lock = NULL;

        // Free memory of multiline log buffers.
        CdiMultilineLogBufferState* state_ptr = NULL;
        while (NULL != (state_ptr = (CdiMultilineLogBufferState*)CdiSinglyLinkedListPopHead(&multiline_free_list))) {
            CdiOsMemFree(state_ptr);
        }

        if (log_thread_data_valid) {
            CdiOsThreadFreeData(log_thread_data);
            log_thread_data_valid = false;
        }

        // Now that are done using the stdout logger, it is safe to destroy the stdout handle.
        if (stdout_log_handle) {
            CdiOsMemFree(stdout_log_handle);
            stdout_log_handle = NULL;
        }
    }

    CdiOsStaticMutexUnlock(logger_context_mutex_lock);
}
