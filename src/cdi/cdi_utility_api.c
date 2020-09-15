// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise the CDI Utility Functions API.
 */

#include "cdi_utility_api.h"

#include <assert.h>
#include <stddef.h>

#include "cdi_baseline_profile_api.h"
#include "cdi_core_api.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/string keys for CdiAdapterTypeSelection.
static const EnumStringKey adapter_type_key_array[] = {
    { kCdiAdapterTypeEfa,             "EFA" },
    { kCdiAdapterTypeSocket,          "SOCKET" },
    { kCdiAdapterTypeSocketLibfabric, "SOCKET_LIBFABRIC" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiBufferType.
static const EnumStringKey buffer_type_key_array[] = {
    { kCdiLinearBuffer, "LINEAR" },
    { kCdiSgl,          "SGL" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/String keys for ConnectionProtocolType.
static const EnumStringKey protocols_key_array[] = {
    { kProtocolTypeRaw, "RAW" },
    { kProtocolTypeAvm, "AVM" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogMethod.
static const EnumStringKey log_method_key_array[] = {
    { kLogMethodStdout,   "LogMethodStdout" },
    { kLogMethodCallback, "LogMethodCallback" },
    { kLogMethodFile,     "LogMethodFile" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogComponent.
static const EnumStringKey log_component_key_array[] = {
    { kLogComponentGeneric,            "GENERIC" },
    { kLogComponentPayloadConfig,      "PAYLOAD_CONFIG" },
    { kLogComponentPerformanceMetrics, "PERFORMANCE_METRICS" },
    { kLogComponentProbe,              "PROBE" },
    { kLogComponentEndpointManager,    "ENDPOINT_MANAGER" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogLevel.
static const EnumStringKey log_level_key_array[] = {
    { kLogFatal,    "FATAL" },
    { kLogCritical, "CRITICAL" },
    { kLogError,    "ERROR" },
    { kLogWarning,  "WARNING" },
    { kLogInfo,     "INFO" },
    { kLogVerbose,  "VERBOSE" },
    { kLogDebug,    "DEBUG" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmPayloadType.
static const EnumStringKey payload_type_key_array[] = {
    { kCdiAvmVideo,     "Video" },
    { kCdiAvmAudio,     "Audio" },
    { kCdiAvmAncillary, "Ancillary" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoSampling.
static const EnumStringKey video_sampling_key_array[] = {
    { kCdiAvmVidYCbCr444, "YCbCr444" },
    { kCdiAvmVidYCbCr422, "YCbCr422" },
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
    { kCdiAvmVidTcsLinear,       "Linear" },
    { kCdiAvmVidTcsBT2100LINPQ,  "BT2100LINPQ" },
    { kCdiAvmVidTcsBT2100LINHLG, "BT2100LINHLG" },
    { kCdiAvmVidTcsST2065_1,     "ST2065_1" },
    { kCdiAvmVidTcsST428_1,      "ST428_1" },
    { kCdiAvmVidTcsDensity,      "Density" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmVideoRange
static const EnumStringKey range_key_array[] = {
    { kCdiAvmVidRangeNarrow,      "Narrow" },
    { kCdiAvmVidRangeFullProtect, "FullProtect" },
    { kCdiAvmVidRangeFull,        "Full" },
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
    { kCdiAvmAudioM,    "M" },
    { kCdiAvmAudioDM,   "DM" },
    { kCdiAvmAudioST,   "ST" },
    { kCdiAvmAudioLtRt, "LtRt" },
    { kCdiAvmAudio51,   "51" },
    { kCdiAvmAudio71,   "71" },
    { kCdiAvmAudio222,  "222" },
    { kCdiAvmAudioSGRP, "SGRP" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmAudioSampleRate.
static const EnumStringKey audio_sample_rate_key_array[] = {
    { kCdiAvmAudioSampleRate48kHz, "48kHz" },
    { kCdiAvmAudioSampleRate96kHz, "96kHz" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiConnectionStatus.
static const EnumStringKey connection_status_key_array[] = {
    { kCdiConnectionStatusDisconnected, "Disconnected" },
    { kCdiConnectionStatusConnected,    "Connected" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

// Update EnumStringKeyTypes in cdi_utility_api.h whenever an entry is added to this function's switch statement.
const EnumStringKey* CdiUtilityKeyGetArray(EnumStringKeyTypes key_type) {
    const EnumStringKey* key_array_ptr = NULL;
    switch (key_type) {
        case kKeyAdapterType:                 key_array_ptr = adapter_type_key_array; break;
        case kKeyBufferType:                  key_array_ptr = buffer_type_key_array; break;
        case kKeyConnectionProtocolType:      key_array_ptr = protocols_key_array; break;
        case kKeyLogMethod:                   key_array_ptr = log_method_key_array; break;
        case kKeyLogComponent:                key_array_ptr = log_component_key_array; break;
        case kKeyLogLevel:                    key_array_ptr = log_level_key_array; break;
        case kKeyAvmPayloadType:              key_array_ptr = payload_type_key_array; break;
        case kKeyAvmVideoSamplingType:        key_array_ptr = video_sampling_key_array; break;
        case kKeyAvmVideoAlphaChannelType:    key_array_ptr = alpha_channel_key_array; break;
        case kKeyAvmVideoBitDepthType:        key_array_ptr = video_bit_depth_key_array; break;
        case kKeyAvmVideoColorimetryType:     key_array_ptr = colorimetry_key_array; break;
        case kKeyAvmVideoTcsType:             key_array_ptr = tcs_key_array; break;
        case kKeyAvmVideoRangeType:           key_array_ptr = range_key_array; break;
        case kKeyAvmAudioChannelGroupingType: key_array_ptr = audio_channel_grouping_key_array; break;
        case kKeyAvmAudioSampleRateType:      key_array_ptr = audio_sample_rate_key_array; break;
        case kKeyConnectionStatus:            key_array_ptr = connection_status_key_array; break;
    }
    assert(NULL != key_array_ptr);
    return key_array_ptr;
}

const char* CdiUtilityEnumValueToString(const EnumStringKey* key_array, int enum_value)
{
    const char* ret_str = NULL;

    if (key_array) {
        int i = 0;
        while (CDI_INVALID_ENUM_VALUE != key_array[i].enum_value) {
            if (enum_value == key_array[i].enum_value) {
                ret_str = key_array[i].name_str;
                break;
            }
            i++;
        }
    }

    return ret_str;
}

int CdiUtilityStringToEnumValue(const EnumStringKey* key_array, const char* name_str)
{
    int ret_enum_value = CDI_INVALID_ENUM_VALUE;

    if (key_array && name_str) {
        int i = 0;
        while (CDI_INVALID_ENUM_VALUE != key_array[i].enum_value) {
            if (0 == CdiOsStrCaseCmp(name_str, key_array[i].name_str)) {
                ret_enum_value = key_array[i].enum_value;
                break;
            }
            i++;
        }
    }

    return ret_enum_value;
}

const char* CdiUtilityKeyEnumToString(EnumStringKeyTypes key_type, int enum_value)
{
    const EnumStringKey* key_array = CdiUtilityKeyGetArray(key_type);
    return CdiUtilityEnumValueToString(key_array, enum_value);
}

int CdiUtilityKeyStringToEnum(EnumStringKeyTypes key_type, const char* name_str)
{
    const EnumStringKey *key_array = CdiUtilityKeyGetArray(key_type);
    return CdiUtilityStringToEnumValue(key_array, name_str);
}

uint32_t CdiUtilityPtpToRtp(const CdiPtpTimestamp* ptp_timestamp_ptr, uint32_t sample_rate)
{
    uint64_t rtp_counts = 0;
    if ((sample_rate != 0) && (NULL != ptp_timestamp_ptr)) {
        uint64_t ptp_time_in_nanoseconds = ((uint64_t)ptp_timestamp_ptr->seconds * NANOSECONDS_TO_SECONDS) +
                                           ptp_timestamp_ptr->nanoseconds;
        uint32_t rounding_factor = ((sample_rate - 1) / 2);
        uint64_t sample_period_in_nanoseconds = (NANOSECONDS_TO_SECONDS + rounding_factor) / sample_rate;
        rtp_counts = ptp_time_in_nanoseconds / sample_period_in_nanoseconds;
    }
    // RTP timestamp is truncated to 32 bits so any lost upper bits on ptp_time_in_nanoseconds don't matter.
    // As long as the PTP timestamps are from a common source RTP time from different samples can be compared.
    return (uint32_t)rtp_counts;
}

uint64_t CdiUtilityPtpTimestampToMicroseconds(const CdiPtpTimestamp* timestamp_ptr)
{
    return (uint64_t)timestamp_ptr->seconds * 1000000L + (timestamp_ptr->nanoseconds / 1000L);
}
