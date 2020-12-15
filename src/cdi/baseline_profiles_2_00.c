// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the functions and other definitions that comprise the CDI AVM baseline profile.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cdi_baseline_profile_01_00_api.h"
#include "cdi_logger_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * The current version of the baseline video profile. This value is always sent with AVM configuration structures for
 * video.
 */
static const char* profile_version_video_str = "02.00";

/**
 * The current version of the baseline audio profile. This value is always sent with AVM configuration structures for
 * audio.
 */
static const char* profile_version_audio_str = "02.00";

/**
 * The current version of the baseline ancillary data profile. This value is always sent with AVM configuration
 * structures for ancillary data.
 */
static const char* profile_version_ancillary_data_str = "02.00";

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/string keys for CdiAvmVideoSampling.
static const EnumStringKey video_sampling_key_array[] = {
    { kCdiAvmVidYCbCr444, "YCbCr-4:4:4" },
    { kCdiAvmVidYCbCr422, "YCbCr-4:2:2" },
    { kCdiAvmVidRGB,      "RGB" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoAlphaChannel.
static const EnumStringKey alpha_channel_key_array[] = {
    { kCdiAvmAlphaUnused, "Unused" },
    { kCdiAvmAlphaUsed,   "Used" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoTcs
static const EnumStringKey tcs_key_array[] = {
    { kCdiAvmVidTcsSDR,          "SDR" },
    { kCdiAvmVidTcsPQ,           "PQ" },
    { kCdiAvmVidTcsHLG,          "HLG" },
    { kCdiAvmVidTcsLinear,       "LINEAR" },
    { kCdiAvmVidTcsBT2100LINPQ,  "BT2100LINPQ" },
    { kCdiAvmVidTcsBT2100LINHLG, "BT2100LINHLG" },
    { kCdiAvmVidTcsST2065_1,     "ST2065_1" },
    { kCdiAvmVidTcsST428_1,      "ST428_1" },
    { kCdiAvmVidTcsDensity,      "DENSITY" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoRange
static const EnumStringKey range_key_array[] = {
    { kCdiAvmVidRangeNarrow,      "NARROW" },
    { kCdiAvmVidRangeFullProtect, "FULLPROTECT" },
    { kCdiAvmVidRangeFull,        "FULL" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoBitDepth.
static const EnumStringKey video_bit_depth_key_array[] = {
    { kCdiAvmVidBitDepth8,  "8bit" },
    { kCdiAvmVidBitDepth10, "10bit" },
    { kCdiAvmVidBitDepth12, "12bit" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmColorimetry.
static const EnumStringKey colorimetry_key_array[] = {
    { kCdiAvmVidColorimetryBT601,    "BT601" },
    { kCdiAvmVidColorimetryBT709,    "BT709" },
    { kCdiAvmVidColorimetryBT2020,   "BT2020" },
    { kCdiAvmVidColorimetryBT2100,   "BT2100" },
    { kCdiAvmVidColorimetryST2065_1, "ST2065_1" },
    { kCdiAvmVidColorimetryST2065_3, "ST2065_3" },
    { kCdiAvmVidColorimetryXYZ,      "XYZ" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmAudioChannelGrouping.
static const EnumStringKey audio_channel_grouping_key_array[] = {
    { kCdiAvmAudioM, "SMPTE2110.(M)" },
    { kCdiAvmAudioDM, "SMPTE2110.(DM)" },
    { kCdiAvmAudioST, "SMPTE2110.(ST)" },
    { kCdiAvmAudioLtRt, "SMPTE2110.(LtRt)" },
    { kCdiAvmAudio51, "SMPTE2110.(51)" },
    { kCdiAvmAudio71, "SMPTE2110.(71)"},
    { kCdiAvmAudio222, "SMPTE2110.(222)" },
    { kCdiAvmAudioSGRP, "SMPTE2110.(SGRP)" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmAudioSampleRate.
static const EnumStringKey audio_sample_rate_key_array[] = {
    { kCdiAvmAudioSampleRate48kHz, "48kHz" },
    { kCdiAvmAudioSampleRate96kHz, "96kHz" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Update EnumStringKeyTypes in cdi_utility_api.h whenever an entry is added to this function's switch statement.
static const EnumStringKey* KeyGetArray(CdiAvmBaselineEnumStringKeyTypes key_type) {
    const EnumStringKey* key_array_ptr = NULL;
    switch (key_type) {
        case kKeyAvmPayloadType:
            assert(false); // Should not get here. This type is supported in baseline.
            break;
        case kKeyAvmVideoSamplingType:        key_array_ptr = video_sampling_key_array; break;
        case kKeyAvmVideoAlphaChannelType:    key_array_ptr = alpha_channel_key_array; break;
        case kKeyAvmVideoBitDepthType:        key_array_ptr = video_bit_depth_key_array; break;
        case kKeyAvmVideoColorimetryType:     key_array_ptr = colorimetry_key_array; break;
        case kKeyAvmVideoTcsType:             key_array_ptr = tcs_key_array; break;
        case kKeyAvmVideoRangeType:           key_array_ptr = range_key_array; break;

        case kKeyAvmAudioChannelGroupingType: key_array_ptr = audio_channel_grouping_key_array; break;
        case kKeyAvmAudioSampleRateType:      key_array_ptr = audio_sample_rate_key_array; break;
    }
    assert(NULL != key_array_ptr);
    return key_array_ptr;
}

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Returns the number of bits that evenly fit video pixels into bytes. This is explained in ST 2110-20 and these values
 * are in the tables, though they're expressed in units of bytes.
 *
 * @param baseline_config_ptr Pointer to the config structure with the specifics of the video format whose unit size is
 *                            to be returned.
 * @param payload_unit_size_ptr Pointer to where the unit size is to be written.
 * @return false if the configuration is not valid enough to determine the unit size, true otherwise.
 */
static bool GetVideoUnitSize(const CdiAvmBaselineConfig* baseline_config_ptr, int* payload_unit_size_ptr)
{
    bool ret = false;
    const CdiAvmVideoConfig* video_config_ptr = &baseline_config_ptr->video_config;
    switch (video_config_ptr->depth) {
        case kCdiAvmVidBitDepth8:
            // YUV 4:4:4 and RGB have the same pgroup sizes.
            *payload_unit_size_ptr = (kCdiAvmVidYCbCr422 == video_config_ptr->sampling) ? 32 : 24;
            ret = true;
            break;
        case kCdiAvmVidBitDepth10:
            *payload_unit_size_ptr = (kCdiAvmVidYCbCr422 == video_config_ptr->sampling) ? 40 : 120;
            ret = true;
            break;
        case kCdiAvmVidBitDepth12:
            *payload_unit_size_ptr = (kCdiAvmVidYCbCr422 == video_config_ptr->sampling) ? 48 : 72;
            ret = true;
            break;
    }
    return ret;
}

/**
 * Populates the provided generic configuration structure with the information from a video baseline configuration
 * structure.
 *
 * @param baseline_ptr Pointer to the source configuration; its payload_type must be kCdiAvmVideo.
 * @param config_ptr Address of where the generic configuration is to be written.
 * @param payload_unit_size_ptr Pointer to where the payload unit size is to be written.
 *
 * @return true if the conversion was successful, false if it failed.
 */
static bool MakeBaselineVideoConfiguration(const CdiAvmBaselineConfigCommon* baseline_ptr, CdiAvmConfig* config_ptr,
                                           int* payload_unit_size_ptr)
{
    bool ret = false;
    const CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    const CdiAvmVideoConfig* video_config_ptr = &baseline_config_ptr->video_config;
    char optional_params_str[512];
    optional_params_str[0] = '\0';
    size_t pos = 0;
    const size_t max_pos = sizeof(optional_params_str);

    // Optionally add "interlace" parameter.
    if (max_pos > pos && video_config_ptr->interlace) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " interlace;");
    }

    // Optionally add "segmented" parameter.
    if (max_pos > pos && video_config_ptr->segmented) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " segmented;");
    }

    // Optionally add "TCS" parameter.
    if (max_pos > pos && kCdiAvmVidTcsSDR != video_config_ptr->tcs) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " TCS=%s;",
                        CdiAvmKeyEnumToString(kKeyAvmVideoTcsType, video_config_ptr->tcs,
                                              &video_config_ptr->version));
    }

    // Optionally add "RANGE" parameter.
    if (max_pos > pos && kCdiAvmVidRangeNarrow != video_config_ptr->range) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " RANGE=%s;",
                        CdiAvmKeyEnumToString(kKeyAvmVideoRangeType, video_config_ptr->range,
                                              &video_config_ptr->version));
    }

    // Optionally add "PAR" parameter.
    if (max_pos > pos && (1 != video_config_ptr->par_width || 1 != video_config_ptr->par_height)) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " PAR=%u:%u;", video_config_ptr->par_width,
                        video_config_ptr->par_height);
    }

    // Optionally add "alpha_included" parameter.
    if (max_pos > pos && kCdiAvmAlphaUsed == video_config_ptr->alpha_channel) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " alpha_included=enabled;");
    }

    // Optionally add "partial_frame" parameter.
    if (max_pos > pos && (0 != video_config_ptr->horizontal_size || 0 != video_config_ptr->vertical_size ||
            0 != video_config_ptr->start_horizontal_pos || 0 != video_config_ptr->start_vertical_pos)) {
        pos += snprintf(&optional_params_str[pos], max_pos - pos, " partial_frame=%ux%u+%u+%u;",
                        video_config_ptr->horizontal_size, video_config_ptr->vertical_size,
                        video_config_ptr->start_horizontal_pos, video_config_ptr->start_vertical_pos);
    }

    if (max_pos <= pos) {
        CDI_LOG_THREAD(kLogError, "optional parameters list is too long");
    } else {
        char rate_str[20];
        if (1 == video_config_ptr->frame_rate_den) {
            snprintf(rate_str, sizeof(rate_str), "%u", video_config_ptr->frame_rate_num);
        } else {
            snprintf(rate_str, sizeof(rate_str), "%u/%u", video_config_ptr->frame_rate_num,
                     video_config_ptr->frame_rate_den);
        }
        int bit_depth = 8;
        switch (video_config_ptr->depth) {
            case kCdiAvmVidBitDepth8:
                bit_depth =  8;
                break;
            case kCdiAvmVidBitDepth10:
                bit_depth = 10;
                break;
            case kCdiAvmVidBitDepth12:
                bit_depth = 12;
                break;
        }
        pos = snprintf((char*)config_ptr->data, sizeof(config_ptr->data), "cdi_profile_version=%s; sampling=%s; "
                       "depth=%u; width=%u, height=%u; exactframerate=%s; colorimetry=%s;%s",
                       profile_version_video_str,
                       CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType, video_config_ptr->sampling,
                                             &video_config_ptr->version),
                       bit_depth, video_config_ptr->width, video_config_ptr->height, rate_str,
                       CdiAvmKeyEnumToString(kKeyAvmVideoColorimetryType, video_config_ptr->colorimetry,
                                             &video_config_ptr->version),
                       optional_params_str);
        if ((int)sizeof(config_ptr->data) <= pos) {
            CDI_LOG_THREAD(kLogError, "video configuration string is too long");
        } else {
            // Conversion successful.
            config_ptr->data_size = pos;
            ret = GetVideoUnitSize(baseline_config_ptr, payload_unit_size_ptr);
        }
    }

    return ret;
}

/**
 * Returns the number of bits that evenly fit audio samples into bytes. It includes the samples for all of the channels
 * in the stream.
 *
 * @param baseline_config_ptr Pointer to the config structure with the specifics of the audio format whose unit size is
 *                            to be returned.
 * @param payload_unit_size_ptr Pointer to where the unit size is to be written.
 * @return false if the configuration is not valid enough to determine the unit size, true otherwise.
 */
static bool GetAudioUnitSize(const CdiAvmBaselineConfig* baseline_config_ptr, int* payload_unit_size_ptr)
{
    // Compute the unit size based on number of channels and bit depth (always 24).
    bool ret = false;
    int channel_count = 0;
    switch (baseline_config_ptr->audio_config.grouping) {
        case kCdiAvmAudioM:
            channel_count = 1;
            ret = true;
            break;
        case kCdiAvmAudioDM:
        case kCdiAvmAudioST:
        case kCdiAvmAudioLtRt:
            channel_count = 2;
            ret = true;
            break;
        case kCdiAvmAudio51:
            channel_count = 6;
            ret = true;
            break;
        case kCdiAvmAudio71:
            channel_count = 8;
            ret = true;
            break;
        case kCdiAvmAudio222:
            channel_count = 24;
            ret = true;
            break;
        case kCdiAvmAudioSGRP:
            channel_count = 4;
            ret = true;
            break;
        // No default so compiler complains about missing cases.
    }
    // Each audio sample is 3 bytes. Unit size must contain all the bytes of the samples of all the channels.
    *payload_unit_size_ptr = sizeof(uint8_t) * 3 * channel_count;
    return ret;
}

/**
 * Populates the provided generic configuration structure with the information from an audio baseline configuration
 * structure.
 *
 * @param baseline_ptr Pointer to the source configuration; its payload_type must be kCdiAvmAudio.
 * @param config_ptr Address of where the generic configuration is to be written.
 * @param payload_unit_size_ptr Pointer to where the payload unit size is to be written.
 *
 * @return true if the conversion was successful, false if it failed.
 */
static bool MakeBaselineAudioConfiguration(const CdiAvmBaselineConfigCommon* baseline_ptr, CdiAvmConfig* config_ptr,
                                           int* payload_unit_size_ptr)
{
    bool ret = true;
    const CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    const CdiAvmAudioConfig* audio_config_ptr = &baseline_config_ptr->audio_config;
    char language_param_str[sizeof(audio_config_ptr->language) + 16] = { '\0' };
    if ('\0' != audio_config_ptr->language[0]) {
        char language_str[sizeof(audio_config_ptr->language) + 1] = { '\0' };
        memcpy(language_str, audio_config_ptr->language, sizeof(audio_config_ptr->language));
        int pos = snprintf(language_param_str, sizeof(language_param_str), " language=%s;", audio_config_ptr->language);
        if ((int)sizeof(language_param_str) <= pos) {
            CDI_LOG_THREAD(kLogError, "audio language parameter could not be formatted");
            ret = false;
        }
    }
    if (ret) {
        int pos = snprintf((char*)config_ptr->data, sizeof(config_ptr->data),
                           "cdi_profile_version=%s; order=%s; rate=%s;%s",
                           profile_version_audio_str,
                           CdiAvmKeyEnumToString(kKeyAvmAudioChannelGroupingType, audio_config_ptr->grouping,
                                                 &audio_config_ptr->version),
                           CdiAvmKeyEnumToString(kKeyAvmAudioSampleRateType, audio_config_ptr->sample_rate_khz,
                                                 &audio_config_ptr->version),
                           language_param_str);
        if ((int)sizeof(config_ptr->data) <= pos) {
            CDI_LOG_THREAD(kLogError, "audio configuration string is too long");
            ret = false;
        } else {
            config_ptr->data_size = pos;
            ret = GetAudioUnitSize(baseline_config_ptr, payload_unit_size_ptr);
        }
    }

    return ret;
}

/**
 * Returns the number of bits that comprise the smallest number of bits that should be kept together for transmitting
 * ancillary data.
 *
 * @param baseline_config_ptr Pointer to the config structure with the specifics of the ancillary data format whose unit
 *                            size is to be returned.
 * @param payload_unit_size_ptr Pointer to where the unit size is to be written.
 * @return false if the configuration is not valid enough to determine the unit size, true otherwise.
 */
static bool GetAncillaryDataUnitSize(const CdiAvmBaselineConfig* baseline_config_ptr, int* payload_unit_size_ptr)
{
    (void)baseline_config_ptr;

    *payload_unit_size_ptr = sizeof(uint32_t);  // Let the transmit packetizer break packets only at word boundaries.
    return true;
}

/**
 * Populates the provided generic configuration structure with the information from an ancillary data baseline
 * configuration structure.
 *
 * @param baseline_ptr Pointer to the source configuration; its payload_type must be kCdiAvmAncillary.
 * @param config_ptr Address of where the generic configuration is to be written.
 * @param payload_unit_size_ptr Pointer to where the payload unit size is to be written.
 *
 * @return true if the conversion was successful, false if it failed.
 */
static bool MakeBaselineAncillaryDataConfiguration(const CdiAvmBaselineConfigCommon* baseline_ptr,
                                                   CdiAvmConfig* config_ptr, int* payload_unit_size_ptr)
{
    bool ret = true;
    const CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    int pos = snprintf((char*)config_ptr->data, sizeof(config_ptr->data), "cdi_profile_version=%s;",
                       profile_version_ancillary_data_str);
    if ((int)sizeof(config_ptr->data) <= pos) {
        CDI_LOG_THREAD(kLogError, "ancillary data configuration string is too long");
        ret = false;
    } else {
        // Conversion successful.
        config_ptr->data_size = pos;
    }

    if (ret) {
        ret = GetAncillaryDataUnitSize(baseline_config_ptr, payload_unit_size_ptr);
    }

    return ret;
}

/**
 * Attempts to interpret a generic AVM configuration structure and convert it into a baseline configuration structure
 * for video. Unknown parameters do not result in failure but do cause warnings to be logged in order to handle forward
 * compatiblity as gracefully as possible.
 *
 * @param config_ptr Pointer to the source generic configuration structure.
 * @param baseline_ptr Address where the resulting video baseline configuration data will be written.
 *
 * @return true if the conversion was successful, false if a failure was encountered.
 */
static bool ParseBaselineVideoConfiguration(const CdiAvmConfig* config_ptr, CdiAvmBaselineConfigCommon* baseline_ptr)
{
    bool ret = true;
    CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    // Make a copy of the data array since CdiOsStrTokR is destructive. +1 for terminating NUL character.
    char copy_str[sizeof(config_ptr->data) + 1];
    memcpy(copy_str, config_ptr->data, config_ptr->data_size);
    copy_str[config_ptr->data_size] = '\0';

    CdiAvmVideoConfig* video_config_ptr = &baseline_config_ptr->video_config;

    // Set non-zero, optional, default video configuration values.
    video_config_ptr->par_width = 1;
    video_config_ptr->par_height = 1;

    // Break the string up into semicolon separated tokens.
    int i = 0;
    char* param_save_ptr = NULL;
    for (char* param_ptr = copy_str ; ret && NULL != param_ptr ; i++) {
        param_ptr = CdiOsStrTokR((i == 0) ? param_ptr : NULL, "; ", &param_save_ptr);
        if (param_ptr != NULL) {
            char* value_ptr = strchr(param_ptr, '=');
            if (NULL != value_ptr) {
                // Replace '=' with NUL character so ptr is terminated. Increment value_ptr so it points to the value
                // which is already NUL terminated by CdiOsStrTokR().
                *(value_ptr++) = '\0';
            }
            if (0 == CdiOsStrCaseCmp(param_ptr, "cdi_profile_version")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video profile version parameter value is missing");
                    ret = false;
                } else {
                    if (!CdiAvmParseBaselineVersionString(value_ptr, &video_config_ptr->version)) {
                        CDI_LOG_THREAD(kLogError, "unable to parse video profile version parameter value [%s]",
                                       value_ptr);
                        ret = false;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "sampling")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video sampling parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmVideoSamplingType, value_ptr,
                                                    &video_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown video sampling value [%s]", value_ptr);
                        ret = false;
                    } else {
                        video_config_ptr->sampling = (CdiAvmVideoSampling)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "depth")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video depth parameter value is missing");
                    ret = false;
                } else {
                    int bit_depth = atoi(value_ptr);
                    switch (bit_depth) {
                        case 8:
                            video_config_ptr->depth = kCdiAvmVidBitDepth8;
                            break;
                        case 10:
                            video_config_ptr->depth = kCdiAvmVidBitDepth10;
                            break;
                        case 12:
                            video_config_ptr->depth = kCdiAvmVidBitDepth12;
                            break;
                        default:
                            CDI_LOG_THREAD(kLogError, "invalid video bit depth value [%s]", value_ptr);
                            ret = false;
                            break;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "width")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video width parameter value is missing");
                    ret = false;
                } else {
                    video_config_ptr->width = atoi(value_ptr);
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "height")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video height parameter value is missing");
                    ret = false;
                } else {
                    video_config_ptr->height = atoi(value_ptr);
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "exactframerate")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video exactframerate parameter value is missing");
                    ret = false;
                } else {
                    char* denominator_ptr = strchr(value_ptr, '/');
                    if (NULL != denominator_ptr) {
                        *(denominator_ptr++) = '\0';
                        video_config_ptr->frame_rate_den = atoi(denominator_ptr);
                    } else {
                        video_config_ptr->frame_rate_den = 1;
                    }
                    video_config_ptr->frame_rate_num = atoi(value_ptr);
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "colorimetry")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video colorimetry parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmVideoColorimetryType, value_ptr,
                                                    &video_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown video colorimetry value [%s]", value_ptr);
                        ret = false;
                    } else {
                        video_config_ptr->colorimetry = (CdiAvmColorimetry)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "interlace")) {
                if (NULL != value_ptr) {
                    CDI_LOG_THREAD(kLogWarning, "value for video interlace parameter ignored [%s]", value_ptr);
                } else {
                    video_config_ptr->interlace = true;
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "segmented")) {
                if (NULL != value_ptr) {
                    CDI_LOG_THREAD(kLogWarning, "value for video segmented parameter ignored [%s]", value_ptr);
                } else {
                    video_config_ptr->segmented = true;
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "TCS")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video TCS parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmVideoTcsType, value_ptr,
                                                    &video_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown video TCS value [%s]", value_ptr);
                        ret = false;
                    } else {
                        video_config_ptr->tcs = (CdiAvmVideoTcs)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "RANGE")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video RANGE parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmVideoRangeType, value_ptr,
                                                    &video_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown video RANGE value [%s]", value_ptr);
                        ret = false;
                    } else {
                        video_config_ptr->range = (CdiAvmVideoRange)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "PAR")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video exactframerate parameter value is missing");
                    ret = false;
                } else {
                    char* height_ptr = strchr(value_ptr, ':');
                    if (NULL != height_ptr) {
                        *(height_ptr++) = '\0';
                        video_config_ptr->par_height = atoi(height_ptr);
                    }
                    video_config_ptr->par_width = atoi(value_ptr);
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "alpha_included")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video alpha_included parameter value is missing");
                    ret = false;
                } else {
                    if (0 == CdiOsStrCaseCmp(value_ptr, "enabled")) {
                        video_config_ptr->alpha_channel = kCdiAvmAlphaUsed;
                    } else if (0 != CdiOsStrCaseCmp(value_ptr, "disabled")) {
                        CDI_LOG_THREAD(kLogWarning, "invalid video alpha_included depth value [%s]", value_ptr);
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "partial_frame")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "video alpha_included parameter value is missing");
                    ret = false;
                } else {
                    // Save address in original copy and length in case error message needs to be logged.
                    char* value_save_str = value_ptr - copy_str + (char*)config_ptr->data;
                    int value_save_length = strlen(value_ptr);

                    bool partial_frame_parsed = false;
                    char* height_ptr = strchr(value_ptr, 'x');
                    if (NULL != height_ptr) {
                        *(height_ptr++) = '\0';
                        char* hoffset_ptr = strchr(height_ptr, '+');
                        if (NULL != hoffset_ptr) {
                            *(hoffset_ptr++) = '\0';
                            char* voffset_ptr = strchr(hoffset_ptr, '+');
                            if (NULL != voffset_ptr) {
                                *(voffset_ptr++) = '\0';
                                video_config_ptr->horizontal_size = atoi(value_ptr);
                                video_config_ptr->vertical_size = atoi(height_ptr);
                                video_config_ptr->start_horizontal_pos = atoi(hoffset_ptr);
                                video_config_ptr->start_vertical_pos = atoi(voffset_ptr);
                                partial_frame_parsed = true;
                            }
                        }
                    }
                    if (!partial_frame_parsed) {
                        // Restore entire value back to its place in copy_str.
                        memcpy(value_ptr, value_save_str, value_save_length);
                        CDI_LOG_THREAD(kLogError, "invalid video partial_frame value [%s]", value_ptr);
                        ret = false;
                    }
                }
            } else {
                CDI_LOG_THREAD(kLogWarning, "unknown parameter/value in video configuration string [%s]", param_ptr);
            }
        }
    }

    return ret;
}

