// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in transmitter.c.
 */

#ifndef TEST_TRANSMITTER_H__
#define TEST_TRANSMITTER_H__

#include <stdbool.h>

#include "cdi_core_api.h"
#include "cdi_avm_api.h"
#include "cdi_raw_api.h"
#include "cdi_test.h"
#include "test_control.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Start up the test transmit connection as requested by the user, delaying if required, and then launch
 * TestTxSendAllPayloads.
 *
 * @param   arg_ptr  Void pointer to the connection_info data structure for this connection.
 *
 * @return  True if no errors; false if errors;
 */
THREAD TestTxCreateThread(void* arg_ptr);

#endif // TEST_TRANSMITTER_H__

