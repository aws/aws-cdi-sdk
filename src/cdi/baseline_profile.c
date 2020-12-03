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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cdi_baseline_profile_api.h"
#include "cdi_logger_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Used to determine if static instance data used within this file has been initialized.
static volatile bool initialized = false;

/// @brief Statically allocated mutex used to make initialization of profile data thread-safe.
static CdiStaticMutexType mutex_lock = CDI_STATIC_MUTEX_INITIALIZER;

/// @brief Value used to define the maximum number of profiles stored for each profile type.
#define PROFILES_MAX    (10)

/// @brief Type used to hold baseline profile version and V-table to required APIs.
typedef struct {
    CdiAvmBaselineProfileVersion version; ///< Profile version.
    CdiAvmVTableApi vtable_api; ///< Profile V-table API.
} BaselineProfileData;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// @brief Array of baseline profiles, with version and v-tables for each one. Indexed by profile type.
static BaselineProfileData baseline_profile_array[CDI_BASELINE_AVM_PAYLOAD_TYPE_ENUM_COUNT][PROFILES_MAX] = {0};

/// @brief Array of number of profiles for each profile type.
static int profile_type_count_array[CDI_BASELINE_AVM_PAYLOAD_TYPE_ENUM_COUNT] = {0};

/**
 * Table for converting between the supported AVM media types and the URIs associated with them.
 */
