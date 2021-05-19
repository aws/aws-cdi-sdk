// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file contain utility definitions and function prototypes that are used within the
 * SDK but are not part of the API.
 */

#if !defined UTILITIES_H__
#define UTILITIES_H__

#include <stddef.h>
#include <stdint.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * This macro is used to locate a pointer to the start of a structure given a pointer to the specified member in the
 * structure.
 *
 * @param ptr The address of the contained structure member.
 * @param type The type of the containing structure.
 * @param member The name of the member within the containing structure whose address is ptr.
 *
 * @return The address of the containing structure.
 */
#define CONTAINER_OF(ptr, type, member) \
                    (type*)( (char*)(ptr) - offsetof(type, member) )

/**
 * This macro is used to locate a pointer to a desired member in a structure that is located in the same containing
 * structure.
 *
 * @param ptr The address of the known contained structure member.
 * @param type The type of the containing structure.
 * @param member_pointed_to The name of the member within the containing structure whose address is ptr.
 * @param desired_member The name of the contained member whose address is to be returned.
 *
 * @return The address of the containing structure.
 */
#define CONTAINER_FROM(ptr, type, member_pointed_to, desired_member) \
                       (type*)( (char*)(ptr) + (offsetof(type, desired_member) - offsetof(type, member_pointed_to)) )

/**
 * Evaluates to the number of elements in a statically sized array.
 *
 * @param array The name of the statically sized array variable whose element count is to be returned.
 *
 * @return The number of elements that comprise the array variable.
 */
#define CDI_ARRAY_ELEMENT_COUNT(array) ((int)(sizeof(array) / sizeof(array[0])))

/**
 * Evaluates to the smaller of the two quantities. This is implemented as a macro so that it can be used for any type
 * of arguments so long as they can be compared using the less than operator. Care must be taken when using this macro
 * as the arguments may get evaluated multiple times; don't specify an auto-incremented variable, for example.
 */
#define CDI_MIN(a, b) (((a) < (b)) ? (a) : (b))

/**
 * Evaluates to the larger of the two quantities. This is implemented as a macro so that it can be used for any type
 * of arguments so long as they can be compared using the greater than operator. Care must be taken when using this
 * macro as the arguments may get evaluated multiple times; don't specify an auto-incremented variable, for example.
 */
#define CDI_MAX(a, b) (((a) > (b)) ? (a) : (b))

/// @brief Define used to represent an invalid enum value.
#define CDI_INVALID_ENUM_VALUE  (-1)

/**
 * Return an empty string if the string specified is NULL, otherwise the specified string is returned.
 *
 * @param source_str Pointer to source string
 *
 * @return Pointer to source_str if it is not NULL, otherwise returns a pointer to an empty string.
 */
static inline const char* CdiGetEmptyStringIfNull(const char* source_str)
{
    static const char empty_str[] = "";

    if (NULL == source_str) {
        return empty_str;
    }

    return source_str;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#endif // UTILITIES_H__
