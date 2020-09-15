// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
THREAD TestRxCreateThread(void* arg_ptr);

#endif // TEST_RECEIVER_H__
