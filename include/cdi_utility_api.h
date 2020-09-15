// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

#ifndef CDI_UTILITY_API_H__
#define CDI_UTILITY_API_H__

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in cdi_utility_api.c
 * and are meant to provide access to helpful SDK data structures and functions that are not part
 * of the core API functionality.
 *
 */

#include <stdbool.h>
#include <stdint.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Used to define API interface export for windows variant.
#if defined(WIN32)
#define CDI_INTERFACE __declspec (dllexport)
#else
#define CDI_INTERFACE
#endif

/// Used to define an invalid enum found.
#define CDI_INVALID_ENUM_VALUE  (-1)
/// Used to define the maximum length of an audio group enum string.
#define MAX_AUDIO_GROUP_STR_SIZE (10)
/// Number of nanoseconds in a second.
#define NANOSECONDS_TO_SECONDS   (1000000000UL)

/// @brief Forward structure declaration to create pointer to PTP timestamp.
typedef struct CdiPtpTimestamp CdiPtpTimestamp;

/**
 * @brief Type used for holding arrays of enums and related string representations.
 */
typedef struct {
        int enum_value;       ///< Enumerated value.
        const char* name_str; ///< Corresponding string representation.
} EnumStringKey;

/**
 * @brief Enums used to indicate which key-value array a function is to access.
 *
 * NOTE: Update CdiUtilityKeyGetArray in cdi_utility_api.c whenever an entry is added to this table.
 */
typedef enum {
    kKeyAdapterType,                    ///< Key for CdiAdapterTypeSelection
    kKeyBufferType,                     ///< Key for CdiBufferType
    kKeyConnectionProtocolType,         ///< Key for ConnectionProtocolType
    kKeyLogMethod,                      ///< Key for CdiLogMethod
    kKeyLogComponent,                   ///< Key for CdiLogComponent
    kKeyLogLevel,                       ///< Key for CdiLogLevel
    kKeyAvmPayloadType,                 ///< Key for CdiAvmPayloadType
    kKeyAvmVideoSamplingType,           ///< Key for CdiAvmVideoSampling
    kKeyAvmVideoAlphaChannelType,       ///< Key for CdiAvmVideoAlphaChannel
    kKeyAvmVideoBitDepthType,           ///< Key for CdiAvmVideoBitDepth
    kKeyAvmVideoColorimetryType,        ///< Key for CdiAvmColorimetry
    kKeyAvmVideoTcsType,                ///< Key for CdiAvmVideoTcs
    kKeyAvmVideoRangeType,              ///< Key for CdiAvmVideoRange
    kKeyAvmAudioChannelGroupingType,    ///< Key for CdiAvmAudioChannelGrouping
    kKeyAvmAudioSampleRateType,         ///< Key for CdiAvmAudioSampleRate
    kKeyConnectionStatus,               ///< Key for CdiConnectionStatus
} EnumStringKeyTypes;

/**
 * @brief This enumeration is used in the CdiConnectionState structure to indicate what connection layer is being
 * used.
 */
typedef enum {
    kProtocolTypeRaw, ///< Raw connection
    kProtocolTypeAvm, ///< Audio, Video and Metadata (AVM) connection
} ConnectionProtocolType;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Function used to get a pointer to a key-value array of a type specified by key_type.
 *
 * @param key_type Enum from EnumStringKeyTypes which indicates which key-value array to return.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
CDI_INTERFACE const EnumStringKey* CdiUtilityKeyGetArray(EnumStringKeyTypes key_type);

/**
 * Convert an enum value to a string.
 *
 * @param key_array Pointer to array enum/string key pairs to use for the conversion.
 * @param enum_value Value to convert to a string.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
CDI_INTERFACE const char* CdiUtilityEnumValueToString(const EnumStringKey* key_array, int enum_value);

/**
 * Convert a string to a matching enum value.
 *
 * @param key_array Pointer to array enum/string key pairs to use for the conversion.
 * @param name_str Pointer to string name of enumerated value.
 *
 * @return Returned enumerated value. If no match was found, CDI_INVALID_ENUM_VALUE is returned.
 */
CDI_INTERFACE int CdiUtilityStringToEnumValue(const EnumStringKey* key_array, const char* name_str);

/**
 * Function used to convert an enum value to a string.
 *
 * @param key_type Enum from EnumStringKeyTypes which indicates which key-value array to search for enum_value.
 * @param enum_value Value to convert to a string.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
CDI_INTERFACE const char* CdiUtilityKeyEnumToString(EnumStringKeyTypes key_type, int enum_value);

/**
 * Function used to convert a string to a matching enum value.
 *
 * @param key_type Enum from EnumStringKeyTypes which indicates which key-value array to search for name_str.
 * @param name_str Pointer to string name of enumerated value.
 *
 * @return Returned enumerated value. If no match was found, CDI_INVALID_ENUM_VALUE is returned.
 */
CDI_INTERFACE int CdiUtilityKeyStringToEnum(EnumStringKeyTypes key_type, const char* name_str);

/**
 * @brief Function used to convert a PTP timestamp into an RTP timestamp.
 *
 * @param ptp_timestamp_ptr A pointer to a PTP timestamp consisting of seconds and nanoseconds;
 * @param sample_rate       A sample rate value in Hz for the number of samples per second used for the RTP units.
 *
 * @return An RTP timestamp in sample_rate units. If sample_rate is 0 or ptp_timestamp_ptr is NULL 0 is returned.
 */
uint32_t CdiUtilityPtpToRtp(const CdiPtpTimestamp* ptp_timestamp_ptr, uint32_t sample_rate);

/**
 * Convert the specified PTP timestamp into microseconds.
 *
 * @param timestamp_ptr Pointer to PTP timestamp to convert.
 *
 * @return PTP timestamp in microseconds.
 */
uint64_t CdiUtilityPtpTimestampToMicroseconds(const CdiPtpTimestamp* timestamp_ptr);

/**
 * Convenience function to return the human readable string associated with the given boolean value.
 *
 * @param b The value to convert.
 *
 * @return The address of a human readable character string.
 */
static inline const char* CdiUtilityBoolToString(bool b)
{
    return b ? "true" : "false";
}

#endif // CDI_UTILITY_API_H__
