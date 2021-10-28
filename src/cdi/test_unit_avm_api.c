// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains a unit test for the AVM API.
 */

#include "cdi_avm_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_logger_api.h"

#include <stdbool.h>

/// Helper macro.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            CDI_LOG_THREAD(kLogError, "%s at line [%d] failed", #condition, __LINE__); \
            pass = false; \
        } \
    } while (false);

/// Test for CdiAvmGetBaselineUnitSize.
static bool TestGetBaselineUnitSize()
{
    bool pass = true;
    int unit_size = 0;

    // Test unit size for video payload type.
    CdiAvmBaselineConfig video_config = {
        .payload_type = kCdiAvmVideo,
        .video_config = {
            .version = {2, 0},
            .sampling = kCdiAvmVidYCbCr422,
            .depth = kCdiAvmVidBitDepth12
        }
    };
    CdiAvmGetBaselineUnitSize(&video_config, &unit_size);
    pass = pass && unit_size == 48;

    // Test unit size for audio payload type.
    CdiAvmBaselineConfig audio_config = {
        .payload_type = kCdiAvmAudio,
        .audio_config = {
            .version = {2, 0},
            .grouping = kCdiAvmAudio71,
            .sample_rate_khz = kCdiAvmAudioSampleRate96kHz
        }
    };
    CdiAvmGetBaselineUnitSize(&audio_config, &unit_size);
    pass = pass && unit_size == 8 * 3 * 8; // Audio71 = 8 channels

    // Test unit size for ancillary payload type.
    CdiAvmBaselineConfig ancillary_config = {
        .payload_type = kCdiAvmAncillary,
        .ancillary_data_config = {
            .version = {2, 0}
        }
    };
    CdiAvmGetBaselineUnitSize(&ancillary_config, &unit_size);
    pass = pass && unit_size == 32; // always 32

    return pass;
}

/// Test for CdiAvmValidateBaselineVersionString.
static bool TestValidateBaselineVersion()
{
    bool pass = true;

    CdiAvmBaselineProfileVersion version;
    CdiReturnStatus rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, "01.00", &version);
    CHECK(kCdiStatusOk == rs);
    CHECK(1 == version.major);
    CHECK(0 == version.minor);

    // Not setting output parameter is OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, "01.00", NULL);
    CHECK(kCdiStatusOk == rs);

    // Not providing a version string is not OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, NULL, &version);
    CHECK(kCdiStatusInvalidParameter == rs);

    // Not providing a version string is not OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmAncillary, "7.98", &version);
    CHECK(kCdiStatusProfileNotSupported == rs);

    return pass;
}

/// Test for CdiAvmRegisterBaselineProfile.
static bool TestRegisterBaselineProfile()
{
    bool pass = true;

    CdiAvmVTableApi config = {
        .make_config_ptr = NULL,
        .parse_config_ptr = NULL,
        .get_unit_size_ptr = NULL,
        .key_get_array_ptr = NULL,
        .structure_size = sizeof(CdiAvmBaselineConfig)
    };

    CdiReturnStatus expected_status[] = { kCdiStatusOk, kCdiStatusDuplicateBaselineVersion };
    // Check that registering the same profile functions multiple times is prevented.
    for (int i = 0; i<5; ++i) {
        CdiReturnStatus rs = CdiAvmRegisterBaselineProfile(kCdiAvmVideo, "19.84", &config);
        CHECK(expected_status[i != 0] == rs);
    }

    return pass;
}


/// Helper macro.
#define RUN_TEST(test_func)                                            \
    do { if (!test_func()) {                                           \
        CDI_LOG_THREAD(kLogError, "AVM test "#test_func" failed.");    \
        rs = kCdiStatusFatal;                                          \
    } } while (false)

CdiReturnStatus TestUnitAvmApi(void)
{
    CdiReturnStatus rs = kCdiStatusOk;
    RUN_TEST(TestGetBaselineUnitSize);
    RUN_TEST(TestValidateBaselineVersion);
    RUN_TEST(TestRegisterBaselineProfile);
    return rs;
}
