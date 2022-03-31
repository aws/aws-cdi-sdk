// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used within the SDK to support functionality that is not
 * part of the API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h" // Must include this file first due to #define _GNU_SOURCE
#include "adapter_efa_probe_rx.h"

#include "adapter_api.h"
#include "adapter_efa_probe_control.h"
#include "adapter_efa_probe_tx.h"
#include "endpoint_manager.h"
#include "internal_rx.h"
#include "internal_utility.h"
#include "cdi_os_api.h"

#include <arpa/inet.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Save data from remote endpoint.
 *
 * @param cdi_endpoint_handle CDI endpoint handle.
 * @param probe_hdr_ptr Pointer to control packet header data.
 * @param source_address_ptr Pointer to source address structure (sockaddr_in).
 */
static void SaveRemoteEndpointInfo(CdiEndpointHandle cdi_endpoint_handle, const CdiDecodedProbeHeader* probe_hdr_ptr,
                                   const struct sockaddr_in* source_address_ptr)
{
    EndpointManagerRemoteEndpointInfoSet(cdi_endpoint_handle, source_address_ptr,
                                         probe_hdr_ptr->senders_stream_name_str);

    AdapterEndpointState* endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;

    // Copy sender's EFA device GID, and remote IP (specific to EFA).
    memcpy(efa_endpoint_ptr->remote_ipv6_gid_array, probe_hdr_ptr->senders_gid_array,
            sizeof(efa_endpoint_ptr->remote_ipv6_gid_array));
}

/**
 * Use the specified control packet to try and find an existing probe endpoint that matches the information contained in
 * the packet. If a match is found, the handle of the matching probe endpoint is returned, otherwise NULL is returned.
 *
 * @param handle Handle of adpater connection.
 * @param probe_hdr_ptr Pointer to control packet header.
 * @param address_ptr Pointer to address structure (sockaddr_in).
 *
 * @return Pointer to found probe endpoint state data. If a match was not found, NULL is returned.
 */
static ProbeEndpointState* FindProbeEndpoint(AdapterConnectionHandle handle,
                                             const CdiDecodedProbeHeader* probe_hdr_ptr,
                                             const struct sockaddr_in* address_ptr)
{
    ProbeEndpointState* probe_ptr = NULL;

    // Try to find which endpoint this command should be sent to.
    CdiEndpointHandle cdi_endpoint_handle =
        EndpointManagerGetFirstEndpoint(handle->data_state.cdi_connection_handle->endpoint_manager_handle);

    while (cdi_endpoint_handle) {
        const struct sockaddr_in* remote_address_ptr = EndpointManagerEndpointRemoteAddressGet(cdi_endpoint_handle);
        AdapterEndpointState* endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
        EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;

        // Use this endpoint if it has not been configured yet (no remote IP) or IP/dest port match.
        if (0 == remote_address_ptr->sin_port ||
            (remote_address_ptr->sin_addr.s_addr == address_ptr->sin_addr.s_addr &&
             remote_address_ptr->sin_port == address_ptr->sin_port)) {
            probe_ptr = efa_endpoint_ptr->probe_endpoint_handle;
            break;
        }
        cdi_endpoint_handle = EndpointManagerGetNextEndpoint(cdi_endpoint_handle);
    }

    if (NULL == cdi_endpoint_handle) {
        char ip_str[MAX_IP_STRING_LENGTH];
        inet_ntop(AF_INET, &address_ptr->sin_addr, ip_str, sizeof(ip_str));
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Unable to find existing endpoint for IP[%s:%d].",
                                 ip_str, ntohs(address_ptr->sin_port));

        CdiEndpointHandle temp_handle =
            EndpointManagerGetFirstEndpoint(handle->data_state.cdi_connection_handle->endpoint_manager_handle);
        if (NULL == temp_handle) {
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "No existing endpoints.");
        } else {
            while (temp_handle) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Existing endpoint IP[%s:%d].",
                                        EndpointManagerEndpointRemoteIpGet(temp_handle),
                                        EndpointManagerEndpointRemotePortGet(temp_handle));
                temp_handle = EndpointManagerGetNextEndpoint(temp_handle);
            }
        }
    }

    if (cdi_endpoint_handle && kEndpointDirectionSend == handle->direction) {
        SaveRemoteEndpointInfo(cdi_endpoint_handle, probe_hdr_ptr, address_ptr); // Save latest remote endpoint data.
    }

    return probe_ptr;
}

