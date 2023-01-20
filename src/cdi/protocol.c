// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used for payloads.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "protocol.h"

#include <stdbool.h>
#include <string.h>

#include "adapter_api.h"
#include "adapter_efa_probe.h"
#include "internal.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Structure used to hold internal state data used by this module.
 */
typedef struct {
    /// @brief Protocol version data available outside of this module. Define as first element in this structure so we
    /// can typecast a pointer to external_data to this type.
    CdiProtocol external_data;
    CdiProtocolVTableApi* api_ptr; ///< VTable API used to access protocol dependent header data.
} CdiProtocolState;

/**
 * @brief Set protocol version to 1.
 *
 * @param remote_version_ptr Pointer to remote's protocol version information.
 * @param protocol_handle Address where to write protocol version data that is available outside of this module.
 * @param ret_api_ptr Address where to write protocol version VTable API.
 */
extern void ProtocolVersionSet1(const CdiProtocolVersionNumber* remote_version_ptr, CdiProtocolHandle protocol_handle,
                                CdiProtocolVTableApi** ret_api_ptr);
/**
 * @brief Set protocol version 2 if remote is compatible with it.
 *
 * @param remote_version_ptr Pointer to remote's protocol version information.
 * @param protocol_handle Address where to write protocol version data that is available outside of this module.
 * @param ret_api_ptr Address where to write protocol version VTable API. Only used if true is returned.
 *
 * @return true if protocol version 2 is set, otherwise false is returned. Must then fallback to version 1 using
 * PayloadProtocolVersionSet1().
 */
extern bool ProtocolVersionSet2(const CdiProtocolVersionNumber* remote_version_ptr, CdiProtocolHandle protocol_handle,
                                CdiProtocolVTableApi** ret_api_ptr);

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Set negotiated protocol version based on remote version and current version of the SDK.
 *
 * @param remote_version_ptr Pointer to remote's protocol version information.
 * @param state_ptr Address where to write returned protocol state data.
 */
static void ProtocolVersionSetInternal(const CdiProtocolVersionNumber* remote_version_ptr, CdiProtocolState* state_ptr)
{
    // Try to use latest version(s) first. If not compatible, then fall back to lower version.
    if (!ProtocolVersionSet2(remote_version_ptr, &state_ptr->external_data, &state_ptr->api_ptr)) {
        ProtocolVersionSet1(remote_version_ptr, &state_ptr->external_data, &state_ptr->api_ptr);
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ProtocolVersionSetLegacy(CdiProtocolHandle* ret_handle_ptr)
{
    CdiProtocolState* state_ptr = CdiOsMemAllocZero(sizeof(CdiProtocolState));

    // NOTE: Since SDK 1.x ignores the probe_version_num, we use it so later versions of the SDK know that we support
    // additional probe command formats.
    CdiProtocolVersionNumber version = {
        .version_num = 1,
        .major_version_num = 0,
        .probe_version_num = CDI_PROBE_VERSION // Probe version ignored by SDK 1.x
    };

    ProtocolVersionSet1(&version, &state_ptr->external_data, &state_ptr->api_ptr);

    *ret_handle_ptr = (CdiProtocol*)state_ptr;
}

void ProtocolVersionSet(const CdiProtocolVersionNumber* remote_version_ptr, CdiProtocolHandle* ret_handle_ptr)
{
    CdiProtocolState* state_ptr = CdiOsMemAllocZero(sizeof(CdiProtocolState));

    ProtocolVersionSetInternal(remote_version_ptr, state_ptr);

    CDI_LOG_THREAD(kLogDebug, "Creating protocol version[%d.%d.%d]. Local[%d.%d.%d] vs Remote[%d.%d.%d].",
                    state_ptr->external_data.negotiated_version.version_num,
                    state_ptr->external_data.negotiated_version.major_version_num,
                    state_ptr->external_data.negotiated_version.probe_version_num,
                    CDI_PROTOCOL_VERSION, CDI_PROTOCOL_MAJOR_VERSION, CDI_PROBE_VERSION,
                    remote_version_ptr->version_num, remote_version_ptr->major_version_num,
                    remote_version_ptr->probe_version_num);

    *ret_handle_ptr = (CdiProtocol*)state_ptr;
}

void ProtocolVersionDestroy(CdiProtocolHandle protocol_handle)
{
    CdiProtocolState* state_ptr = (CdiProtocolState*)protocol_handle;
    if (state_ptr) {
        CdiOsMemFree(state_ptr);
    }
}

int ProtocolPayloadHeaderInit(CdiProtocolHandle protocol_handle, void* header_ptr, int header_buffer_size,
                              const TxPayloadState* payload_state_ptr)
{
    CdiProtocolState* protocol_ptr = (CdiProtocolState*)protocol_handle;
    return (protocol_ptr->api_ptr->header_init)(header_ptr, header_buffer_size, payload_state_ptr);
}

void ProtocolPayloadHeaderDecode(CdiProtocolHandle protocol_handle, void* encoded_data_ptr, int encoded_data_size,
                                 CdiDecodedPacketHeader* dest_header_ptr)
{
    CdiProtocolState* protocol_ptr = (CdiProtocolState*)protocol_handle;
    (protocol_ptr->api_ptr->header_decode)(encoded_data_ptr, encoded_data_size, dest_header_ptr);
}

void ProtocolPayloadPacketRxReorderInfo(CdiProtocolHandle protocol_handle, const CdiRawPacketHeader* header_ptr,
                                        CdiPacketRxReorderInfo* ret_info_ptr)
{
    CdiProtocolState* protocol_ptr = (CdiProtocolState*)protocol_handle;
    (protocol_ptr->api_ptr->rx_reorder_info)(header_ptr, ret_info_ptr);
}


CdiReturnStatus ProtocolProbeHeaderDecode(const void* encoded_data_ptr, int encoded_data_size,
                                          CdiDecodedProbeHeader* dest_header_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    const CdiProtocolVersionNumber* senders_version_ptr = (CdiProtocolVersionNumber*)encoded_data_ptr;
    if (encoded_data_size < (int)sizeof(CdiProtocolVersionNumber)) {
        CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet that is too small[%d]. Expecting[%d] bytes.",
                       encoded_data_size, (int)sizeof(CdiProtocolVersionNumber));
        rs = kCdiStatusProbePacketInvalidSize;
    } else if (2 == senders_version_ptr->version_num && 0 == senders_version_ptr->major_version_num &&
               0 == senders_version_ptr->probe_version_num) {
        CDI_LOG_THREAD(kLogError, "Remote CDI SDK 2.0.0 is not supported. Upgrade it to a newer version.");
        rs = kCdiStatusNonFatal;
    } else {
        // Get the protocol of the sender's version and use it to decode the probe header.
        CdiProtocolState protocol_state = { 0 };
        ProtocolVersionSetInternal(senders_version_ptr, &protocol_state);
        rs = (protocol_state.api_ptr->probe_decode)(encoded_data_ptr, encoded_data_size, dest_header_ptr);
    }

    return rs;
}

int ProtocolProbeHeaderEncode(CdiProtocolHandle protocol_handle, CdiDecodedProbeHeader* src_header_ptr,
                              CdiRawProbeHeader* dest_header_ptr)
{
    CdiProtocolState* state_ptr = (CdiProtocolState*)protocol_handle;

    // Set version number in the decoded header from the protocol being used.
    src_header_ptr->senders_version = protocol_handle->negotiated_version;

    return (state_ptr->api_ptr->probe_encode)(src_header_ptr, dest_header_ptr);
}
