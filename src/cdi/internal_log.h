// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file is used for logging.
 */

#ifndef CDI_INTERNAL_LOG_H__
#define CDI_INTERNAL_LOG_H__

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_log_api.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Send the log message to SDK global log.
#define SDK_LOG_GLOBAL(log_level, ...) \
    CdiLogger(cdi_global_context.global_log_handle, kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, \
              __VA_ARGS__)

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#endif // CDI_INTERNAL_LOG_H__