/**
 * Destroy Rx endpoint.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 */
static void DestroyRxEndpoint(ProbeEndpointState* probe_ptr)
{
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;
    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Destroying stale endpoint.");
    EndpointManagerEndpointDestroy(cdi_endpoint_handle);
    probe_ptr->rx_probe_state.rx_state = kProbeStateDestroy;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ProbeRxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type)
{
    assert(kEndpointMessageTypePacketReceived == message_type);
    (void)message_type;

    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)param_ptr;

    if (kAdapterPacketStatusOk != packet_ptr->tx_state.ack_status) {
        CDI_LOG_THREAD(kLogError, "EFA packet error. Status[%d].", packet_ptr->tx_state.ack_status);
    } else {
        // Probe does not use payload SGL resources, so internal_data_ptr is NULL. The SGL only contains the adapter
        // packet buffers that need to be freed. Since this function is only called from PollThread(), we can free
        // the buffers directly.
        assert(NULL == packet_ptr->sg_list.internal_data_ptr);
        EfaRxEndpointRxBuffersFree(probe_ptr->app_adapter_endpoint_handle, &packet_ptr->sg_list);

        if (++probe_ptr->rx_probe_state.packets_received_count >= EFA_PROBE_PACKET_COUNT) {
            // EFA probe has successfully completed on receiver. Enable application connection.
            ProbeControlEfaConnectionEnableApplication(probe_ptr);
        }
    }
}

void ProbeRxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr)
{
    AdapterConnectionState* adapter_con_ptr = (AdapterConnectionState*)param_ptr;

    if (CdiOsSignalGet(adapter_con_ptr->shutdown_signal)) {
        return;
    }

    if (kAdapterPacketStatusOk != packet_ptr->tx_state.ack_status) {
        CDI_LOG_THREAD(kLogError, "Control packet error. Status[%d].", packet_ptr->tx_state.ack_status);
        assert(false);
    } else {
        CdiDecodedProbeHeader header = { 0 };
        bool command_packet_valid = (kCdiStatusOk == ProtocolProbeHeaderDecode(packet_ptr->sg_list.sgl_head_ptr->address_ptr,
                                                                               packet_ptr->sg_list.total_data_size,
                                                                               &header));

        // Check if we received a probe control packet that only supports protocol versions before 3.
        struct sockaddr_in senders_address = packet_ptr->socket_adapter_state.address;
        if (header.senders_version.probe_version_num < 4) {
            // Using unidirectional probe version, which does not support the bidirectional socket control interface.
            // Must get the sender's port from the packet's header.
            senders_address.sin_port = htons(header.senders_control_dest_port); // Convert int port to network byte order
        }

        bool fifo_write_failed = false;
        ProbeEndpointState* probe_ptr = NULL;
        if (command_packet_valid) {
            probe_ptr = FindProbeEndpoint(adapter_con_ptr, &header, &senders_address);
            if (NULL == probe_ptr) {
                if (kEndpointDirectionReceive == adapter_con_ptr->direction) {
                    // Create a new Rx EFA Endpoint.
                    CdiEndpointHandle cdi_endpoint_handle = NULL;
                    CDI_LOG_THREAD(kLogInfo, "Creating new Rx endpoint remote IP[%s:%d].",
                                    header.senders_ip_str, header.senders_control_dest_port);
                    CdiReturnStatus rs = EndpointManagerRxCreateEndpoint(
                        EndpointManagerConnectionToEndpointManager(adapter_con_ptr->data_state.cdi_connection_handle),
                        adapter_con_ptr->port_number, &senders_address, &cdi_endpoint_handle);
                    if (kCdiStatusOk == rs) {
                        // Ensure all remote endpoint information is saved.
                        SaveRemoteEndpointInfo(cdi_endpoint_handle, &header, &senders_address);
                        EfaEndpointState* efa_endpoint_ptr = cdi_endpoint_handle->adapter_endpoint_ptr->type_specific_ptr;
                        probe_ptr = efa_endpoint_ptr->probe_endpoint_handle;
                    } else {
                        CDI_LOG_THREAD(kLogError, "Failed to create new EFA Rx endpoint remote IP[%s:%d]",
                                        header.senders_ip_str, header.senders_control_dest_port);
                        fifo_write_failed = true;
                    }
                } else {
                    CDI_LOG_THREAD(kLogError, "Sender failed to find existing endpoint for remote IP[%s:%d]",
                                    header.senders_ip_str, header.senders_control_dest_port);
                    fifo_write_failed = true;
                }
            }
        }

        if (command_packet_valid && probe_ptr) {
            ControlCommand control_cmd = {
                .command_type = kCommandTypeRxPacket,
                .receive_packet = {
                    .packet_sgl = packet_ptr->sg_list,
                    .source_address = senders_address,
                }
            };
            CdiSignalType shutdown_signal = probe_ptr->app_adapter_endpoint_handle->shutdown_signal;
            if (!CdiFifoWrite(probe_ptr->control_packet_fifo_handle, CDI_INFINITE, shutdown_signal, &control_cmd)) {
                fifo_write_failed = true;
            }
        }

        // Since didn't put the packet into the FIFO for processing, we need to return it to the pool here.
        if (!command_packet_valid || fifo_write_failed) {
            CdiAdapterFreeBuffer(ControlInterfaceGetEndpoint(adapter_con_ptr->control_interface_handle),
                                                             &packet_ptr->sg_list);
        }
    }
}

