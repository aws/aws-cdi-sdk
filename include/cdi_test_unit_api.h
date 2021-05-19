// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_TEST_UNIT_API_H__
#define CDI_TEST_UNIT_API_H__

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in cdi_test_unit_api.c
 * and are meant to provide access to helpful SDK data structures and functions that are not part
 * of the core API functionality.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cdi_core_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This enumeration is used in to indicate which unit test to run.
 */
typedef enum {
    kTestUnitAll, ///< Test all unit tests.
    kTestUnitSgl, ///< Test unit SGL.
    kTestUnitTimeout, ///< Test unit timeout.
    kTestUnitTDigest, ///< Test unit T-digest.
    kTestUnitRxpacketReorder, ///< Test unit Rx packet reorderer.
    kTestUnitRxPayloadReorder, ///< Test unit Rx payload reorderer.
    kTestUnitList, ///< Unit test for doubly linked list implementation.
    kTestUnitLast, ///< End of list (for range checking, do no remove).
} CdiTestUnitName;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Get key array, only used internally by the CDI-SDK.
 *
 * @return Pointer to enum string array.
 */
const EnumStringKey* TestUnitGetKeyArray(void);

/**
 * Function used to convert a string to a matching enum value.
 *
 * @param test_name Enum from CdiTestUnitName which indicates which unit test to run.
 *
 * @return true if all tests were successful, otherwise false.
 */
CDI_INTERFACE bool CdiTestUnitRun(CdiTestUnitName test_name);

#ifdef __cplusplus
}
#endif

#endif // CDI_UTILITY_API_H__
