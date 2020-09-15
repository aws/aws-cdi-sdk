// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file is used for logging.
 */

#ifndef CDI_INTERNAL_LOG_H__
#define CDI_INTERNAL_LOG_H__

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

