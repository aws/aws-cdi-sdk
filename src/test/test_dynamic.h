// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_dynamic.c.
 */

#ifndef TEST_DYNAMIC_H__
#define TEST_DYNAMIC_H__

#include <stdbool.h>

#include "test_args.h"
#include "test_configuration.h"
#include "test_control.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Forward reference of structure to create pointers later.
typedef struct TestDynamicState TestDynamicState;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for managing dynamic tests.
 */
typedef struct TestDynamicState* TestDynamicHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of the test dynamic component for the specified connection.
 *
 * @param connection_info_ptr Pointer to connection state data.
 * @param return_handle_ptr Address where to write returned handle.
 *
 * @return true if successful, otherwise false is returned.
 */
bool TestDynamicCreate(TestConnectionInfo* connection_info_ptr, TestDynamicHandle* return_handle_ptr);

/**
 * Free all resources related to the specified test dynamic component.
 *
 * @param handle Handle of the test dynamic component.
 */
void TestDynamicDestroy(TestDynamicHandle handle);

/**
 * Dynamically test statistics configuration changes by using the SDK CdiCoreStatsReconfigure() API function.
 *
 * @param handle Handle of the test dynamic component.
 *
 * @return true if successful, otherwise false is returned.
 */
bool TestDynamicPollStatsReconfigure(TestDynamicHandle handle);

/**
 * Dynamically test endpoint creation/destruction by using the SDK CdiAvmTxCreateStreamEndpoint() and
 * CdiAvmStreamEndpointDestroy() APIs.
 *
 * @param handle Handle of the test dynamic component.
 *
 * @return true if successful, otherwise false is returned.
 */
bool TestDynamicEndpoints(TestDynamicHandle handle);

/**
 * Determine if dynamic endpoint is enabled or not.
 *
 * @param handle Handle of the test dynamic component.
 * @param stream_index Zero-based index of the stream to check.
 *
 * @return true If enabled is enabled, otherwise false is returned.
 */
bool TestDynamicIsEndpointEnabled(TestDynamicHandle handle, int stream_index);

#endif // TEST_DYNAMIC_H__
