// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the CDI unit test application.
*/

#include "cdi_test_unit_api.h"
#include "test_common.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure that holds all the test settings as set from the command line.
 */
typedef struct {
    CdiTestUnitName test_name; ///< Test to run.
} TestSettings;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Output command line help message.
 */
void PrintHelp(void) {
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "\nCommand line options:\n");
    TestConsoleLog(kLogInfo, "--test <name> : Choose name of unit test to run (default=All). Valid options are:");
    for (int i = 0; i < kTestUnitLast; i++) {
        TestConsoleLog(kLogInfo, "  %s", CdiUtilityKeyEnumToString(kKeyTestUnit, i));
    }
}

/**
 * Parse command line and write to the specified TestSettings structure.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments.
 * @param test_settings_ptr Address where to write returned settings.
 *
 * @return true if successful, otherwise false.
 */
static bool ParseCommandLine(int argc, const char** argv, TestSettings* test_settings_ptr)
{
    bool ret = true;

    int i = 1;
    while (i < argc && ret) {
        const char* arg_str = argv[i++];
        if (0 == CdiOsStrCmp("--test", arg_str)) {
            test_settings_ptr->test_name = CdiUtilityKeyStringToEnum(kKeyTestUnit, argv[i]);
            if (CDI_INVALID_ENUM_VALUE == (int)test_settings_ptr->test_name) {
                CDI_LOG_THREAD(kLogError, "Invalid test name. Got [%s].", argv[i]);
                ret = false;
            }
            i++;
        } else if (0 == CdiOsStrCmp("--help", arg_str) || 0 == CdiOsStrCmp("-h", arg_str)) {
            ret = false;
            break;
        } else {
            CDI_LOG_THREAD(kLogError, "Unknown command line option[%s]\n", arg_str);
            ret = false;
            break;
        }
    }

    if (!ret) {
        PrintHelp();
    }

    return ret;
}


//*********************************************************************************************************************
//******************************************* START OF C MAIN FUNCTION ************************************************
//*********************************************************************************************************************

/**
 * C main entry function.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments.
 *
 * @return 0 on success, otherwise 1 indicating a failure occurred.
 */
int main(int argc, const char** argv)
{
    CdiLoggerInitialize(); // Initialize logger so we can use the CDI_LOG_THREAD() macro to generate console messages.

    // Setup default test settings.
    TestSettings settings = {
        .test_name = kTestUnitAll
    };

    // Parse command line.
    CommandLineHandle command_line_handle = NULL;
    if (!TestCommandLineParserCreate(&argc, &argv, &command_line_handle) ||
        !ParseCommandLine(argc, argv, &settings)) {
        return 1;
    }

    CDI_LOG_THREAD(kLogInfo, "Starting unit test(s).");

    bool ret = CdiTestUnitRun(settings.test_name);

    TestConsoleLog(kLogInfo, "");
    if (ret) {
        CDI_LOG_THREAD(kLogInfo, "All unit test(s) passed.");
    } else {
        CDI_LOG_THREAD(kLogInfo, "One or more unit tests failed.");
    }

    TestCommandLineParserDestroy(command_line_handle);

    return (ret) ? 0 : 1;
}
