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

/// External declarations.
extern CdiReturnStatus TestUnitAll(void);
/// External declarations.
extern CdiReturnStatus TestUnitAvmApi(void);
/// External declarations.
extern CdiReturnStatus TestUnitSgl(void);
/// External declarations.
extern CdiReturnStatus TestUnitTimeout(void);
/// External declarations.
extern CdiReturnStatus TestUnitTDigest(void);
/// External declarations.
extern CdiReturnStatus TestUnitRxReorderPackets(void);
/// External declarations.
extern CdiReturnStatus TestUnitRxReorderPayloads(void);
/// External declarations.
extern CdiReturnStatus TestUnitList(void);
/// External declarations.
extern CdiReturnStatus TestUnitLogger(void);

/// Type used as a pointer to function that runs a unit test.
typedef CdiReturnStatus (*RunTestAPI)(void);

/// Enum/name/function keys.
typedef struct {
    int enum_value;         ///< Enumerated value.
    const char* test_name;  ///< Display name.
    RunTestAPI test_runner; ///< Corresponding runner function.
} RunTestParams;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/string keys for CdiTestUnitRun.
static const RunTestParams tests[] = {
    { kTestUnitAll,                 "All",              NULL },
    { kTestUnitAvmApi,              "AvmApi",           TestUnitAvmApi },
    { kTestUnitSgl,                 "Sgl",              TestUnitSgl },
    { kTestUnitTimeout,             "Timeout",          TestUnitTimeout },
    { kTestUnitTDigest,             "TDigest",          TestUnitTDigest },
    { kTestUnitRxpacketReorder,     "RxPacketReorder",  TestUnitRxReorderPackets },
    { kTestUnitRxPayloadReorder,    "RxPayloadReorder", TestUnitRxReorderPayloads },
    { kTestUnitList,                "List",             TestUnitList },
    { kTestUnitLogger,              "Logger",           TestUnitLogger },
    { CDI_INVALID_ENUM_VALUE, NULL, NULL } // End of the array
};

/// Enum/string keys for CdiConnectionStatus.
static CdiEnumStringKey test_unit_name_key_array[sizeof(tests)/sizeof(tests[0])] = { 0 };

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Log a pass/fail status message.
 *
 * @param test_key Key identifying a test.
 *
 * @return true if success, otherwise false.
 */
static bool RunTest(CdiTestUnitName test_key)
{
    const char* test_name = tests[test_key].test_name;
    RunTestAPI test_runner = tests[test_key].test_runner;

    CDI_LOG_THREAD(kLogInfo, "Starting unit test [%s].", test_name);

    // Run the test.
    CdiReturnStatus rs = (test_runner)();

    if (kCdiStatusOk == rs) {
        CDI_LOG_THREAD(kLogInfo, "Unit test [%s] passed.", test_name);
        return true;
    } else {
        CDI_LOG_THREAD(kLogInfo, "Unit test [%s] failed. Reason[%s].", test_name, CdiCoreStatusToString(rs));
    }
    return false;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

const CdiEnumStringKey* CdiTestUnitGetKeyArray(void)
{
    for (size_t i=0; i<sizeof(tests)/sizeof(tests[0]); ++i) {
        test_unit_name_key_array[i].enum_value = tests[i].enum_value;
        test_unit_name_key_array[i].name_str = tests[i].test_name;
    }
    return test_unit_name_key_array;
}

bool CdiTestUnitRun(CdiTestUnitName test_name)
{
    bool pass = true;

    switch (test_name) {
        case kTestUnitAll:
            for (int i=1; i < kTestUnitLast; i++) {
                pass = pass && CdiTestUnitRun(i);
            }
            break;
        default:
            pass = RunTest(test_name);
    }

    return pass;
}
