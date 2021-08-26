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

#include <stdbool.h>

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

CdiReturnStatus TestUnitAvmApi(void)
{
    return TestGetBaselineUnitSize() ? kCdiStatusOk : kCdiStatusFatal;
}
