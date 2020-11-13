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

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief
 *
 * @param probe_ptr
 * @param dest_ip_addr_str
 * @param dest_port
 *
 * @return True if new Tx control interface has been created and started, otherwise false is returned.
 */
bool ProbeTxControlCreateInterface(ProbeEndpointState* probe_ptr, const char* dest_ip_addr_str, int dest_port);

/**
 * Process a probe packet completion message from the transmitter EFA endpoint.
 *
 * NOTE: This function is called from Poll().
 *
 * @param param_ptr Pointer to user parameter.
 * @param packet_ptr Pointer to packet.
 */
void ProbeTxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr);

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
 * @param common_hdr_ptr Pointer to control message command header.
 * @param wait_timeout_ms_ptr Pointer to current wait timeout. This function may alter the contents of the value.
 *
 * @return True if new probe state has been set, otherwise false is returned.
 */
bool ProbeTxControlProcessPacket(ProbeEndpointState* probe_ptr,
                                 const ControlPacketCommonHeader* common_hdr_ptr, uint64_t* wait_timeout_ms_ptr);

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