bool ProbeRxControlProcessPacket(ProbeEndpointState* probe_ptr,
                                 const CdiDecodedProbeHeader* probe_hdr_ptr,
                                 const struct sockaddr_in* source_address_ptr, uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;

    bool dest_port_changed = (efa_endpoint_ptr->tx_control_dest_port != probe_hdr_ptr->senders_control_dest_port);

    // If the destination port has changed, update saved remote endpoint data.
    if (dest_port_changed) {
        // Save senders endpoint info and new Tx destination port.
        SaveRemoteEndpointInfo(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle, probe_hdr_ptr,
                               source_address_ptr);
        efa_endpoint_ptr->tx_control_dest_port = probe_hdr_ptr->senders_control_dest_port;
    }

    switch (probe_hdr_ptr->command) {
        case kProbeCommandReset:
            // Send a request to the Endpoint Manager to reset the local Rx connection.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Rx remote IP[%s:%d] got Reset command from Tx. Restarting EFA connection.",
                                     probe_hdr_ptr->senders_ip_str, probe_hdr_ptr->senders_control_dest_port);
            CDI_LOG_THREAD(kLogInfo, "Received connection request.");
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaReset;
            *wait_timeout_ms_ptr = 0;
            ret_new_state = true;

            // Resetting, so free negotiated protocol version if it is set.
            ProtocolVersionDestroy(probe_ptr->app_adapter_endpoint_handle->protocol_handle);
            probe_ptr->app_adapter_endpoint_handle->protocol_handle = NULL;
            probe_ptr->send_ack_probe_version = probe_hdr_ptr->senders_version.probe_version_num;

            // Check if we received a reset command that only supports protocol versions before 3.
            if (probe_hdr_ptr->senders_version.probe_version_num < 3) {
                // Remote supports protocol version 1, so set it.
                EndpointManagerProtocolVersionSet(cdi_endpoint_handle, &probe_hdr_ptr->senders_version);
            }

            // Save command and ACK packet number so after the reset completes, we can respond by sending the ACK.
            probe_ptr->send_ack_command = probe_hdr_ptr->command;
            probe_ptr->send_ack_control_packet_num = probe_hdr_ptr->control_packet_num;
            probe_ptr->send_ack_command_valid = true;
            break;
        case kProbeCommandProtocolVersion:
            // Set negotiated protocol version.
            EndpointManagerProtocolVersionSet(cdi_endpoint_handle, &probe_hdr_ptr->senders_version);
            // Send an ACK back to the transmitter (client).
            ProbeControlSendAck(probe_ptr, probe_hdr_ptr->command, probe_hdr_ptr->control_packet_num);
            break;
        case kProbeCommandPing:
            // Bump ping received counter.
            probe_ptr->rx_probe_state.pings_received_count++;

            // Set Rx state to connected and timeout based on ping monitor frequency.
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaConnected;
            *wait_timeout_ms_ptr = RX_PING_MONITOR_TIMEOUT_MSEC;
            ret_new_state = true;

            // Send an ACK back to the transmitter (client).
            ProbeControlSendAck(probe_ptr, probe_hdr_ptr->command, probe_hdr_ptr->control_packet_num);
            break;

        // Should never get these commands.
        case kProbeCommandAck:
        case kProbeCommandConnected:
        default:
            assert(false);
    }

    return ret_new_state;
}

