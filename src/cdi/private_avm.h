// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions of types and the one global variable used internally by the SDK's
 * implementation. These are details that do not need to be exposed to the user programs via the API.
 */

#ifndef CDI_PRIVATE_AVM_H__
#define CDI_PRIVATE_AVM_H__

#include <stdbool.h>

#include "configuration.h"
#include "cdi_avm_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

// --------------------------------------------------------------------
// All structures in the block below are byte packed (no byte padding).
// --------------------------------------------------------------------
#pragma pack(push, 1)

/**
 * @brief This defines a structure that contains the CDI packet #0 header that is common to all AVM payload
 *        types.
 */
typedef struct {
    CdiAvmExtraData avm_extra_data; ///< Extra data that is sent along with the AVM payload to the receiver.
} CDIPacketAvmCommonHeader;

/**
 * @brief This defines a structure that contains the CDI packet #0 header for a AVM video payload that does not
 *        contain any AVM configuration data.
 */
typedef struct {
    CDIPacketAvmCommonHeader header; ///< Header that is common to all AVM packets that contain a CDI header.
} CDIPacketAvmNoConfig;

/**
 * @brief This defines a structure that contains the CDI packet #0 header for a AVM video payload that contains AVM
 *        configuration data.
 */
typedef struct {
    CDIPacketAvmCommonHeader header; ///< Header that is common to all AVM packets that contain a CDI header.
    CdiAvmConfig config;               ///< Defines the format of the payload.
} CDIPacketAvmWithConfig;

/**
 * @brief Union of AVM payload cdi headers. Use to reserve memory that can be used to hold any type of CDI packet
 *        AVM header.
 */
typedef struct {
    union {
        /// @brief Header that is common to all AVM packets that contain a CDI header.
        CDIPacketAvmCommonHeader common_header;
        /// @brief Header for video payload that does not contain any configuration data.
        CDIPacketAvmNoConfig no_config;
        /// @brief Header for video payload that contains configuration data.
        CDIPacketAvmWithConfig with_config;
    };
} CDIPacketAvmUnion;

#pragma pack(pop)
// --------------------------------------------------------------------
// End of byte packed structures (no byte padding).
// --------------------------------------------------------------------

/// Maximum number of bytes for CDI packet #0 extra data.
#define MAX_CDI_PACKET_EXTRA_DATA       (sizeof(CDIPacketAvmUnion))

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#endif // CDI_PRIVATE_AVM_H__

