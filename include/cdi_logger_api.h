// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in logger.c.
 */

#ifndef CDI_LOGGER_API_API_H__
#define CDI_LOGGER_API_API_H__

#include "cdi_log_api.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Maximum length of log message string used in logger.c.
#define MAX_LOG_STRING_LENGTH               (1024)

/// Maximum length of log message function name string used in logger.c.
#define MAX_LOG_FUNCTION_NAME_STRING_LENGTH (128)

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a logger instance. Each handle represents an
 * instance of a logger. A logger is used to hold multiple logs and a single global log.
 */
typedef struct CdiLoggerState* CdiLoggerHandle;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a log. Each handle represents an instance
 * of a log.
 */
typedef struct CdiLogState* CdiLogHandle;

/**
 * @brief Structure used to hold a buffer for a multiline log message.
 */
typedef struct MultilineLogBufferState MultilineLogBufferState;

/**
 * @brief Structure used to hold state data for a multiline log message.
 */
typedef struct {
    bool logging_enabled; ///< When true, logging is enabled for this log_handle.
    CdiLogHandle log_handle; ///< Handle to log being accessed.

    CdiLogComponent component; ///<Selects the SDK component type for logging.
    CdiLogLevel log_level; ///< Current log level for log_handle.

    char function_name_str[MAX_LOG_FUNCTION_NAME_STRING_LENGTH]; ///<Name of this log.
    int line_number; ///<Line number in file where log was called.

    int line_count;                            ///< Number of log lines in the log message buffer
    MultilineLogBufferState* buffer_state_ptr; ///< Pointer to log message buffer structure

    bool buffer_used; ///< Buffer was used, so don't generate output when ending using CdiLoggerMultilineEnd().
} CdiLogMultilineState;

/**
 * @brief Macro used to generate a formatted log line and send the message to the log associated with the calling thread
 *        using CdiLoggerThreadLogSet(). If no log is associated with the calling thread, then stdout is used. To set
 *        the log level use CdiLoggerLevelSet() with kLogComponentGeneric for the component parameter.
 *
 * @param log_level The log level for this message.<br>
 * The remaining parameters contain a variable length list of arguments. The first argument must be the format string.
 */
#define CDI_LOG_THREAD(log_level, ...) \
    CdiLogger(CdiLoggerThreadLogGet(), kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, __VA_ARGS__)

/**
 * @brief Macro used to generate a formatted log line for the specified log component and send the message to the log
 *        associated with the calling thread using CdiLoggerThreadLogSet(). If no log is associated with the calling
 *        thread, then stdout is used. To enable/disable logging of the specified component, use
 *        CdiLoggerComponentEnable(). To set the log level use CdiLoggerLevelSet().
 *
 * @param log_level The log level for this message.
 * @param component Specifies which component to monitor. (E.g. kLogComponentProbe)<br>
 * The remaining parameters contain a variable length list of arguments. The first argument must be the format string.
 */
#define CDI_LOG_THREAD_COMPONENT(log_level, component, ...) \
    CdiLogger(CdiLoggerThreadLogGet(), component, log_level, __FUNCTION__, __LINE__, __VA_ARGS__)

/**
 * @brief Macro used to generate a formatted log line and send to the specified log.
 *
 * @param log_handle Handle of log to send log message to.
 * @param log_level The log level for this message.<br>
 * The remaining parameters contain a variable length list of arguments. The first argument must be the format string.
 */
#define CDI_LOG_HANDLE(log_handle, log_level, ...) \
    CdiLogger(log_handle, kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, __VA_ARGS__)

/**
 * @brief Macro used to start the generation of a multiple line log message. Use this once, followed by any number of
 *        CDI_LOG_MULTILINE() macros to generate each log line. End the multiple line log message using
 *        CDI_LOG_MULTILINE_END(). The log messages are sent to the log associated with the calling thread using
 *        CdiLoggerThreadLogSet(). If no log is associated with the calling thread, then stdout is used.
 */
#define CDI_LOG_THREAD_MULTILINE_BEGIN(log_level, multiline_state_ptr) \
    CdiLoggerMultilineBegin(CdiLoggerThreadLogGet(), kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, \
    multiline_state_ptr)

