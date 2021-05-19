// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter_efa_probe_tx.c.
 */

#ifndef CDI_ADAPTER_EFA_PROBE_TX_H__
#define CDI_ADAPTER_EFA_PROBE_TX_H__

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
 * Process a probe packet completion message from the transmitter EFA endpoint.
 *
 * NOTE: This function is called from Poll().
 *
 * @param param_ptr Pointer to user parameter.
 * @param packet_ptr Pointer to packet.
 * @param message_type Endpoint message type.
 */
void ProbeTxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

/**
 * Process a control packet completion message from the transmitter probe control interface endpoint.
 *
 * NOTE: This function is called from PollThread().
 *
 * @param param_ptr Pointer to user parameter.
 * @param packet_ptr Pointer to packet containing the control message.
 */
void ProbeTxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr);

/**
 * Process control message for Tx connection.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param probe_hdr_ptr Pointer to control message header.
 * @param wait_timeout_ms_ptr Pointer to current wait timeout. This function may alter the contents of the value.
 *
 * @return True if new probe state has been set, otherwise false is returned.
 */
bool ProbeTxControlProcessPacket(ProbeEndpointState* probe_ptr, const CdiDecodedProbeHeader* probe_hdr_ptr,
                                 uint64_t* wait_timeout_ms_ptr);

/**
 * Called when the wait timeout period has expired. Time to process the current Tx probe state.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 *
 * @return Wait timeout value.
 */
uint64_t ProbeTxControlProcessProbeState(ProbeEndpointState* probe_ptr);

#endif  // CDI_ADAPTER_EFA_PROBE_TX_H__