uint64_t ProbeRxControlProcessProbeState(ProbeEndpointState* probe_ptr)
{
    uint64_t wait_timeout_ms = DEFAULT_TIMEOUT_MSEC;
    AdapterEndpointHandle adapter_endpoint_handle = probe_ptr->app_adapter_endpoint_handle;
    CdiEndpointHandle cdi_endpoint_handle = adapter_endpoint_handle->cdi_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = adapter_endpoint_handle->adapter_con_state_ptr;

    if (kProbeStateEfaConnectedPing != probe_ptr->rx_probe_state.rx_state) {
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe Rx remote IP[%s:%d] state[%s].",
                                EndpointManagerEndpointRemoteIpGet(cdi_endpoint_handle),
                                EndpointManagerEndpointRemotePortGet(cdi_endpoint_handle),
                                InternalUtilityKeyEnumToString(kKeyProbeState, probe_ptr->rx_probe_state.rx_state));
    }

    switch (probe_ptr->rx_probe_state.rx_state) {
        case kProbeStateEfaStart:
        case kProbeStateWaitForStart:
            // Not used, so nothing to do.
            break;
        case kProbeStateResetting:
            // Got timeout before these commands completed. Go to connection reset state.
            ProbeControlSendCommand(probe_ptr, kProbeCommandReset, true);
            wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            break;
        case kProbeStateEfaReset:
            // Either a reset request came from the Tx, ProbeEndpointError() was used, EFA probe timed-out, or a ping
            // was not received within the expected timeout period. Notify the application that we are disconnected and
            // send a request to reset the connection to the Endpoint Manager.
            ProbeControlEfaConnectionQueueReset(probe_ptr, NULL);
            probe_ptr->rx_probe_state.rx_state = kProbeStateResetting; // Advance to resetting state.
            wait_timeout_ms = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
            break;
        case kProbeStateIdle:
        case kProbeStateSendReset:
            // Notify application that we are disconnected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusDisconnected, NULL);
            if (++probe_ptr->rx_probe_state.send_reset_retry_count < RX_RESET_COMMAND_MAX_RETRIES) {
                    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                        "Probe Rx remote IP[%s:%d] sending reset #[%d].",
                        EndpointManagerEndpointRemoteIpGet(cdi_endpoint_handle),
                        EndpointManagerEndpointRemotePortGet(cdi_endpoint_handle),
                        probe_ptr->rx_probe_state.send_reset_retry_count);
                // If we have received a reset command from the remote Tx (client) connection, which contains the
                // remote IP and destination port, we can send reset commands to it.
                if (adapter_con_ptr->control_interface_handle) {
                    // Send command to reset the remote Tx (client) connection. Will not expect an ACK back.
                    ProbeControlSendCommand(probe_ptr, kProbeCommandReset, false);
                }
                probe_ptr->rx_probe_state.rx_state = kProbeStateSendReset; // Ensure in send reset state.
                wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            } else {
                DestroyRxEndpoint(probe_ptr);
                wait_timeout_ms = 0; // Do immediately.
            }
            break;
        case kProbeStateResetDone:
            // If the reset was triggered by the remote connection, respond with an ACK command.
            if (probe_ptr->send_ack_command_valid) {
                ProbeControlSendAck(probe_ptr, probe_ptr->send_ack_command, (int)probe_ptr->send_ack_control_packet_num);
                probe_ptr->send_ack_command_valid = false;
                // For Rx, the EFA endpoint has been started in ProbeEndpointResetDone(), so we can advance to the
                // kProbeStateEfaProbe state.
                probe_ptr->rx_probe_state.rx_state = kProbeStateEfaProbe; // Advance to EFA probe state.
                // If the EFA probe does not complete by this timeout, we return back to connection reset state.
                wait_timeout_ms = EFA_PROBE_MONITOR_TIMEOUT_MSEC;
            } else {
                // Reset was not triggered by the remote connection, so just setup to send another reset command to it.
                // No need to stop/start local libfabric here.
                probe_ptr->rx_probe_state.rx_state = kProbeStateSendReset;
                wait_timeout_ms = 0; // Do immediately.
            }
            break;
        case kProbeStateEfaProbe:
            // Did not complete EFA probe state within timeout. Reset the connection.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                "Probe Rx EFA probe timeout. Sending reset to Tx.");
            probe_ptr->rx_probe_state.rx_state = kProbeCommandReset; // Advance to resetting state.
            wait_timeout_ms = 0; // Do immediately.
            break;
        case kProbeStateEfaConnected:
            // Notify application that we are connected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusConnected, NULL);
            // Send command to notify the remote Tx (client) that we are connected and it is ok for the remote to switch
            // to the connected state. This is done to prevent problems caused by EFA packet reordering. Without this
            // communication, the transmitter could start sending a payload and packets for it might arrive before the
            // last probe packet arrives. NOTE: We will not expect an ACK back.
            ProbeControlSendCommand(probe_ptr, kProbeCommandConnected, false);
            probe_ptr->rx_probe_state.send_reset_retry_count = 0; // Reset retry counter.
            // Save current total Rx packet count so we can use to determine if packets have arrived since it was
            // saved.
            probe_ptr->rx_probe_state.total_packet_count_snapshot = cdi_endpoint_handle->rx_state.total_packet_count;

