// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise the CDI Utility Functions API.
 */

#include "cdi_utility_api.h"
#include "cdi_logger_api.h"

#include <assert.h>
#include <stddef.h>

#include "cdi_test_unit_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// External declaration.
extern CdiReturnStatus TestUnitAll(void);
/// External declaration.
extern CdiReturnStatus TestUnitSgl(void);
/// External declaration.
extern CdiReturnStatus TestUnitTimeout(void);
/// External declaration.
extern CdiReturnStatus TestUnitTDigest(void);
/// External declaration.
extern CdiReturnStatus TestUnitRxReorderPackets(void);
/// External declaration.
extern CdiReturnStatus TestUnitRxReorderPayloads(void);
/// External declaration.
extern CdiReturnStatus TestUnitList(void);

/// Type used as a pointer to function that runs a unit test.
typedef CdiReturnStatus (*RunTestAPI)(void);

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/string keys for CdiConnectionStatus.
static const EnumStringKey test_unit_name_key_array[] = {
    { kTestUnitAll,                 "All" },
    { kTestUnitSgl,                 "Sgl" },
    { kTestUnitTimeout,             "Timeout" },
    { kTestUnitTDigest,             "TDigest" },
    { kTestUnitRxpacketReorder,     "RxPacketReorder" },
    { kTestUnitRxPayloadReorder,    "RxPayloadReorder" },
    { kTestUnitList,                "CdiList" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Log a pass/fail status message.
 *
 * @param test_name Test name
 * @param test_api Pointer to test function to call to run the test.
 *
 * @return true if success, otherwise false.
 */
static bool RunTest(CdiTestUnitName test_name, RunTestAPI test_api)
{
    CDI_LOG_THREAD(kLogInfo, "Starting unit test [%s].", CdiUtilityKeyEnumToString(kKeyTestUnit, test_name));

    // Run the test.
    CdiReturnStatus rs = (test_api)();

    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "Unit test [%s] passed.", CdiUtilityKeyEnumToString(kKeyTestUnit, test_name));
        return true;
    } else {
        CDI_LOG_THREAD(kLogInfo, "Unit test [%s] failed. Reason[%s].",
                       CdiUtilityKeyEnumToString(kKeyTestUnit, test_name), CdiCoreStatusToString(rs));
    }
    return false;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

const EnumStringKey* TestUnitGetKeyArray(void)
{
    return test_unit_name_key_array;
}

bool CdiTestUnitRun(CdiTestUnitName test_name)
{
    bool pass = true;

    if (kTestUnitAll == test_name || kTestUnitSgl == test_name) {
        if (!RunTest(kTestUnitSgl, TestUnitSgl)) pass = false;
    }
    if (kTestUnitTimeout == test_name) {
        if (!RunTest(kTestUnitTimeout, TestUnitTimeout)) pass = false;
    }
    if (kTestUnitAll == test_name || kTestUnitTDigest == test_name) {
        if (!RunTest(kTestUnitTDigest, TestUnitTDigest)) pass = false;
    }
    if (kTestUnitAll == test_name || kTestUnitRxpacketReorder == test_name) {
        if (!RunTest(kTestUnitRxpacketReorder, TestUnitRxReorderPackets)) pass = false;
    }
    if (kTestUnitAll == test_name || kTestUnitRxPayloadReorder == test_name) {
        if (!RunTest(kTestUnitRxPayloadReorder, TestUnitRxReorderPayloads)) pass = false;
    }
    if (kTestUnitAll == test_name || kTestUnitList == test_name) {
        if (!RunTest(kTestUnitList, TestUnitList)) pass = false;
    }

    return pass;
}
