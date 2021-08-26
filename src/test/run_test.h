// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in run_test_generic.c.
 */

#ifndef RUN_TEST_H__
#define RUN_TEST_H__

#include "test_args.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Height of stats window.
#define STATS_WINDOW_STATIC_HEIGHT  (4)

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Runs a test using the test parameters provided by the user.
 *
 * @param   test_settings_ptr  Pointer to an array of user-supplied test settings for all connections.
 * @param   max_test_settings_entries  The maximum number of test settings structures that the test_settings array can hold.
 * @param   num_connections    The number of connections to create.
 *
 * @return                     true if all tests pass; false any tests fail
 */
bool RunTestGeneric(TestSettings* test_settings_ptr, int max_test_settings_entries, int num_connections);

#endif // RUN_TEST_H__
