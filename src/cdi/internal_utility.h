// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

#ifndef INTERNAL_UTILITY_API_H__
#define INTERNAL_UTILITY_API_H__

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in internal_utility.c.
 */

#include <stdbool.h>

#include "cdi_utility_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Enums used to indicate which key-value array a function is to access.
 *
 * NOTE: Update InternalUtilityKeyGetArray in internal_utility.c whenever an entry is added to this table.
 */
typedef enum {
    kKeyProbeState,                       ///< Key for ProbeState
    kKeyProbeCommand,                     ///< Key for ProbeCommand
    kKeyEndpointManagerCommand,           ///< Key for EndpointManagerCommand
} InternalEnumStringKeyTypes;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Function used to convert an internal enum value to a string.
 *
 * @param key_type Enum from InternalEnumStringKeyTypes which indicates which key-value array to search for enum_value.
 * @param enum_value Value to convert to a string.
 *
 * @return Pointer to returned string. If no match was found, NULL is returned.
 */
const char* InternalUtilityKeyEnumToString(InternalEnumStringKeyTypes key_type, int enum_value);

#endif // INTERNAL_UTILITY_API_H__
