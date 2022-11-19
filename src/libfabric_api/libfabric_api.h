// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in libfabric_api.c.
 */

#ifndef LIBFABRIC_API_H__
#define LIBFABRIC_API_H__

#include "cdi_core_api.h"
#include "cdi_utility_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

#ifdef _WIN32
#define LIBFABRIC_1_9_FILENAME_STRING   ("libfabric.dll")
#define LIBFABRIC_NEW_FILENAME_STRING   ("libfabric_new.dll")
#else
/// @brief The filename of the libfabric 1.9 library.
#define LIBFABRIC_1_9_FILENAME_STRING   ("libfabric.so")

/// @brief The filename of the libfabric new library.
#define LIBFABRIC_NEW_FILENAME_STRING   ("libfabric_new.so")
#endif

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Dynamically load libfabric 1.9 and return a V-table to the API used by the SDK.
 *
 * @param ret_api_ptr Address where to write pointer to the V-table API.
 *
 * @return CdiReturnStatus kCdiStausOk if successful, otherwise a value indicating the nature of failure.
 */
CDI_INTERFACE CdiReturnStatus LoadLibfabric1_9(LibfabricApi** ret_api_ptr);

/**
 * @brief Dynamically load libfabric mainline and return a V-table to the API used by the SDK.
 *
 * @param ret_api_ptr Address where to write pointer to the V-table API.
 *
 * @return CdiReturnStatus kCdiStausOk if successful, otherwise a value indicating the nature of failure.
 */
CDI_INTERFACE CdiReturnStatus LoadLibfabricMainline(LibfabricApi** ret_api_ptr);

#endif // LIBFABRIC_API_H__
