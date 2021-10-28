// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in receiver.c.
 */

#ifndef TEST_RECEIVER_H__
#define TEST_RECEIVER_H__

#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @param   arg_ptr  Void pointer to the connection_info data structure for this connection.
 *
 * @return           True if no errors; false if errors;
 */
CDI_THREAD TestRxCreateThread(void* arg_ptr);

#endif // TEST_RECEIVER_H__
