// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

#ifndef CDI_BASELINE_PROFILE_API_H__
#define CDI_BASELINE_PROFILE_API_H__

/**
 * @file
 * @brief
 * This file contains declarations and definitions for the CDI AVM baseline profile API functions.
 */

#include <stdbool.h>
#include <stddef.h>

#include "cdi_avm_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Structure containing the version information common to audio, video, and metadata in the baseline
/// configuration structure.
typedef struct {
    int major; ///< @brief The major number part; this increments to indicate breaking changes.
    int minor; ///< @brief The minor number part; this increments for trivial, non-breaking changes (e.g., corrections).
} CdiAvmBaselineProfileVersion;

/// @brief Payload types used in CdiAvmRxData.
/// NOTE: Any changes made here MUST also be made to "payload_type_key_array" in cdi_utility_api.c and "avm_uri_strings"
/// in baseline_profile.c.
typedef enum {
    kCdiAvmNotBaseline, ///< Payload contains data for some type other than baseline profile.
    kCdiAvmVideo,       ///< Payload contains video data.
    kCdiAvmAudio,       ///< Payload contains audio data.
    kCdiAvmAncillary,   ///< Payload contains ancillary data.
} CdiBaselineAvmPayloadType;

/// @brief Macro used to determine number of enumerated values in CdiBaselineAvmPayloadType, without having to add a
/// "kCdiAvmLast" enum value.
#define CDI_BASELINE_AVM_PAYLOAD_TYPE_ENUM_COUNT    (kCdiAvmAncillary)

/// @brief Structure that is common to all baseline profile versions. This allows the APIs to determine payload type and
/// profile version. Once that has been determined, the structure can then be used by baseline version specific logic
/// where it is cast to the appropriate type.
typedef struct {
    CdiBaselineAvmPayloadType payload_type; ///< @brief Indicates which payload type.
    CdiAvmBaselineProfileVersion version;  ///< Baseline profile version.
} CdiAvmBaselineConfigCommon;

/**
 * @brief Enums used to indicate which key-value array a function is to access.
 *
 * NOTE: Update CdiAvmKeyGetArray() in baseline_profie.c whenever an entry is added to this table.
 */
typedef enum {
    /// Keys that contain one set of unique data (not specific to a profile or version). See CdiAvmKeyGetArray().
    kKeyAvmPayloadType,                 ///< Key for CdiAvmPayloadType

    /// Keys used by video profiles. See EnumStringKeyTypeToPayloadType().
    kKeyAvmVideoSamplingType,           ///< Key for CdiAvmVideoSampling
    kKeyAvmVideoAlphaChannelType,       ///< Key for CdiAvmVideoAlphaChannel
    kKeyAvmVideoBitDepthType,           ///< Key for CdiAvmVideoBitDepth
    kKeyAvmVideoColorimetryType,        ///< Key for CdiAvmColorimetry
    kKeyAvmVideoTcsType,                ///< Key for CdiAvmVideoTcs
    kKeyAvmVideoRangeType,              ///< Key for CdiAvmVideoRange

    // Keys used by audio profiles. See EnumStringKeyTypeToPayloadType().
    kKeyAvmAudioChannelGroupingType,    ///< Key for CdiAvmAudioChannelGrouping
    kKeyAvmAudioSampleRateType,         ///< Key for CdiAvmAudioSampleRate
} CdiAvmBaselineEnumStringKeyTypes;

/// Forward reference of structure to create pointers later.
typedef struct CdiAvmBaselineConfig CdiAvmBaselineConfig;

/**
 * @brief Prototype of function used to make a baseline configuration string from a configuration structure.
 *
 * @param baseline_config_ptr A pointer to common baseline configuration data.
 * @param config_ptr A pointer to AVM configuration data.
 * @param payload_unit_size_ptr Address where to write returned payload unit size.
 *
 * @return true is returned if conversion is successful.
 */
typedef bool (*CdiMakeBaselineConfigurationPtr)(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                                CdiAvmConfig* config_ptr, int* payload_unit_size_ptr);
/**
 * @brief Prototype of function used to parse a baseline configuration string and generate a configuration structure.
 *
 * @param config_ptr A pointer to AVM configuration data.
 * @param baseline_config_ptr A pointer to common baseline configuration data.
 *
 * @return true is returned if conversion is successful.
 */
