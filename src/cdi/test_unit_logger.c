// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains a unit test for the CdiList functionality.
 */

#include "cdi_core_api.h"
#include "cdi_logger_api.h"

/// Test case for multiline logger API when a component is disabled.
static CdiReturnStatus TestMultilineLoggerDisabled(void)
{
    CdiLoggerInitialize();

    CdiLogHandle handle = CdiLoggerThreadLogGet();
    CdiLogMultilineState state;
    bool component_enabled = false;

    CdiLoggerComponentEnable(handle, kLogComponentEndpointManager, component_enabled);
    CdiLoggerMultilineBegin(handle, kLogComponentEndpointManager, kLogError, "SomeFunction", 123, &state);
    CdiLoggerMultiline(&state, "This is a multiline message");
    CdiLoggerMultilineEnd(&state);

    CdiLoggerComponentEnable(handle, kLogComponentEndpointManager, component_enabled);
    CdiLoggerMultilineBegin(handle, kLogComponentEndpointManager, kLogError, "SomeFunction", 123, &state);
    CdiLoggerMultiline(&state, "This is another multiline message");
    char* buffer = CdiLoggerMultilineGetBuffer(&state);
    CdiLoggerMultilineEnd(&state);

    CdiLoggerShutdown(false);

    return component_enabled == (NULL != buffer) ? kCdiStatusOk : kCdiStatusFatal;
}

/// Helper macro.
#define RUN_TEST(test_func)                     \
    do {                                        \
        CdiReturnStatus test_rs = test_func();  \
        if (kCdiStatusOk != test_rs) {          \
            CDI_LOG_THREAD(kLogError, "Logger test "#test_func" failed [%s].", CdiCoreStatusToString(test_rs)); \
            rs = kCdiStatusFatal;               \
    } } while (false)                           \

CdiReturnStatus TestUnitLogger(void)
{
    CdiReturnStatus rs = kCdiStatusOk;
    RUN_TEST(TestMultilineLoggerDisabled);
    return rs;
}