/**
 * @brief Macro used to send a single line of a multiple line log message. Must use CDI_LOG_MULTILINE_BEGIN() once
 * before using this macro and CDI_LOG_MULTILINE_END() once after all the lines have been generated.
 */
#define CDI_LOG_MULTILINE(multiline_state_ptr, ...) \
    CdiLoggerMultiline(multiline_state_ptr, __VA_ARGS__)

/**
 * @brief Macro used to end a multiple line log message and send to the log. CDI_LOG_MULTILINE_BEGIN() must be used once
 *        to start the multiline log message and define which log to send the message to. Each log line must be
 *        generated using CDI_LOG_MULTILINE().
 */
#define CDI_LOG_MULTILINE_END(multiline_handle) CdiLoggerMultilineEnd(multiline_handle)

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the logger. Must be called once before using any other functions in the logger.
 */
bool CdiLoggerInitialize(void);

/**
 * Create an instance of the logger.
 *
 * @param default_log_level The default log level to use.
 * @param ret_logger_handle_ptr Pointer to returned handle of the new logger.
 *
 * @return true successfully initialized; false if not.
 */
bool CdiLoggerCreate(CdiLogLevel default_log_level, CdiLoggerHandle* ret_logger_handle_ptr);

/**
 * Set the global log for the specified logger.
 *
 * @param logger_handle Handle of logger.
 * @param log_handle Handle of log to set as the global log.
 *
 * @return true if successful, false if not.
 */
bool CdiLoggerSetGlobalLog(CdiLoggerHandle logger_handle, CdiLogHandle log_handle);

/**
 * Create a log using the specified log configuration data.
 *
 * @param logger_handle Handle of logger to create log in.
 * @param con_handle Handle of connection to associate with this log. If NULL, the global log is assumed.
 * @param log_method_data_ptr Pointer to log method configuration data.
 * @param ret_log_handle_ptr Pointer to returned handle of the new log.
 *
 * @return true if successful, false if not.
 */
bool CdiLoggerCreateLog(CdiLoggerHandle logger_handle, CdiConnectionHandle con_handle,
                        const CdiLogMethodData* log_method_data_ptr, CdiLogHandle* ret_log_handle_ptr);

/**
 * Create a file log.
 *
 * @param logger_handle Handle of logger to create file log in.
 * @param filename_str String representing the log file to create. If NULL, then output will be directed to stdout.
 * @param ret_log_handle_ptr Pointer to returned handle of the new log.
 *
 * @return  true successfully initialized; false if not.
 */
bool CdiLoggerCreateFileLog(CdiLoggerHandle logger_handle, const char* filename_str, CdiLogHandle* ret_log_handle_ptr);

/**
 * Function used to generate a formatted log line.
 *
 * @param handle Log handle.
 * @param component Component that is generating this message.
 * @param log_level Level of logging for this message.
 * @param function_name_str Pointer to name of function the log line is being generated from. If NULL, function name
 *                          and source code line number are not used.
 * @param line_number Source code line number the log message is being generated from.
 * @param format_str Pointer to string used for formatting the message.<br>
 * The remaining parameters contain a variable length list of arguments used by the format string to generate the log
 * message.
 */
void CdiLogger(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level,
               const char* function_name_str, int line_number, const char* format_str, ...);

/**
 * Function used to generate a formatted log line from logger callback data.
 *
 * @param handle Log handle to write the message to. Must be using a kLogMethodStdout or kLogMethodFile log method.
 * @param cb_data_ptr Logger callback data used to generate the log message.
 */
void CdiLoggerLogFromCallback(CdiLogHandle handle, const CdiLogMessageCbData* cb_data_ptr);

/**
 * Associate the specified log with the calling thread. Subsequent calls to non-global CdiLog functions by this thread
 * will write to the specified log.
 *
 * @param handle Log handle.
 *
 * @return  true successfully initialized; false if not.
 */
bool CdiLoggerThreadLogSet(CdiLogHandle handle);

/**
 * Remove any association of the calling thread with a logger. Subsequent calls to non-global CdiLog functions by this
 * thread will write to the global logger (if one exists) or to stdout.
 */
void CdiLoggerThreadLogUnset(void);