typedef bool (*CdiParseBaselineConfigurationPtr)(const CdiAvmConfig* config_ptr,
                                                 CdiAvmBaselineConfigCommon* baseline_config_ptr);

/**
 * @brief Prototype of function used to get baseline unit size.
 *
 * @param baseline_config_ptr A pointer to common baseline configuration data.
 * @param payload_unit_size_ptr Address where to write returned unit size.
 *
 * @return Status code.
 */
typedef CdiReturnStatus (*CdiAvmGetBaselineUnitSizePtr)(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                                        int* payload_unit_size_ptr);

/**
 * @brief Prototype of function used to get enum/string table for the specified key.
 *
 * @param key_type Enumerated key type.
 *
 * @return Pointer to enum/string keypair table. If none is found, NULL is returned.
 */
typedef const EnumStringKey* (*CdiAvmKeyGetArrayPtr)(CdiAvmBaselineEnumStringKeyTypes key_type);

/**
 * @brief Type used to hold V-table of APIs that must be implemented by baseline profiles.
 */
typedef struct {
    CdiMakeBaselineConfigurationPtr make_config_ptr; ///< Function pointer used to make configuration string.
    CdiParseBaselineConfigurationPtr parse_config_ptr; ///< Function pointer used to parse configuration string.
    CdiAvmGetBaselineUnitSizePtr get_unit_size_ptr; ///< Function pointer used to get unit size.
    CdiAvmKeyGetArrayPtr key_get_array_ptr; ///< Function pointer used to get key/string array.
    int structure_size; ///< Number of bytes in the baseline configuration structure.
} CdiAvmVTableApi;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the AVM layer of the CDI-SDK. NOTE: Must be called before using any of the other AVM baseline profile
 * APIs.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmInitializeBaselineProfiles(void);

/**
 * @brief Register a baseline profile.
 *
 * @param profile_type Enum which indicates the type of profile (ie. video, audio or ancillary data).
 * @param profile_version_str Pointer to profile version string. Must be in "xx.xx" format.
 * @param vtable_api_ptr Pointer to V-table to use for required baseline profile APIs.
 *
 * @return kCdiStatusOk if ok. kCdiStatusArraySizeExceeded if array size has been exceeded, otherwise kCdiStatusFatal.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmRegisterBaselineProfile(CdiBaselineAvmPayloadType profile_type,
                                                            const char* profile_version_str,
                                                            CdiAvmVTableApi* vtable_api_ptr);

/**
 * Converts from the simple, constrained baseline configuration structures to the more general purpose CdiAvmConfig
 * structure used in the AVM interface. This is useful on the transmit side for streams that comply with the baseline
 * CDI profile for inter-vendor compatibility.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param baseline_config_ptr Address of the source configuration structure.
 * @param config_ptr Address of where to write the resulting AVM configuration structure.
 * @param payload_unit_size_ptr Address of where to write the payload unit size which is used by the transmit
 *                              packetizer. This value should be passed in the unit_size member of the
 *                              payload_config_ptr->core_config_data structure when calling CdiAvmTxPayload().
 *
 * @return CdiReturnStatus kCdiStatusOk if the conversion was successful, kCdiStatusFatal if config_ptr is NULL or the
 *         baseline configuration
 */
CDI_INTERFACE CdiReturnStatus CdiAvmMakeBaselineConfiguration(const CdiAvmBaselineConfig* baseline_config_ptr,
                                                              CdiAvmConfig* config_ptr, int* payload_unit_size_ptr);

/**
 * @brief New version of the existing CdiAvmMakeBaselineConfiguration() API that supports a generic extensible baseline
 * configuration structure. For more details see CdiAvmMakeBaselineConfiguration().
 */
CDI_INTERFACE CdiReturnStatus CdiAvmMakeBaselineConfiguration2(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                                               CdiAvmConfig* config_ptr, int* payload_unit_size_ptr);

