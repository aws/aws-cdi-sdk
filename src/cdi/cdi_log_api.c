// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise the CDI Core SDK's API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_log_api.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include "configuration.h"
#include "internal.h"
#include "logger_api.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************


//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************


//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiLogComponentEnable(CdiConnectionHandle handle, CdiLogComponent component, bool enable)
{
    CdiLogHandle log_handle = NULL;
    if (handle) {
        log_handle = handle->log_handle;
    }
    return CdiLoggerComponentEnable(log_handle, component, enable);
}

CdiReturnStatus CdiLogComponentEnableGlobal(CdiLogComponent component, bool enable)
{
    return CdiLoggerComponentEnableGlobal(component, enable);
}

bool CdiLogComponentIsEnabled(CdiConnectionHandle handle, CdiLogComponent component)
{
    CdiLogHandle log_handle = NULL;
    if (handle) {
        log_handle = handle->log_handle;
    }
    return CdiLoggerComponentIsEnabled(log_handle, component);
}

CdiReturnStatus CdiLogLevelSet(CdiConnectionHandle handle, CdiLogComponent component, CdiLogLevel level)
{
    CdiLogHandle log_handle = NULL;
    if (handle) {
        log_handle = handle->log_handle;
    }
    return CdiLoggerLevelSet(log_handle, component, level);
}

CDI_INTERFACE CdiReturnStatus CdiLogLevelSetGlobal(CdiLogComponent component, CdiLogLevel level)
{
    return CdiLoggerLevelSetGlobal(component, level);
}

CDI_INTERFACE CdiReturnStatus CdiLogStderrEnable(bool enable, CdiLogLevel level)
{
    return CdiLoggerStderrEnable(enable, level);
}