/**
 * Attempts to interpret a generic AVM configuration structure and convert it into a baseline configuration structure
 * for audio. Unknown parameters do not result in failure but do cause warnings to be logged in order to handle forward
 * compatiblity as gracefully as possible.
 *
 * @param config_ptr Pointer to the source generic configuration structure.
 * @param baseline_ptr Address where the resulting video baseline configuration data will be written.
 *
 * @return true if the conversion was successful, false if a failure was encountered.
 */
static bool ParseBaselineAudioConfiguration(const CdiAvmConfig* config_ptr, CdiAvmBaselineConfigCommon* baseline_ptr)
{
    bool ret = true;
    CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    // Make a copy of the data array since CdiOsStrTokR is destructive. +1 for terminating NUL character.
    char copy_str[sizeof(config_ptr->data) + 1];
    memcpy(copy_str, config_ptr->data, config_ptr->data_size);
    copy_str[config_ptr->data_size] = '\0';

    CdiAvmAudioConfig* audio_config_ptr = &baseline_config_ptr->audio_config;

    // Set default values for optional parameters.
    memset(audio_config_ptr->language, 0, sizeof(audio_config_ptr->language));

    // Break the string up into semicolon separated tokens.
    int i = 0;
    char* param_save_ptr = NULL;
    for (char* param_ptr = copy_str ; ret && NULL != param_ptr ; i++) {
        param_ptr = CdiOsStrTokR((i == 0) ? param_ptr : NULL, "; ", &param_save_ptr);
        if (param_ptr != NULL) {
            char* value_ptr = strchr(param_ptr, '=');
            if (NULL != value_ptr) {
                // Replace '=' with NUL character so ptr is terminated. Increment value_ptr so it points to the value
                // which is already NUL terminated by CdiOsStrTokR().
                *(value_ptr++) = '\0';
            }
            if (0 == CdiOsStrCaseCmp(param_ptr, "cdi_profile_version")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "audio profile version parameter value is missing");
                    ret = false;
                } else {
                    if (!CdiAvmParseBaselineVersionString(value_ptr, &audio_config_ptr->version)) {
                        CDI_LOG_THREAD(kLogError, "unable to parse audio profile version parameter value [%s]",
                                       value_ptr);
                        ret = false;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "order")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "audio channel order parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmAudioChannelGroupingType, value_ptr,
                                                    &audio_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown audio channel order value [%s]", value_ptr);
                        ret = false;
                    } else {
                        audio_config_ptr->grouping = (CdiAvmAudioChannelGrouping)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "rate")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "audio sample rate parameter value is missing");
                    ret = false;
                } else {
                    int key = CdiAvmKeyStringToEnum(kKeyAvmAudioSampleRateType, value_ptr,
                                                    &audio_config_ptr->version);
                    if (CDI_INVALID_ENUM_VALUE == key) {
                        CDI_LOG_THREAD(kLogError, "unknown audio sample rate value [%s]", value_ptr);
                        ret = false;
                    } else {
                        audio_config_ptr->sample_rate_khz = (CdiAvmAudioSampleRate)key;
                    }
                }
            } else if (0 == CdiOsStrCaseCmp(param_ptr, "language")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "audio language parameter value is missing");
                    ret = false;
                } else if (sizeof(audio_config_ptr->language) < strlen(value_ptr)) {
                    CDI_LOG_THREAD(kLogError, "audio language parameter value is too long: [%s]", value_ptr);
                    ret = false;
                } else {
                    memcpy(audio_config_ptr->language, value_ptr, strlen(value_ptr));
                }
            } else {
                CDI_LOG_THREAD(kLogWarning, "unknown parameter/value in audio configuration string [%s]", param_ptr);
            }
        }
    }

    return ret;
}