#ifdef DISABLE_PROBE_MONITORING
            wait_timeout_ms = CDI_INFINITE;
#else
            // Just connected, so advance to ping state and timeout if we miss receiving a ping.
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaConnectedPing;
            wait_timeout_ms = RX_PING_MONITOR_TIMEOUT_MSEC;
#endif
            break;
        case kProbeStateEfaConnectedPing:
            // Rx ping not received without timeout period. Check to see if any Rx packets were received during the
            // timeout period.
            if (probe_ptr->rx_probe_state.total_packet_count_snapshot !=
                cdi_endpoint_handle->rx_state.total_packet_count) {
                // Got Rx packets since last ping, so ignore the missing ping (ping control packets could have been
                // dropped). Reset counters and wait again for next ping.
                probe_ptr->rx_probe_state.send_reset_retry_count = 0;
                probe_ptr->rx_probe_state.total_packet_count_snapshot = cdi_endpoint_handle->rx_state.total_packet_count;
                wait_timeout_ms = RX_PING_MONITOR_TIMEOUT_MSEC;
            } else {
                // Did not get a ping or an Rx packets within the timeout period. Reset the connection.
                DestroyRxEndpoint(probe_ptr);
                wait_timeout_ms = 0; // Do immediately.
            }
            break;
        case kProbeStateDestroy:
        case kProbeStateSendProtocolVersion:
        case kProbeStateEfaTxProbeAcks:
            // Nothing special needed here.
            break;
    }

    return wait_timeout_ms;
}
