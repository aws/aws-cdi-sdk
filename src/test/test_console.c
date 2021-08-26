// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains utility functions for outputting data on the console. In addition to standard console output, it
* supports a multi-window console using the ncurses libary on linux and the PDCurses libary on windows.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "test_console.h"
#include "curses.h"

#ifdef WIN32
    #include <fcntl.h> // For O_TEXT
#endif
#include <stdio.h>
#include <stdlib.h>

#include "cdi_logger_api.h"
#include "cdi_test.h"
#include "curses.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Maximum length of a single line from stderr output. The text wraps if the value is exceeded.
#define MAX_MESSAGE_SIZE            (1024)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Multi-window screen has been successfully initialized.
static bool screen_initialized = false;

static int console_height = 0; ///< Height of console.
static int console_width = 0;  ///< Width of console.

/// If multi-window mode is enabled, this pointer is used by the stats window.
static WINDOW* stats_window_ptr = NULL;
static int stats_window_height = 0;            ///< Height of stats window.
static chtype* stats_window_buffer_ptr = NULL; ///< Pointer to buffer used to hold a copy of the stats window.

/// If multi-window mode is enabled, this pointer is used by the scrolling log message window.
static WINDOW* log_window_ptr = NULL;
static int log_window_height = 0;            ///< Height of log window.
static chtype* log_window_buffer_ptr = NULL; ///< Pointer to buffer used to hold a copy of the log window.

/// Critical section used to protect access to the log window.
static CdiCsID log_window_lock = NULL;

/// File descriptor for pipe. [0]= read fd, [1]= write fd.
static int pipe_fd_array[2] = { CDI_INVALID_HANDLE_VALUE, CDI_INVALID_HANDLE_VALUE };

static int original_stdout_fd = CDI_INVALID_HANDLE_VALUE; ///< File descriptor for stdout.

CdiThreadID console_thread_id; ///< Thread ID for this connection.

/// When true, the console is being destroyed. This is used to prevent threads from using the console output functions
/// while TestConsoleDestroy() is executing.
static bool abnormal_termination_enabled = false;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Allocate a buffer and save the contents of a window to it.
 *
 * @param window_ptr Pointer to window handle.
 * @param buffer_ptr Pointer to where to store the window data.
 * @param height Height of window.
 * @param width Width of window.
 */
static void SaveWindowToBuffer(WINDOW* window_ptr, chtype* buffer_ptr, int height, int width) {
    for (int i = 0; i < height; i++) {
        mvwinchnstr(window_ptr, i, 0, buffer_ptr + i*width, width);
    }
}

/**
 * Dump the text in a window to stdout.
 *
 * @param buffer_ptr Pointer to window buffer.
 * @param height Height of window.
 * @param width Width of window.
 */
static void DumpSavedWindowToStdout(chtype* buffer_ptr, int height, int width) {
    for (int r = 0; r < height; r++) {
        // Ignore blank lines.
        bool is_blank = true;
        for (int i = 0; i < width; i++) {
            char c = buffer_ptr[i + r*width] & A_CHARTEXT;
            if ('\0' == c) {
                break;
            } else if (' ' != c) {
                is_blank = false;
                break;
            }
        }
        if (!is_blank) {
            for (int i = 0; i < width; i++) {
                char c = buffer_ptr[i + r*width] & A_CHARTEXT;
                if ('\0' == c) {
                    break;
                }
                putchar(c);
            }
#ifndef WIN32
            // Force a carriage return on stdout. Some linux consoles only move down to the next line and not back to
            // the start of it. This is not needed in windows.
            puts("\r");
#endif
        }
    }
}

/**
 * This function monitors the stderr pipe, and sends any data to the console log window.
 *
 * @param arg_ptr Pointer to parameter for use by the thread (not used here).
 *
 * @return Always returns 0 (not used).
 */