/**
 * Attempts to interpret a generic AVM configuration structure and convert it into a baseline configuration structure
 * for ancillary data. Unknown parameters do not result in failure but do cause warnings to be logged in order to handle
 * forward compatiblity as gracefully as possible.
 *
 * @param config_ptr Pointer to the source generic configuration structure.
 * @param baseline_ptr Address where the resulting video baseline configuration data will be written.
 *
 * @return true if the conversion was successful, false if a failure was encountered.
 */
static bool ParseBaselineAncillaryDataConfiguration(const CdiAvmConfig* config_ptr,
                                                    CdiAvmBaselineConfigCommon* baseline_ptr)
{
    bool ret = true;
    CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    // Make a copy of the data array since CdiOsStrTokR is destructive. +1 for terminating NUL character.
    char copy_str[sizeof(config_ptr->data) + 1];
    memcpy(copy_str, config_ptr->data, config_ptr->data_size);
    copy_str[config_ptr->data_size] = '\0';

    CdiAvmAncillaryDataConfig* ancillary_data_config_ptr = &baseline_config_ptr->ancillary_data_config;

    // Break the string up into semicolon separated tokens.
    int i = 0;
    char* param_save_ptr = NULL;
    for (char* param_ptr = copy_str ; ret && NULL != param_ptr ; i++) {
        param_ptr = CdiOsStrTokR((i == 0) ? param_ptr : NULL, "; ", &param_save_ptr);
        if (param_ptr != NULL) {
            char* value_ptr = strchr(param_ptr, '=');
            if (NULL != value_ptr) {
                // Replace '=' with NUL character so ptr is terminated. Increment value_ptr so it points to the value
                // which is already NUL terminated by CdiOsStrTokR().
                *(value_ptr++) = '\0';
            }
            if (0 == CdiOsStrCaseCmp(param_ptr, "cdi_profile_version")) {
                if (NULL == value_ptr) {
                    CDI_LOG_THREAD(kLogError, "ancillary data profile version parameter value is missing");
                    ret = false;
                } else {
                    if (!CdiAvmParseBaselineVersionString(value_ptr, &ancillary_data_config_ptr->version)) {
                        CDI_LOG_THREAD(kLogError,
                                       "unable to parse ancillary data profile version parameter value [%s]",
                                       value_ptr);
                        ret = false;
                    }
                }
            } else {
                CDI_LOG_THREAD(kLogWarning, "unknown parameter/value in ancillary data configuration string [%s]",
                               param_ptr);
            }
        }
    }

    return ret;
}