/**
 * @brief Converts from the AVM configuration structure to the CDI baseline configuration structure if possible. This is
 * to be called on the receive side if the CdiAvmConfig structure is provided to the registered receive payload callback
 * function. This function should be called whenever the pointer is non-NULL as the first step in determining the
 * stream's configuration. If it returns kCdiStatusOk, then the configuration belongs to the CDI baseline profile
 * described by the structure filled in at baseline_config_ptr. If kCdiStatusFatal is returned, the provided
 * configuration does not belong to the baseline profile and therefore needs to be decoded in an application specific
 * manner if other profiles are supported by it.
 *
 * @param config_ptr Pointer to the AVM configuration structure provided to the registered receive payload callback
 *                   function.
 * @param baseline_config_ptr Address of where to write the baseline configuration structure parameters if the
 *                            configuration belongs to the CDI baseline profile.
 *
 * @return kCdiStatusOk if the conversion was successful; kCdiStatusFatal if baseline_config_ptr is NULL,
 *         config_ptr->uri isn't NUL terminated, config_ptr->data_size exceeds sizeof(config_ptr->data) or if
 *         config_ptr->data could not be decoded; or kCdiStatusNonFatal if config_ptr->uri does not belong to the CDI
 *         baseline profile.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmParseBaselineConfiguration(const CdiAvmConfig* config_ptr,
                                                               CdiAvmBaselineConfig* baseline_config_ptr);

/**
 * New version of the existing CdiAvmParseBaselineConfiguration() API that supports a generic extensible baseline
 * configuration structure. For more details see CdiAvmParseBaselineConfiguration().
 */
CDI_INTERFACE CdiReturnStatus CdiAvmParseBaselineConfiguration2(const CdiAvmConfig* config_ptr,
                                                                CdiAvmBaselineConfigCommon* baseline_config_ptr);

/**
 * @brief Gets the unit size for transmission of the media as specified by the configuration structure.
 *
 * @param baseline_config_ptr The address of a configuration structure whose unit size is of interest.
 * @param payload_unit_size_ptr Pointer to location where the unit size is to be written.
 *
 * @return CdiReturnStatus kCdiStatusOk if the configuration structure was correct enough to determine a unit size,
 *         otherwise kCdiStatusFatal.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmGetBaselineUnitSize(const CdiAvmBaselineConfig* baseline_config_ptr,
                                                        int* payload_unit_size_ptr);

/**
 * @brief New version of the existing CdiAvmGetBaselineUnitSize() API that supports a generic extensible baseline
 * configuration structure. For more details see CdiAvmGetBaselineUnitSize().
 */
CDI_INTERFACE CdiReturnStatus CdiAvmGetBaselineUnitSize2(const CdiAvmBaselineConfigCommon* baseline_config_ptr,
                                                         int* payload_unit_size_ptr);

/**
 * Function used to convert an enum value to a string.
 *
 * @param key_type Enum which indicates which key-value array to search for enum_value.
 * @param enum_value Value to convert to a string.
 * @param version_ptr Pointer to profile version to use. If NULL, profile version 01.00 is used.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
CDI_INTERFACE const char* CdiAvmKeyEnumToString(CdiAvmBaselineEnumStringKeyTypes key_type, int enum_value,
                                                const CdiAvmBaselineProfileVersion* version_ptr);

/**
 * Function used to convert a string to a matching enum value.
 *
 * @param key_type Enum which indicates which key-value array to search for name_str.
 * @param name_str Pointer to string name of enumerated value.
 * @param version_ptr Pointer to profile version to use. If NULL, profile version 01.00 is used.
 *
 * @return Returned enumerated value. If no match was found, CDI_INVALID_ENUM_VALUE is returned.
 */
CDI_INTERFACE int CdiAvmKeyStringToEnum(CdiAvmBaselineEnumStringKeyTypes key_type, const char* name_str,
                                        const CdiAvmBaselineProfileVersion* version_ptr);

/**
 * Function used to get a pointer to a key-value array of a type specified by key_type.
 *
 * @param key_type Enum from EnumStringKeyTypes which indicates which key-value array to return.
 * @param version_ptr Pointer to profile version to use. If NULL, profile version 01.00 is used.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
CDI_INTERFACE const EnumStringKey* CdiAvmKeyGetArray(CdiAvmBaselineEnumStringKeyTypes key_type,
                                                     const CdiAvmBaselineProfileVersion* version_ptr);

/**
 * Converts a string representing a baseline configuration structure version number in the form of "xx.yy" into a
 * structure with major and minor members.
 *
 * @param version_str The source string to convert.
 * @param ret_version_ptr The address of the structure to write the version numbers into.
 *
 * @return true if the conversion was successful, false if a failure was encountered.
 */
bool CdiAvmParseBaselineVersionString(const char* version_str, CdiAvmBaselineProfileVersion* ret_version_ptr);

#ifdef __cplusplus
}
#endif

#endif // CDI_BASELINE_PROFILE_API_H__
