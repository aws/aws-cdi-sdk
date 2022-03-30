// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * A tool to dump the contents of a RIFF file.
 */

#include <stdlib.h>
#include <stdio.h>

#include "cdi_os_api.h"
#include "cdi_logger_api.h"
#include "riff.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Parse command line option.
 *
 * @param option_str Option string given on command line.
 *
 * @return Dump mode or -1 for unrecognized option.
 */
static int ParseOption(const char* option_str)
{
    int ret = -1;
    if (0 == CdiOsStrCmp("--did", option_str)) {
        ret = kRiffDumpDid;
    } else if (0 == CdiOsStrCmp("--cc", option_str)) {
        ret = kRiffDumpClosedCaptions;
    } else {
        fprintf(stderr, "unrecognized option '%s'\n", option_str);
    }
    return ret;
}

/// @brief Print usage message.
static void PrintUsage()
{
    printf("dump_riff <filename>             - Show list of RIFF chunks\n");
    printf("dump_riff --did <filename>       - Show DID/SDID per ANC packet\n");
    printf("dump_riff --cc  <filename>       - Show closed caption data in ANC chunks\n");
}

/**
 * Dump certain data in a RIFF file, or print an error message when file cannot be dumped.
 *
 * @param filename Name of the RIFF file.
 * @param mode Dump mode.
 *
 * @return True when file was dumped, false when an error message was printed.
 */
static bool DumpFile(const char* filename, int mode)
{
    bool ret = true;

    switch (mode) {
        case kRiffDumpDid:
        case kRiffDumpClosedCaptions:
        if (!RiffFileContainsAncillaryData(filename)) {
            fprintf(stderr, "Not a CDI ancillary payload file\n");
            ret = false;
        }
        break;
    }

    if (ret) {
        ret = ReportRiffFileContents(filename, 100, mode);
    }
    return ret;
}


//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * C main entry function.
 *
 * @param argc Number of command line arguments.
 * @param argv Pointer to array of pointers to command line arguments
 * @return Return 0 on success, otherwise 1 indicating a failure occurred.
 */
int main(int argc, char **argv)
{
    CdiLoggerInitialize(); // Initialize logger so we can use the CDI_LOG_THREAD() macro.

    int option = kRiffDumpNone;
    int ret = 0;
    const char* filename = NULL;

    // We expect at most one option.
    switch (argc) {
        case 1:
            PrintUsage();
        break;

        case 2:
            option = kRiffDumpRaw;
            filename = argv[1];
        break;

        case 3:
        {
            option = ParseOption(argv[1]);
            if (-1 == option) {
                ret = 1; // unrecognized option, return with an error
            } else {
                filename = argv[2];
            }
        }
        break;

        default:
        fprintf(stderr, "Invalid number of arguments (must be one or two).");
        ret = 1;
    }

    if (0 == ret) {
        DumpFile(filename, option);
    }

    CdiLoggerShutdown(false);
    return ret;
}