static const EnumStringKey avm_uri_strings[] = {
    // kCdiAvmNotBaseline is intentionally missing.
    { kCdiAvmVideo,     "https://cdi.elemental.com/specs/baseline-video" },
    { kCdiAvmAudio,     "https://cdi.elemental.com/specs/baseline-audio" },
    { kCdiAvmAncillary, "https://cdi.elemental.com/specs/baseline-ancillary-data" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/string keys for CdiAvmPayloadType.
static const EnumStringKey payload_type_key_array[] = {
    { kCdiAvmNotBaseline, "not baseline" },
    { kCdiAvmVideo,       "Video" },
    { kCdiAvmAudio,       "Audio" },
    { kCdiAvmAncillary,   "Ancillary" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Convert an enum key type to a profile type (same thing as payload type).
 *
 * @param key_type Enum value of key type to convert.
 *
 * @return Enum value of profile type.
 */
static CdiBaselineAvmPayloadType EnumStringKeyTypeToPayloadType(CdiAvmBaselineEnumStringKeyTypes key_type)
{
    CdiBaselineAvmPayloadType payload_type = kCdiAvmNotBaseline;

    switch (key_type) {
        case kKeyAvmPayloadType:
            // Nothing special needed.
            break;
        case kKeyAvmVideoSamplingType:
        case kKeyAvmVideoAlphaChannelType:
        case kKeyAvmVideoBitDepthType:
        case kKeyAvmVideoColorimetryType:
        case kKeyAvmVideoTcsType:
        case kKeyAvmVideoRangeType:
            payload_type = kCdiAvmVideo;
            break;
        case kKeyAvmAudioChannelGroupingType:
        case kKeyAvmAudioSampleRateType:
            payload_type = kCdiAvmAudio;
            break;
    }

    return payload_type;
}

/**
 * Initialize the AVM layer of the CDI-SDK.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus InitializeBaselineProfiles(void)
{
    CdiReturnStatus ret = kCdiStatusOk;

    // Register profiles based on 01.00.
    extern CdiReturnStatus RegisterAvmBaselineProfiles_1_00(void);
    ret = RegisterAvmBaselineProfiles_1_00();

    if (kCdiStatusOk == ret) {
        // Register profiles based on 02.00.
        extern CdiReturnStatus RegisterAvmBaselineProfiles_2_00(void);
        ret = RegisterAvmBaselineProfiles_2_00();
    }

    if (kCdiStatusOk != ret) {
        CDI_LOG_THREAD(kLogError, "Failed to initialize baseline profiles. Error[%s].", ret);
    }

    return ret;
}

/**
 * @brief Find the baseline profile for the specified payload type and version.
 *
 * @param payload_type Enum value of payload type (same as profile type)
 * @param version_ptr Pointer to version to find. If NULL, finds first profile in table.
 *
 * @return Pointer to found profile. NULL if none found.
 */
static BaselineProfileData* FindProfileVersion(CdiBaselineAvmPayloadType payload_type,
                                               const CdiAvmBaselineProfileVersion* version_ptr)
{
    BaselineProfileData* profile_data_ptr = NULL;

    // profile_type_count_array and baseline_profile_array are indexed with kCdiAvmVideo at element 0.
    const int payload_type_idx = payload_type - kCdiAvmVideo;

    // No need to use lock if we have already completed initialization.
    if (!initialized) {
        CdiOsStaticMutexLock(mutex_lock);
        if (!initialized) {
            InitializeBaselineProfiles();
            initialized = true;
        }
        CdiOsStaticMutexUnlock(mutex_lock);
    }

    // If desired version is NULL or 0.0, then default to first profile (ie. 1.0).
    if (NULL == version_ptr || (0 == version_ptr->major && 0 == version_ptr->minor)) {
        if (profile_type_count_array[payload_type_idx]) {
            profile_data_ptr = &baseline_profile_array[payload_type_idx][0];
        }
    } else {
        // Try to find matching profile version.
        for (int i = 0; i < profile_type_count_array[payload_type_idx]; i++) {
            BaselineProfileData* tmp_ptr = &baseline_profile_array[payload_type_idx][i];
            if (tmp_ptr->version.major == version_ptr->major &&
                tmp_ptr->version.minor == version_ptr->minor) {
                profile_data_ptr = tmp_ptr;
                break;
            }
        }
    }

    if (NULL == profile_data_ptr) {
        CDI_LOG_THREAD(kLogWarning, "Unable to find baseline profile v[%02d.%02d] for payload type[%s].",
                       version_ptr->major, version_ptr->minor,
                       CdiAvmKeyEnumToString(kKeyAvmPayloadType, payload_type, NULL));
    }

    return profile_data_ptr;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

////////////////////////////////////////////////////////////////////////////////
// Doxygen commenting for these functions is in cdi_baseline_profile_api.h.
////////////////////////////////////////////////////////////////////////////////

CdiReturnStatus CdiAvmRegisterBaselineProfile(CdiBaselineAvmPayloadType profile_type,
                                              const char* profile_version_str,
                                              CdiAvmVTableApi* vtable_api_ptr)
{
    CdiReturnStatus ret = kCdiStatusOk;

    // profile_type_count_array and baseline_profile_array are indexed with kCdiAvmVideo at element 0.
    const int profile_type_idx = profile_type - kCdiAvmVideo;

    CdiAvmBaselineProfileVersion version;
    if (!CdiAvmParseBaselineVersionString(profile_version_str, &version)) {
        CDI_LOG_THREAD(kLogError, "Unable to parse version string[%s]. Expected format is: xx.xx", profile_version_str);
        return kCdiStatusFatal;
    }

    if (kCdiAvmNotBaseline == profile_type) {
            ret = kCdiStatusFatal;
    }

    if (kCdiStatusOk == ret) {
        int count = profile_type_count_array[profile_type_idx];
        if (count < (int)CDI_ARRAY_ELEMENT_COUNT(baseline_profile_array[profile_type_idx])) {
            baseline_profile_array[profile_type_idx][count].version = version;
            baseline_profile_array[profile_type_idx][count].vtable_api = *vtable_api_ptr;
            profile_type_count_array[profile_type_idx]++;
        } else {
            ret = kCdiStatusArraySizeExceeded;
        }
    }

    return ret;
}

CdiReturnStatus CdiAvmMakeBaselineConfiguration(const CdiAvmBaselineConfig* baseline_config_ptr,
                                                CdiAvmConfig* config_ptr, int* payload_unit_size_ptr)
{
    return CdiAvmMakeBaselineConfiguration2((CdiAvmBaselineConfigCommon*)baseline_config_ptr, config_ptr,
                                            payload_unit_size_ptr);
}

CdiReturnStatus CdiAvmMakeBaselineConfiguration2(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                                 CdiAvmConfig* config_ptr, int* payload_unit_size_ptr)
{
    CdiReturnStatus ret = kCdiStatusFatal;

    // Zero out the whole structure for a clean start.
    memset(config_ptr, 0, sizeof(*config_ptr));

    BaselineProfileData* profile_data_ptr = FindProfileVersion(baseline_config_ptr->payload_type,
                                                               &baseline_config_ptr->version);
    if (profile_data_ptr) {
        // Copy the URI.
        strncpy(config_ptr->uri, CdiUtilityEnumValueToString(avm_uri_strings, baseline_config_ptr->payload_type),
                                                             sizeof(config_ptr->uri) - 1);

        if ((profile_data_ptr->vtable_api.make_config_ptr)(baseline_config_ptr, config_ptr, payload_unit_size_ptr)) {
            ret = kCdiStatusOk;
        }
    }

    return ret;
}

CdiReturnStatus CdiAvmParseBaselineConfiguration(const CdiAvmConfig* config_ptr,
                                                 CdiAvmBaselineConfig* baseline_config_ptr)
{
    return CdiAvmParseBaselineConfiguration2(config_ptr, (CdiAvmBaselineConfigCommon*)baseline_config_ptr);
}

CdiReturnStatus CdiAvmParseBaselineConfiguration2(const CdiAvmConfig* config_ptr,
                                                  CdiAvmBaselineConfigCommon* baseline_config_ptr)
{
    CdiReturnStatus ret = kCdiStatusNonFatal;

    // Ensure that uri and data_size meet specifications.
    if ((sizeof(config_ptr->uri) - 1) <= strlen(config_ptr->uri)) {
        CDI_LOG_THREAD(kLogError, "uri string length[%u] exceeds specification[%u]", strlen(config_ptr->uri),
                       sizeof(config_ptr->uri));
    } else if ((int)sizeof(config_ptr->data) < config_ptr->data_size) {
        CDI_LOG_THREAD(kLogError, "data_size value[%u] exceeds specification[%u]", config_ptr->data_size,
                       sizeof(config_ptr->data));
    } else {
        int key = CDI_INVALID_ENUM_VALUE;
        if (NULL != config_ptr) {
            key = CdiUtilityStringToEnumValue(avm_uri_strings, config_ptr->uri);
        }
        CdiAvmBaselineConfigCommon common_config = { 0 };
        if (CDI_INVALID_ENUM_VALUE != key) {
            // This is a valid baseline profile, set its type.
            common_config.payload_type = (CdiBaselineAvmPayloadType)key;

            // Parse version info, so we can tell which baseline profile parser to use.
            static const char profile_str[] = "cdi_profile_version=";
            const char* found_str = strstr((const char*)config_ptr->data, profile_str);
            if (found_str) {
                found_str += strlen(profile_str);
                char str_buf[6];
                found_str += CdiOsStrCpy(str_buf, sizeof(str_buf), found_str);
                if (';' != *found_str) {
                    CDI_LOG_THREAD(kLogError, "Expected ';' at end of version 'xx.xx'. Found[%s].", found_str);
                } else if (!CdiAvmParseBaselineVersionString(str_buf, &common_config.version)) {
                    CDI_LOG_THREAD(kLogError, "Unable to parse profile version 'xx.xx'. Found[%s].", str_buf);
                } else {
                    // Successfully parsed the version, now try to find the corresponding profile.
                    BaselineProfileData* profile_data_ptr = FindProfileVersion(common_config.payload_type,
                                                                               &common_config.version);
                    if (profile_data_ptr) {
                        // Clear the entire structure then plug in the payload type and version number.
                        memset(baseline_config_ptr, 0, profile_data_ptr->vtable_api.structure_size);
                        baseline_config_ptr->payload_type = common_config.payload_type;
                        baseline_config_ptr->version = common_config.version;

                        // Have the version and payload type specific function fill in the rest.
                        if ((profile_data_ptr->vtable_api.parse_config_ptr)(config_ptr, baseline_config_ptr)) {
                            ret = kCdiStatusOk; // Successfully parsed, so change ret status to ok.
                        }
                    } else {
                        ret = kCdiStatusProfileNotSupported;
                    }
                }
            } else {
                CDI_LOG_THREAD(kLogError, "Unable to parse profile version string 'cdi_profile_version='.");
            }
        }
    }

    if (kCdiStatusOk != ret) {
        baseline_config_ptr->payload_type = kCdiAvmNotBaseline;
    }

    return ret;
}

CdiReturnStatus CdiAvmGetBaselineUnitSize(const CdiAvmBaselineConfig* baseline_config_ptr,
                                          int* payload_unit_size_ptr)
{
    return CdiAvmGetBaselineUnitSize2((CdiAvmBaselineConfigCommon*)baseline_config_ptr, payload_unit_size_ptr);
}

CdiReturnStatus CdiAvmGetBaselineUnitSize2(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                           int* payload_unit_size_ptr)
{
    CdiReturnStatus ret = kCdiStatusNonFatal;

    BaselineProfileData* profile_data_ptr = FindProfileVersion(baseline_config_ptr->payload_type,
                                                                &baseline_config_ptr->version);
    if (profile_data_ptr) {
        ret = (profile_data_ptr->vtable_api.get_unit_size_ptr)(baseline_config_ptr, payload_unit_size_ptr);
    }

    return ret;
}

const char* CdiAvmKeyEnumToString(CdiAvmBaselineEnumStringKeyTypes key_type, int enum_value,
                                  const CdiAvmBaselineProfileVersion* version_ptr)
{
    const EnumStringKey* array_ptr = CdiAvmKeyGetArray(key_type, version_ptr);
    if (array_ptr) {
        return CdiUtilityEnumValueToString(array_ptr, enum_value);
    }
    return NULL;
}

int CdiAvmKeyStringToEnum(CdiAvmBaselineEnumStringKeyTypes key_type, const char* name_str,
                          const CdiAvmBaselineProfileVersion* version_ptr)
{
    const EnumStringKey* array_ptr = CdiAvmKeyGetArray(key_type, version_ptr);
    if (array_ptr) {
        return CdiUtilityStringToEnumValue(array_ptr, name_str);
    }
    return CDI_INVALID_ENUM_VALUE;
}

const EnumStringKey* CdiAvmKeyGetArray(CdiAvmBaselineEnumStringKeyTypes key_type,
                                       const CdiAvmBaselineProfileVersion* version_ptr)
{
    const EnumStringKey* array_ptr = NULL;

    if (kKeyAvmPayloadType == key_type) {
        array_ptr = payload_type_key_array;
    } else {
        CdiBaselineAvmPayloadType payload_type = EnumStringKeyTypeToPayloadType(key_type);
        BaselineProfileData* profile_data_ptr = FindProfileVersion(payload_type, version_ptr);
        if (profile_data_ptr) {
            array_ptr = (profile_data_ptr->vtable_api.key_get_array_ptr)(key_type);
        }
    }

    return array_ptr;
}

bool CdiAvmParseBaselineVersionString(const char* version_str, CdiAvmBaselineProfileVersion* ret_version_ptr)
{
    bool ret = false;

    bool minor = false;
    int accumulator = 0;
    for (int i = 0; i <= 5; i++) {
        const char c = version_str[i];
        if (minor && ('\0' == c)) {
            ret_version_ptr->minor = accumulator;
            ret = true;
        } else if (!minor && ('.' == c)) {
            ret_version_ptr->major = accumulator;
            accumulator = 0;
            minor = true;
        } else if (('0' <= c) && ('9' >= c)) {
            accumulator = accumulator * 10 + (c - '0');
        } else {
            break;
        }
    }

    return ret;
}