static THREAD TestConsoleThread(void* arg_ptr)
{
    (void)arg_ptr; // Not used
    char msg_buf_ptr[MAX_MESSAGE_SIZE];

    if (CDI_INVALID_HANDLE_VALUE != pipe_fd_array[1]) {
        int index = 0;

        // Read characters until the pipe has been closed and is empty.
        while (read(pipe_fd_array[0], &msg_buf_ptr[index], 1) > 0) {

            // Process a line of characters delimited by '\n' or max size.
            if ('\n' == msg_buf_ptr[index] || index >= MAX_MESSAGE_SIZE - 1) {
                // Don't want to include the line ending character, since TestConsoleLog() will add one.
                if ('\n' != msg_buf_ptr[index]) {
                    index++;
                }
                msg_buf_ptr[index] = '\0'; // Put end of line marker in the string.

                // Write the line to the scrolling log window.
                TestConsoleLog(kLogInfo, msg_buf_ptr);
                index = 0; // Reset pointer to start of buffer for next meesage.
            } else {
                index++;
            }
        }

        // Pipe has been closed and emptied. Log any remaining characters that are in our line buffer.
        if (index) {
            msg_buf_ptr[index] = '\0'; // Put end of line marker in the string.
            TestConsoleLog(kLogInfo, msg_buf_ptr);
        }

        close(pipe_fd_array[0]); // Close the read end of the pipe.

        // Invalidate the FDs.
        pipe_fd_array[0] = CDI_INVALID_HANDLE_VALUE;
        pipe_fd_array[1] = CDI_INVALID_HANDLE_VALUE;
    }

    return 0; // This is not used.
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool TestConsoleCreate(bool multi_window_mode, int num_stats_lines)
{
    bool ret = true;

    stats_window_height = num_stats_lines;

    if (multi_window_mode) {
        ret = CdiOsCritSectionCreate(&log_window_lock);
        if (ret) {
            // We are going to redirect stderr to a pipe and control when it text goes to the log window. Otherwise it will
            // be written directly to the console and appear at random locations.
            original_stdout_fd = dup(CDI_STDERR_FILENO); // Save a copy of the original stderr.

        // Create a read/write pipe to use for stderr.
#ifdef WIN32
            ret = _pipe(pipe_fd_array, 1024, O_TEXT) == 0;
#else
            ret = pipe(pipe_fd_array) == 0;
#endif
        }

        if (ret) {
            ret = dup2(pipe_fd_array[1], CDI_STDERR_FILENO) >= 0; // Send output on stderr to the pipe's writer.
        }

        if (ret) {
            ret = CdiOsThreadCreate(TestConsoleThread, &console_thread_id, "Console", NULL, NULL);
        }

        // Initialize the curses console.
        if (ret && NULL == initscr()) {
            ret = false;
        } else {
            screen_initialized = true;
        }

        if (ret) {
            // Get console size.
            getmaxyx(stdscr, console_height, console_width);

            // Create the stats window. Stats window contains some static text lines + one line for each connection.
            stats_window_ptr = newwin(stats_window_height, console_width, 0, 0); // lines, columns, y, x
            scrollok(stats_window_ptr, false); // Don't want it to scroll.

            // Create the scrolling log window.
            log_window_height = console_height - stats_window_height;
            log_window_ptr = newwin(log_window_height, console_width, stats_window_height, 0);
            scrollok(log_window_ptr, true); // Want it to scroll automatically.

            // Allocate buffers to hold the windows. Use +1 to allow for NULL termination for each line.
            int buf_size = stats_window_height * console_width * (sizeof(chtype) + 1);
            stats_window_buffer_ptr = CdiOsMemAlloc(buf_size);

            buf_size = log_window_height * console_width * (sizeof(chtype) + 1);
            log_window_buffer_ptr = CdiOsMemAlloc(buf_size);
        }
    }

    if (!ret) {
        TestConsoleDestroy(false); // false= Normal termination
    }

    return ret;
}

void TestConsoleDestroy(bool abnormal_termination)
{
    if (CDI_INVALID_HANDLE_VALUE != pipe_fd_array[1]) {
        close(pipe_fd_array[1]);  // Close the write end of the pipe, to prevent read() from continuing to block.
        close(CDI_STDERR_FILENO); // Done with stderr for now. Will set back to the original value below.
    }

    if (console_thread_id) {
        // Wait for the thread to complete.
        CdiOsThreadJoin(console_thread_id, CDI_INFINITE, NULL);
        console_thread_id = NULL;
    }
    // Now that the thread has stopped, it is safe to clean up the remaining resources.

    if (abnormal_termination) {
        // Set flag to disable all console log API functions to prevent other threads from generating erroneous error
        // messages.
        abnormal_termination_enabled = true;
        // Wait a bit to ensure blocked threads are not waiting in any of the console log API functions.
        CdiOsSleep(100);
    }

    if (stats_window_ptr) {
        if (stats_window_buffer_ptr) {
            // Save away the stats window buffer, so we can later send it to stdout.
            SaveWindowToBuffer(stats_window_ptr, stats_window_buffer_ptr, stats_window_height, console_width);
        }
        delwin(stats_window_ptr);
        stats_window_ptr = NULL;
    }

    if (log_window_ptr) {
        if (log_window_buffer_ptr) {
            // Save away the log window buffer, so we can later send it to stdout.
            SaveWindowToBuffer(log_window_ptr, log_window_buffer_ptr, log_window_height, console_width);
        }
        delwin(log_window_ptr);
        log_window_ptr = NULL;
    }

    if (screen_initialized) {
        // Screen was initialized for multi-window mode, so switch it back to the standard console.
        endwin();
        screen_initialized = false;
    }

    if (stats_window_buffer_ptr) {
        // Dump the saved stats window to the standard console so we can still see the info.
        DumpSavedWindowToStdout(stats_window_buffer_ptr, stats_window_height, console_width);
        CdiOsMemFree(stats_window_buffer_ptr);
    }
    if (log_window_buffer_ptr) {
        // Dump the saved log window to the standard console so we can still see the info.
        DumpSavedWindowToStdout(log_window_buffer_ptr, log_window_height, console_width);
        CdiOsMemFree(log_window_buffer_ptr);
    }

    if (CDI_INVALID_HANDLE_VALUE != original_stdout_fd) {
#ifndef WIN32
        // Restore the original stderr handler. Only need to do for linux. Causes a segfault in windows. Windows code
        // for reference: dup2(_fileno(stderr), original_stdout_fd);
        dup2(CDI_STDERR_FILENO, original_stdout_fd);
#endif
        close(original_stdout_fd);
        original_stdout_fd = CDI_INVALID_HANDLE_VALUE;
    }

    CdiOsCritSectionDelete(log_window_lock);
    log_window_lock = NULL;
}

void TestConsoleLogMessageCallback(const CdiLogMessageCbData* cb_data_ptr)
{
    if (abnormal_termination_enabled) {
        return;
    }

    if (CdiLoggerIsEnabled(NULL, cb_data_ptr->component, cb_data_ptr->log_level)) {
        // We need to generate a single log message that contains an optional function name and line number for the
        // first line. Multiline messages need to have each line separated with a line ending character. This is all
        // handled by the Multiline API functions, so we will just use them.
        CdiLogMultilineState m_state;
        CdiLoggerMultilineBegin(NULL, cb_data_ptr->component, cb_data_ptr->log_level,
                                cb_data_ptr->source_code_function_name_ptr, cb_data_ptr->source_code_line_number,
                                &m_state);
        // Walk through each line and write to the new single log message buffer.
        const char* line_str = cb_data_ptr->message_str;
        for (int i = 0; i < cb_data_ptr->line_count; i++) {
            CdiLoggerMultiline(&m_state, line_str);
            line_str += strlen(line_str) + 1; // Advance pointer to byte just past end of the current string.
        }

        char* log_str = CdiLoggerMultilineGetBuffer(&m_state);
        if (log_window_ptr) {
            CdiOsCritSectionReserve(log_window_lock);
            // NOTE: Caller must use TestConsoleStatsRefresh() to see the updated text in the window.
            waddstr(log_window_ptr, log_str);
            waddstr(log_window_ptr, "\r");
            wrefresh(log_window_ptr);
            CdiOsCritSectionRelease(log_window_lock);
        } else {
            printf("%s\r\n", log_str); // send to stdout
        }

        CdiLoggerMultilineEnd(&m_state);
    }
}

void TestConsoleStats(int x, int y, int attribute, const char* format_str, ...)
{
    if (abnormal_termination_enabled) {
        return;
    }

    va_list vars;

    va_start(vars, format_str);
    if (stats_window_ptr) {
        CdiOsCritSectionReserve(log_window_lock);
        wmove(stats_window_ptr, y, x);

        if (A_NORMAL != attribute) {
            wattron(stats_window_ptr, attribute);
        }

        // NOTE: Caller must use TestConsoleStatsRefresh() to see the updated text in the window.
        vw_printw(stats_window_ptr, format_str, vars);

        if (A_NORMAL != attribute) {
            wattroff(stats_window_ptr, attribute);
        }

        CdiOsCritSectionRelease(log_window_lock);
    } else {
        vprintf(format_str, vars); // send to stdout
        printf("\r\n");
        fflush(stdout);
    }
    va_end(vars);
}

void TestConsoleStatsHorzLine(int x, int y, int width)
{
    if (abnormal_termination_enabled) {
        return;
    }

    if (0 == width) {
        width = console_width - x;
    }

    if (stats_window_ptr) {
        // NOTE: Caller must use TestConsoleStatsRefresh() to see the updated text in the window.
        mvwhline(stats_window_ptr, y, x, '-', width);
    }
}

void TestConsoleStatsRefresh(void)
{
    if (abnormal_termination_enabled) {
        return;
    }

    if (stats_window_ptr) {
        CdiOsCritSectionReserve(log_window_lock);
        wrefresh(stats_window_ptr);
        CdiOsCritSectionRelease(log_window_lock);
    }
}

void TestConsoleLog(CdiLogLevel log_level, const char* format_str, ...)
{
    if (abnormal_termination_enabled) {
        return;
    }

    if (CdiLoggerIsEnabled(NULL, kLogComponentGeneric, log_level)) {
        va_list vars;

        va_start(vars, format_str);
        if (log_window_ptr) {
            CdiOsCritSectionReserve(log_window_lock);
            if (kLogError == log_level) {
                waddstr(log_window_ptr, "ERROR: ");
            }
            vw_printw(log_window_ptr, format_str, vars);
            waddstr(log_window_ptr, "\n\r");
            wrefresh(log_window_ptr); // Update the text in the window.
            CdiOsCritSectionRelease(log_window_lock);
        } else {
            if (kLogError == log_level) {
                printf("ERROR: ");
            }
            vprintf(format_str, vars); // Send to stdout.
            printf("\n\r");
            fflush(stdout);
        }
        va_end(vars);
    }
}