/// @brief See CdiAvmGetBaselineUnitSize().
static CdiReturnStatus GetBaselineUnitSize(const CdiAvmBaselineConfigCommon* baseline_ptr, int* payload_unit_size_ptr)
{
    bool ret = false;
    CdiAvmBaselineConfig* baseline_config_ptr = (CdiAvmBaselineConfig*)baseline_ptr;

    switch (baseline_config_ptr->payload_type) {
        case kCdiAvmNotBaseline:
            // This type is invalid in this context. Return error.
            break;
        case kCdiAvmVideo:
            ret = GetVideoUnitSize(baseline_config_ptr, payload_unit_size_ptr);
            break;
        case kCdiAvmAudio:
            ret = GetAudioUnitSize(baseline_config_ptr, payload_unit_size_ptr);
            break;
        case kCdiAvmAncillary:
            ret = GetAncillaryDataUnitSize(baseline_config_ptr, payload_unit_size_ptr);
            break;
        // No default so compiler warns about missing cases.
    }

    return ret ? kCdiStatusOk : kCdiStatusFatal;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

////////////////////////////////////////////////////////////////////////////////
// Doxygen commenting for these functions is in cdi_baseline_profile_api.h.
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Register baseline profile 02.00.
 *
 * @return kCdiStatusOk if ok. kCdiStatusArraySizeExceeded if array size has been exceeded, otherwise kCdiStatusFatal.
 */
CdiReturnStatus RegisterAvmBaselineProfiles_2_00(void)
{
    CdiReturnStatus ret = kCdiStatusOk;

    CdiAvmVTableApi config = {
        .make_config_ptr = MakeBaselineVideoConfiguration,
        .parse_config_ptr = ParseBaselineVideoConfiguration,
        .get_unit_size_ptr = GetBaselineUnitSize,
        .key_get_array_ptr = KeyGetArray,
        .structure_size = sizeof(CdiAvmBaselineConfig)
    };
    ret = CdiAvmRegisterBaselineProfile(kCdiAvmVideo, profile_version_video_str, &config);

    if (kCdiStatusOk == ret) {
        config.make_config_ptr = MakeBaselineAudioConfiguration;
        config.parse_config_ptr = ParseBaselineAudioConfiguration;
        ret = CdiAvmRegisterBaselineProfile(kCdiAvmAudio, profile_version_audio_str, &config);
    }

    if (kCdiStatusOk == ret) {
        config.make_config_ptr = MakeBaselineAncillaryDataConfiguration;
        config.parse_config_ptr = ParseBaselineAncillaryDataConfiguration;
        ret = CdiAvmRegisterBaselineProfile(kCdiAvmAncillary, profile_version_ancillary_data_str, &config);
    }

    return ret;
}