/**
 * Associate the specified log with the calling thread. Subsequent calls to non-global CdiLog functions by this thread
 * will write to the specified log.
 *
 * @return  true successfully initialized; false if not.
 */
CdiLogHandle CdiLoggerThreadLogGet(void);

/**
 * Begin a multiline log message, creating a buffer to hold the log message lines.
 *
 * @param log_handle Log handle.
 * @param component Component that is generating this message.
 * @param log_level Level of logging for this message.
 * @param function_name_str Pointer to name of function the log line is being generated from. If NULL, function name
 *                          and source code line number are not used.
 * @param line_number Source code line number the log message is being generated from.
 * @param state_ptr Pointer to address where to write returned multiline state data.
 */
void CdiLoggerMultilineBegin(CdiLogHandle log_handle, CdiLogComponent component, CdiLogLevel log_level,
                             const char* function_name_str, int line_number, CdiLogMultilineState* state_ptr);

/**
 * Add a line to a multiline log message buffer.
 *
 * @param state_ptr Pointer to multiline state data created using CdiLoggerMultilineBegin().
 * @param format_str Pointer to string used for formatting the message.<br>
 * The remaining parameters contain a variable length list of arguments used by the format string to generate the
 * console message.
 */
void CdiLoggerMultiline(CdiLogMultilineState* state_ptr, const char* format_str, ...);

/**
 * Return pointer to multiline log buffer. Marks the buffer as used, so CdiLoggerMultilineEnd() won't generate any
 * output.
 *
 * @param state_ptr Pointer to multiline state data created using CdiLoggerMultilineBegin().
 *
 * @return Pointer to log buffer.
 */
char* CdiLoggerMultilineGetBuffer(CdiLogMultilineState* state_ptr);

/**
 * End the multiline lo message and write to the log as a single message. Resources used by the multiline log will be
 * released.
 *
 * @param state_ptr Pointer to multiline state data created using CdiLoggerMultilineBegin().
 */
void CdiLoggerMultilineEnd(CdiLogMultilineState* state_ptr);

/**
 * Flush all file logs.
 */
void CdiLoggerFlushAllFileLogs(void);

/**
 * Determine if a specific log component and level is enabled for logging.
 *
 * @param handle Log handle.
 * @param component Component to check.
 * @param log_level Level to check.
 *
 * @return True if enabled, otherwise false.
 */
bool CdiLoggerIsEnabled(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel log_level);

/**
 * @see CdiLogComponentEnable
 */
CdiReturnStatus CdiLoggerComponentEnable(CdiLogHandle handle, CdiLogComponent component, bool enable);

/**
 * @see CdiLogComponentEnableGlobal
 */
CdiReturnStatus CdiLoggerComponentEnableGlobal(CdiLogComponent component, bool enable);

/**
 * @see CdiLogComponentIsEnabled
 */
bool CdiLoggerComponentIsEnabled(CdiLogHandle handle, CdiLogComponent component);

/**
 * @see CdiLogLevelSet
 */
CdiReturnStatus CdiLoggerLevelSet(CdiLogHandle handle, CdiLogComponent component, CdiLogLevel level);

/**
 * @see CdiLogLevelSetGlobal
 */
CdiReturnStatus CdiLoggerLevelSetGlobal(CdiLogComponent component, CdiLogLevel level);

/**
 * @see CdiLogStderrEnable
 */
CdiReturnStatus CdiLoggerStderrEnable(bool enable, CdiLogLevel level);

/**
 * Closes a log file and destroys the resources used by the instance of the specified log.
 *
 * @param handle Log handle.
 */
void CdiLoggerDestroyLog(CdiLogHandle handle);

/**
 * Destroys the resources used by the instance of the specified logger.
 *
 * @param logger_handle Logger handle.
 */
void CdiLoggerDestroyLogger(CdiLoggerHandle logger_handle);

/**
 * Shutdown the logger. Must be called once before existing the application.
 *
 * @param force Use true to forcibly shutdown the logger closing any open files. Should only be used in abnormal
 *              shutdown conditions.
 */
void CdiLoggerShutdown(bool force);

#ifdef __cplusplus
}
#endif

#endif // CDI_LOGGER_API_API_H__

