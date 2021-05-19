// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise internal utility functions.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "internal_utility.h"

#include <assert.h>
#include <stddef.h>

#include "adapter_api.h"
#include "adapter_efa_probe.h"
#include "endpoint_manager.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Enum/String keys for ProbeState. NOTE: Must match ProbeState.
static const EnumStringKey probe_state_key_array[] = {
    { kProbeStateIdle,                   "Idle" },
    { kProbeStateSendReset,              "SendReset" },
    { kProbeStateSendProtocolVersion,    "SendProtocolVersion" },
    { kProbeStateResetting,              "Resetting" },
    { kProbeStateResetDone,              "ResetDone" },
    { kProbeStateWaitForStart,           "WaitForStart" },
    { kProbeStateEfaStart,               "EfaStart"},
    { kProbeStateEfaProbe,               "EFAProbe" },
    { kProbeStateEfaConnected,           "EFAConnected" },
    { kProbeStateEfaConnectedPing,       "EFAPing" },
    { kProbeStateEfaReset,               "EfaReset" },
    { kProbeStateDestroy,                "Destroy" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/String keys for ProbeCommand. NOTE: Must match ProbeCommand.
static const EnumStringKey probe_command_key_array[] = {
    { kProbeCommandReset,     "Reset" },
    { kProbeCommandPing,      "Ping" },
    { kProbeCommandConnected, "Connected" },
    { kProbeCommandAck,       "Ack" },
    { kProbeCommandProtocolVersion, "Protocol Version" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

/// Enum/String keys for EndpointState. NOTE: Must match EndpointState.
static const EnumStringKey endpoint_manager_command_key_array[] = {
    { kEndpointStateIdle,       "Idle" },
    { kEndpointStateReset,      "Reset" },
    { kEndpointStateStart,      "Start" },
    { kEndpointStateShutdown,   "Shutdown" },
    { CDI_INVALID_ENUM_VALUE, NULL } // End of the array
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// @brief Update InternalEnumStringKeyTypes in internal_utility.h whenever an entry is added to this function's switch
/// statement.
static const EnumStringKey* UtilityKeyGetArray(EnumStringKeyTypes key_type) {
    const EnumStringKey* key_array_ptr = NULL;
    switch (key_type) {
        case kKeyProbeState:                key_array_ptr = probe_state_key_array; break;
        case kKeyProbeCommand:              key_array_ptr = probe_command_key_array; break;
        case kKeyEndpointManagerCommand:    key_array_ptr = endpoint_manager_command_key_array; break;
        default: assert(false);
    }
    return key_array_ptr;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

const char* InternalUtilityKeyEnumToString(InternalEnumStringKeyTypes key_type, int enum_value)
{
    const EnumStringKey* key_array = UtilityKeyGetArray(key_type);
    return CdiUtilityEnumValueToString(key_array, enum_value);
}
