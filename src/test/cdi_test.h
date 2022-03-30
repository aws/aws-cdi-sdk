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

/// @brief Log through cdi_test's global log handle.
#define TEST_LOG_GLOBAL(log_level, ...) \
    CdiLogger(GetGlobalTestSettings()->test_app_global_log_handle, kLogComponentGeneric, log_level, __FUNCTION__, \
        __LINE__, __VA_ARGS__)

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
