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

#include <stdbool.h>
#include <stdint.h>

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

#endif // CDI_TEST_H__
