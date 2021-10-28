// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef INTERNAL_UTILITY_API_H__
#define INTERNAL_UTILITY_API_H__

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in internal_utility.c.
 */

#include <assert.h>
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

/**
 * Helper function to up-quantize an integer value.
 *
 * @param value   The value to quantize.
 * @param quantum The number to calculate a multiple of.
 *
 * @return The smallest multiple of quantum equal to or greater than value.
 */
static inline int NextMultipleOf(int value, int quantum)
{
    assert(value > 0);
    assert(quantum > 0);
    return quantum * ((value - 1) / quantum + 1);
}

/**
 * Helper function to down-quantize an integer value.
 *
 * @param value   The value to quantize.
 * @param quantum The number to calculate a multiple of.
 *
 * @return The largest multiple of quantum equal to or smaller than value.
 */
static inline int PrevMultipleOf(int value, int quantum)
{
    assert(value > 0);
    assert(quantum > 0);
    return quantum * (value / quantum);
}

#endif // INTERNAL_UTILITY_API_H__
