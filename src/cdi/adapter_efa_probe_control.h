// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter_efa_probe_control.c.
 */

#ifndef CDI_ADAPTER_EFA_PROBE_CONTROL_H__
#define CDI_ADAPTER_EFA_PROBE_CONTROL_H__

#include "adapter_api.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Start the EFA connection for use for probing.
 *
 * NOTE: For a Tx connection, the remote GID must be valid before calling this function. See remote_ipv6_gid_array in
 * EfaTxEndpointStart().
 *
 * NOTE: We don't want to update the application's connection state until after the EFA probe has completed. EFA probe
 * must use this function to start the probe.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 *
 * @return bool True if successful, otherwise false is returned.
 */
bool ProbeControlEfaConnectionStart(ProbeEndpointState* probe_ptr);

/**
 * Queue a reset of the EFA connection to the endpoint manager. Also, notify the application that the connection state
 * has changed.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param error_msg_str Pointer to optional error message string. Use NULL if no message.
 */
void ProbeControlEfaConnectionQueueReset(ProbeEndpointState* probe_ptr, const char* error_msg_str);

/**
 * Call this when EFA probe has successfully completed. It will enable the application connection.
 *
 * NOTE: This function is called from PollThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 */
void ProbeControlEfaConnectionEnableApplication(ProbeEndpointState* probe_ptr);

/**
 * Post a state change control command to FIFO used by ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param probe_state Probe state to use.
 */
void ProbeControlQueueStateChange(ProbeEndpointState* probe_ptr, ProbeState probe_state);

/**
 * Get a control interface work request from the work request memory pool. The work request is used to send a command
 * to an endpoint using the control interface.
 *
 * @param work_request_pool_handle Handle of work request memory pool.
 *
 * @return Pointer to work request. NULL is returned if the function failed.
 */
ProbePacketWorkRequest* ProbeControlWorkRequestGet(CdiPoolHandle work_request_pool_handle);

/**
 * Set packet size of a work request.
 *
 * @param work_request_ptr Pointer to work request.
 * @param packet_size Size of packet in bytes.
 */
void ProbeControlWorkRequestPacketSizeSet(ProbePacketWorkRequest* work_request_ptr, int packet_size);

/**
 * Send a command using the control interface to an endpoint associated with the probe connection.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param command Command to send.
 * @param requires_ack True if the command requires an ACK in response, otherwise false.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
CdiReturnStatus ProbeControlSendCommand(ProbeEndpointState* probe_ptr, ProbeCommand command, bool requires_ack);

/**
 * Send an ACK to an endpoint using the adapter control interface.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param ack_command Command to send ACK for.
 * @param ack_probe_packet_num Packet number that was received in the command being ACKed.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
CdiReturnStatus ProbeControlSendAck(ProbeEndpointState* probe_ptr, ProbeCommand ack_command,
                                    uint16_t ack_probe_packet_num);

/**
 * Process a control packet message from a probe control interface bidirectional endpoint.
 *
 * NOTE: This function is called from PollThread().
 *
 * @param param_ptr Pointer to user parameter.
 * @param packet_ptr Pointer to packet containing the control message.
 * @param message_type Endpoint message type.
 */
void ProbeControlMessageFromBidirectionalEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

/**
 * Thread used to run the probe.
 *
 * @param ptr Pointer to probe endpoint state data (ProbeEndpointState*).
 *
 * @return The return code is not used.
 */
CDI_THREAD ProbeControlThread(void* ptr);

#endif  // CDI_ADAPTER_EFA_PROBE_CONTROL_H__

