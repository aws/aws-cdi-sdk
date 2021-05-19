// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter_efa_probe_rx.c.
 */

#ifndef CDI_ADAPTER_EFA_PROBE_RX_H__
#define CDI_ADAPTER_EFA_PROBE_RX_H__

#include "adapter_api.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Forward declaration of structure to create pointers.
typedef struct CdiRawProbeHeader CdiRawProbeHeader;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Process a probe packet completion message from the receiver EFA endpoint.
 *
 * NOTE: This function is called from PollThread().
 *
 * @param param_ptr Pointer to probe endpoint state data (ProbeEndpointState*).
 * @param packet_ptr Pointer to packet.
 * @param message_type Endpoint message type.
 */
void ProbeRxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

/**
 * Process a control packet completion message received from an endpoint.
 *
 * NOTE: This function is called from SocketReceiveThread().
 *
 * @param param_ptr Pointer to probe endpoint state data (AdapterEndpointState*).
 * @param packet_ptr Pointer to packet containing the control message.
 */
void ProbeRxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr);

/**
 * Process control message for Rx connection.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param probe_hdr_ptr Pointer to control message header.
 * @param source_address_ptr Pointer to source address structure (sockaddr_in).
 * @param wait_timeout_ms_ptr Pointer to current wait timeout. This function may alter the contents of the value.
 *
 * @return True if new probe state has been set, otherwise false is returned.
 */
bool ProbeRxControlProcessPacket(ProbeEndpointState* probe_ptr, const CdiDecodedProbeHeader* probe_hdr_ptr,
                                 const struct sockaddr_in* source_address_ptr, uint64_t* wait_timeout_ms_ptr);

/**
 * Called when the wait timeout period has expired. Time to process the current Rx probe state.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @return Wait timeout value.
 */
int ProbeRxControlProcessProbeState(ProbeEndpointState* probe_ptr);

#endif  // CDI_ADAPTER_EFA_PROBE_RX_H__
