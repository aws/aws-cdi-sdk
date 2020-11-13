// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_console.c.
 */

#ifndef TEST_CONSOLE_H__
#define TEST_CONSOLE_H__

#include <stdbool.h>

#include "test_args.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize the console for either multi-window mode or stdout mode.
 *
 * @param multi_window_mode True to enable mutli-window mode, otherwise use stdout mode.
 * @param num_stats_lines Number of lines to dedicate to statistics window.
 *
 * @return Returns true if success, otherwise false is returned and the stdout console will be used.
 */
bool TestConsoleCreate(bool multi_window_mode, int num_stats_lines);

/**
 * Callback function used by the log message callback feature.
 *
 * @param cb_data_ptr Pointer to log message callback data.
 */
void TestConsoleLogMessageCallback(const CdiLogMessageCbData* cb_data_ptr);

/**
 * Put a message in the console log window if using mult-window mode, otherwise just write to stdout. NOTE: When in
 * multi-window mode, must use TestConsoleStatsRefresh() to update the window.
 *
 * @param x Console window X position.
 * @param y Console window Y position.
 * @param attribute Character attribute. Use A_NORMAL for normal console output.
 * @param format_str Pointer to format string.
 * @param ... Variable list of string arguments.
 */
void TestConsoleStats(int x, int y, int attribute, const char* format_str, ...);

/**
 * Render a horizontal line on the stats console. NOTE: When in multi-window mode, must use TestConsoleStatsRefresh()
 * to update the window.
 *
 * @param x Console window X position.
 * @param y Console window Y position.
 * @param width Column width of line to draw. Use 0 for full width of console.
 */
void TestConsoleStatsHorzLine(int x, int y, int width);

/**
 * Refresh the status console window. Only used in multi-window mode.
 */
void TestConsoleStatsRefresh(void);

/**
 * Add a message to the console log window, if multi-window mode is enabled (no need to use TestConsoleStatsRefresh()).
 * Otherwise just writes to stdout.
 *
 * @param log_level The log level for this message.
 * @param format_str Format string specifier.
 * @param ...  The remaining parameters contain a variable length list of arguments.
 */
void TestConsoleLog(CdiLogLevel log_level, const char* format_str, ...);

/**
 * Destroy the resources used by the console. Doesn't do much unless multi-window mode is enabled.
 *
 * @param abnormal_termination True if being terminated abnormally (ie. from a signal event handler). This will cause
 *                             all console log API functions to be disabled to prevent other threads from generating
 *                             erroneous log error messages.
 */
void TestConsoleDestroy(bool abnormal_termination);

#endif // TEST_CONSOLE_H__
