// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
 */
void ProbeRxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr);

/**
 * Process a control packet completion message from the receiver probe control interface endpoint.
 *
 * NOTE: This function is called from SocketReceiveThread().
 *
 * @param param_ptr Pointer to probe endpoint state data (ProbeEndpointState*).
 * @param packet_ptr Pointer to packet containing the control message.
 */
void ProbeRxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr);

/**
 * Process control message for Rx connection.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param common_hdr_ptr Pointer to control message command header.
 * @param wait_timeout_ms_ptr Pointer to current wait timeout. This function may alter the contents of the value.
 *
 * @return True if new probe state has been set, otherwise false is returned.
 */
bool ProbeRxControlProcessPacket(ProbeEndpointState* probe_ptr, const ControlPacketCommonHeader* common_hdr_ptr,
                                 uint64_t* wait_timeout_ms_ptr);

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
