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

#include <assert.h>
#include <stddef.h>

#include "cdi_baseline_profile_api.h"
#include "cdi_core_api.h"
#include "cdi_test_unit_api.h"
#include "cdi_os_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief How many times an RTP counter increments before rolling over. An RTP timestamp is specified in IETF RFC 3550
/// as a 32 bit value.
#define RTP_ROLLOVER_COUNT 0x100000000ULL

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/string keys for CdiAdapterTypeSelection.
static const CdiEnumStringKey adapter_type_key_array[] = {
    { kCdiAdapterTypeEfa,             "EFA" },
    { kCdiAdapterTypeSocket,          "SOCKET" },
    { kCdiAdapterTypeSocketLibfabric, "SOCKET_LIBFABRIC" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiBufferType.
static const CdiEnumStringKey buffer_type_key_array[] = {
    { kCdiLinearBuffer, "LINEAR" },
    { kCdiSgl,          "SGL" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/String keys for ConnectionProtocolType.
static const CdiEnumStringKey protocols_key_array[] = {
    { kProtocolTypeRaw, "RAW" },
    { kProtocolTypeAvm, "AVM" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogMethod.
static const CdiEnumStringKey log_method_key_array[] = {
    { kLogMethodStdout,   "LogMethodStdout" },
    { kLogMethodCallback, "LogMethodCallback" },
    { kLogMethodFile,     "LogMethodFile" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogComponent.
static const CdiEnumStringKey log_component_key_array[] = {
    { kLogComponentGeneric,            "GENERIC" },
    { kLogComponentPayloadConfig,      "PAYLOAD_CONFIG" },
    { kLogComponentPerformanceMetrics, "PERFORMANCE_METRICS" },
    { kLogComponentProbe,              "PROBE" },
    { kLogComponentEndpointManager,    "ENDPOINT_MANAGER" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiLogLevel.
static const CdiEnumStringKey log_level_key_array[] = {
    { kLogFatal,    "FATAL" },
    { kLogCritical, "CRITICAL" },
    { kLogError,    "ERROR" },
    { kLogWarning,  "WARNING" },
    { kLogInfo,     "INFO" },
    { kLogVerbose,  "VERBOSE" },
    { kLogDebug,    "DEBUG" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiConnectionStatus.
static const CdiEnumStringKey connection_status_key_array[] = {
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

// Update CdiEnumStringKeyType in cdi_utility_api.h whenever an entry is added to this function's switch statement.
const CdiEnumStringKey* CdiUtilityKeyGetArray(CdiEnumStringKeyType key_type) {
    const CdiEnumStringKey* key_array_ptr = NULL;
    switch (key_type) {
        case kKeyAdapterType:                 key_array_ptr = adapter_type_key_array; break;
        case kKeyBufferType:                  key_array_ptr = buffer_type_key_array; break;
        case kKeyConnectionProtocolType:      key_array_ptr = protocols_key_array; break;
        case kKeyLogMethod:                   key_array_ptr = log_method_key_array; break;
        case kKeyLogComponent:                key_array_ptr = log_component_key_array; break;
        case kKeyLogLevel:                    key_array_ptr = log_level_key_array; break;
        case kKeyConnectionStatus:            key_array_ptr = connection_status_key_array; break;
        case kKeyTestUnit:                    key_array_ptr = CdiTestUnitGetKeyArray(); break;
    }
    assert(NULL != key_array_ptr);
    return key_array_ptr;
}

const char* CdiUtilityEnumValueToString(const CdiEnumStringKey* key_array, int enum_value)
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

int CdiUtilityStringToEnumValue(const CdiEnumStringKey* key_array, const char* name_str)
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

const char* CdiUtilityKeyEnumToString(CdiEnumStringKeyType key_type, int enum_value)
{
    const CdiEnumStringKey* key_array = CdiUtilityKeyGetArray(key_type);
    return CdiUtilityEnumValueToString(key_array, enum_value);
}

int CdiUtilityKeyStringToEnum(CdiEnumStringKeyType key_type, const char* name_str)
{
    const CdiEnumStringKey *key_array = CdiUtilityKeyGetArray(key_type);
    return CdiUtilityStringToEnumValue(key_array, name_str);
}

uint32_t CdiUtilityPtpToRtp(const CdiPtpTimestamp* ptp_timestamp_ptr, uint32_t sample_rate)
{
    uint64_t ptp_time_ns = (ptp_timestamp_ptr->seconds * CDI_NANOSECONDS_PER_SECOND) + ptp_timestamp_ptr->nanoseconds;
    // RTP counter is a 32 bit counter counting at sample_rate samples per seconds.
    uint64_t rtp_rollover_time_ns = ((CDI_NANOSECONDS_PER_SECOND * RTP_ROLLOVER_COUNT) + (sample_rate/2)) / sample_rate;
    // Get the number of nanoseconds since the last rollover occurred and convert that to rtp samples.
    uint64_t rtp_counts = ((ptp_time_ns % rtp_rollover_time_ns) * sample_rate) / CDI_NANOSECONDS_PER_SECOND;
    // RTP timestamp is truncated to 32 bits so any lost upper bits on ptp_time_in_nanoseconds don't matter.
    // As long as the PTP timestamps are from a common source RTP time from different samples can be compared.
    return (uint32_t)rtp_counts;
}

uint64_t CdiUtilityPtpTimestampToMicroseconds(const CdiPtpTimestamp* timestamp_ptr)
{
    return (uint64_t)timestamp_ptr->seconds * 1000000L + (timestamp_ptr->nanoseconds / 1000L);
}
