// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
 * @param common_hdr_ptr Pointer to control packet common header data.
 */
static void SaveRemoteEndpointInfo(CdiEndpointHandle cdi_endpoint_handle,
                                   const ControlPacketCommonHeader* common_hdr_ptr)
{
    EndpointManagerEndpointInfoSet(cdi_endpoint_handle, common_hdr_ptr->senders_stream_identifier,
                                   common_hdr_ptr->senders_stream_name_str);

    AdapterEndpointState* endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;

    // Copy sender's EFA device GID, and remote IP (specific to EFA).
    memcpy(efa_endpoint_ptr->remote_ipv6_gid_array, common_hdr_ptr->senders_gid_array,
            sizeof(efa_endpoint_ptr->remote_ipv6_gid_array));
    CdiOsStrCpy(efa_endpoint_ptr->remote_ip_str, sizeof(efa_endpoint_ptr->remote_ip_str),
                common_hdr_ptr->senders_ip_str);
}

/**
 * Use the specified control packet to try and find an existing probe endpoint that matches the GID contained in the
 * packet. If a match is found, the handle of the matching probe endpoint is returned, otherwise NULL is returned.
 *
 * @param handle Handle of adpater connection.
 * @param common_hdr_ptr Pointer to control packet common header.
 *
 * @return Handle of found probe endpoint. If a match was not found, NULL is returned.
 */
static ProbeEndpointHandle FindProbeEndpoint(AdapterConnectionHandle handle,
                                             const ControlPacketCommonHeader* common_hdr_ptr)
{
    ProbeEndpointHandle probe_endpoint_handle = NULL;

    // Try to find which endpoint this command should be sent to.
    CdiEndpointHandle cdi_endpoint_handle =
        EndpointManagerGetFirstEndpoint(handle->data_state.cdi_connection_handle->endpoint_manager_handle);

    while (cdi_endpoint_handle) {
        int stream_identifier = EndpointManagerEndpointStreamIdGet(cdi_endpoint_handle);
        AdapterEndpointState* endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
        EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;

        if (STREAM_IDENTIFIER_NOT_USED == stream_identifier ||
            stream_identifier == common_hdr_ptr->senders_stream_identifier) {
            probe_endpoint_handle = efa_endpoint_ptr->probe_endpoint_handle;
            break;
        }
        cdi_endpoint_handle = EndpointManagerGetNextEndpoint(cdi_endpoint_handle);
    }

    if (cdi_endpoint_handle && kEndpointDirectionSend == handle->direction) {
        SaveRemoteEndpointInfo(cdi_endpoint_handle, common_hdr_ptr); // Save latest remote endpoint data.
    }

    return probe_endpoint_handle;
}

/**
 * Process ping timeout condition.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 */
static void ProcessPingTimeout(ProbeEndpointState* probe_ptr)
{
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = probe_ptr->app_adapter_endpoint_handle->adapter_con_state_ptr;
    bool do_reset = true;

    if (kEndpointDirectionReceive == probe_ptr->app_adapter_endpoint_handle->adapter_con_state_ptr->direction) {
        // Only destroy if not the last Rx endpoint.
        EndpointManagerHandle mgr_handle = EndpointManagerConnectionToEndpointManager(
                adapter_con_ptr->data_state.cdi_connection_handle);
        if (EndpointManagerEndpointGetCount(mgr_handle) > 1) {
            EndpointManagerEndpointDestroy(cdi_endpoint_handle);
            do_reset = false;
        }
    }

    if (do_reset) {
        ProbeControlEfaConnectionQueueReset(probe_ptr, NULL);
        probe_ptr->rx_probe_state.rx_state = kProbeStateEfaReset;
    } else {
        probe_ptr->rx_probe_state.rx_state = kProbeStateDestroy;
    }
}


//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ProbeRxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr)
{
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
        ControlCommand control_cmd = {
            .command_type = kCommandTypeRxPacket,
            .packet_sgl = packet_ptr->sg_list
        };

        // Calculate the receive packet's checksum. First, save the checksum that was sent with the packet so we can use
        // it to compare against. Then, zero out that checksum field in the packet because when we run the checksum
        // calculation we don't want to include the transmitted checksum value in that calculation.
        bool command_packet_valid = true;
        ControlPacketCommonHeader* common_hdr_ptr =
            (ControlPacketCommonHeader*)control_cmd.packet_sgl.sgl_head_ptr->address_ptr;
        uint16_t rx_checksum = common_hdr_ptr->checksum;
        common_hdr_ptr->checksum = 0;
        uint16_t calculated_checksum =
            ProbeControlChecksum((uint16_t*)common_hdr_ptr, control_cmd.packet_sgl.total_data_size);
        if (calculated_checksum != rx_checksum) {
            CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with bad checksum[0x%4x]. Expecting[0x%4x]",
                                     rx_checksum, calculated_checksum);
            command_packet_valid = false;
        }

        // Make sure the command is a valid command type. This also indirectly verifies that the packet is not all
        // zeros, because none of these probe commands have the value 0.  There is no default statement below because we
        // have covered all of the cases and if another enum gets added later to the typedef, we want the compiler to
        // complain to us.
        if (command_packet_valid) {
            command_packet_valid = false; // Reset to false so we can detect a bad command type below.
            int expected_packet_size = 0;
            switch (common_hdr_ptr->command) {
                case kProbeCommandReset:
                case kProbeCommandPing:
                case kProbeCommandConnected:
                    expected_packet_size = sizeof(ControlPacketCommand);
                    command_packet_valid = true;
                    break;
                case kProbeCommandAck:
                    expected_packet_size = sizeof(ControlPacketAck);
                    command_packet_valid = true;
                    break;
            }
            if (!command_packet_valid) {
                // We got here because none of the cases matched, so the command is invalid.
                CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with invalid command type value[%d].",
                                         (int)common_hdr_ptr->command);
            } else if (expected_packet_size != control_cmd.packet_sgl.total_data_size) {
                // Make sure the command packet is the expected length.
                CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with wrong size[%d]. Expecting[%d]",
                                         control_cmd.packet_sgl.total_data_size, expected_packet_size);
                command_packet_valid = false;
            }
        }

        // If the packet is not valid, then throw it away.
        bool fifo_write_failed = false;
        ProbeEndpointHandle probe_endpoint_handle = NULL;
        if (command_packet_valid) {
            probe_endpoint_handle = FindProbeEndpoint(adapter_con_ptr, common_hdr_ptr);
            if (NULL == probe_endpoint_handle) {
                if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                    CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with unknown Stream ID[%d].",
                                   common_hdr_ptr->senders_stream_identifier);
                    command_packet_valid = false;
                } else {
                    // Create a new Rx EFA Endpoint.
                    CdiEndpointHandle cdi_endpoint_handle = NULL;
                    CDI_LOG_THREAD(kLogInfo, "Creating new Rx endpoint stream ID[%d].",
                                    common_hdr_ptr->senders_stream_identifier);
                    CdiReturnStatus rs = EndpointManagerRxCreateEndpoint(
                        EndpointManagerConnectionToEndpointManager(adapter_con_ptr->data_state.cdi_connection_handle),
                        adapter_con_ptr->port_number, &cdi_endpoint_handle);
                    if (kCdiStatusOk == rs) {
                        SaveRemoteEndpointInfo(cdi_endpoint_handle, common_hdr_ptr);
                        EfaEndpointState* efa_endpoint_ptr = cdi_endpoint_handle->adapter_endpoint_ptr->type_specific_ptr;
                        probe_endpoint_handle = efa_endpoint_ptr->probe_endpoint_handle;
                    } else {
                        CDI_LOG_THREAD(kLogError, "Failed to create new EFA Rx endpoint. Remote IP[%s] stream ID[%d]",
                                    common_hdr_ptr->senders_ip_str, common_hdr_ptr->senders_stream_identifier);
                        fifo_write_failed = true;
                    }
                }
            }
        }

        if (command_packet_valid && probe_endpoint_handle) {
            CdiSignalType shutdown_signal = probe_endpoint_handle->app_adapter_endpoint_handle->shutdown_signal;
            if (!CdiFifoWrite(probe_endpoint_handle->control_packet_fifo_handle, CDI_INFINITE, shutdown_signal,
                            &control_cmd)) {
                fifo_write_failed = true;
            }
        }

        // Since didn't put the packet into the FIFO for processing, we need to return it to the pool here.
        if (!command_packet_valid || fifo_write_failed) {
            EfaConnectionState* efa_con_ptr = (EfaConnectionState*)adapter_con_ptr->type_specific_ptr;
            CdiAdapterFreeBuffer(ControlInterfaceGetEndpoint(efa_con_ptr->rx_control_handle),
                                 &packet_ptr->sg_list);
        }
    }
}

bool ProbeRxControlProcessPacket(ProbeEndpointState* probe_ptr,
                                 const ControlPacketCommonHeader* common_hdr_ptr, uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;

    bool dest_port_changed = (efa_endpoint_ptr->tx_control_dest_port != common_hdr_ptr->senders_control_dest_port);

    // If the destination port has changed or we haven't created the transmit control interface yet, do so now.
    if (dest_port_changed || NULL == efa_endpoint_ptr->tx_control_handle) {
        // Save senders endpoint info and new Tx destination port.
        SaveRemoteEndpointInfo(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle, common_hdr_ptr);
        efa_endpoint_ptr->tx_control_dest_port = common_hdr_ptr->senders_control_dest_port;

        // Got here either because the destination port changed OR the Tx control endpoint has not yet been created.
        // If it has already been created, then this means that the destination port has changed (the connection on the
        // remote system has been restarted). So we must close and re-open it in order to use the new port.
        if (efa_endpoint_ptr->tx_control_handle) {
            ControlInterfaceDestroy(efa_endpoint_ptr->tx_control_handle);
            efa_endpoint_ptr->tx_control_handle = NULL;
        }

        // Create a new Tx control interface endpoint.
        if (!ProbeTxControlCreateInterface(probe_ptr, common_hdr_ptr->senders_ip_str,
                                           efa_endpoint_ptr->tx_control_dest_port)) {
            CDI_LOG_THREAD(kLogError,
                           "Probe failed to create Tx control interface for remote IP[%s] port[%d] stream ID[%d].",
                           common_hdr_ptr->senders_ip_str, efa_endpoint_ptr->tx_control_dest_port,
                           common_hdr_ptr->senders_stream_identifier);
        }
    }

    switch (common_hdr_ptr->command) {
        case kProbeCommandReset:
            // Send a request to the Endpoint Manager to reset the local Rx connection.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Rx stream ID[%d] got Reset command from Tx. Restarting EFA connection.",
                                     common_hdr_ptr->senders_stream_identifier);
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaReset;
            *wait_timeout_ms_ptr = 0;
            ret_new_state = true;

            // Save command and ACK packet number so after the reset completes, we can respond by sending the ACK.
            probe_ptr->send_ack_command = common_hdr_ptr->command;
            probe_ptr->send_ack_control_packet_num = common_hdr_ptr->control_packet_num;
            probe_ptr->send_ack_command_valid = true;
            break;
        case kProbeCommandPing:
            // Bump ping received counter.
            probe_ptr->rx_probe_state.pings_received_count++;

            // Set Rx state to connected and timeout based on ping monitor frequency.
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaConnected;
            *wait_timeout_ms_ptr = RX_PING_MONITOR_TIMEOUT_MSEC;
            ret_new_state = true;

            // Send an ACK back to the transmitter (client).
            ProbeControlSendAck(probe_ptr, common_hdr_ptr->command, common_hdr_ptr->control_packet_num);
            break;

        // Should never get these commands.
        case kProbeCommandAck:
        case kProbeCommandConnected:
        default:
            assert(false);
    }

    return ret_new_state;
}

int ProbeRxControlProcessProbeState(ProbeEndpointState* probe_ptr)
{
    uint64_t wait_timeout_ms = DEFAULT_TIMEOUT_MSEC;
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
    int stream_identifier = EndpointManagerEndpointStreamIdGet(cdi_endpoint_handle);

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe Rx stream ID[%d] state[%s].", stream_identifier,
                             InternalUtilityKeyEnumToString(kKeyProbeState, probe_ptr->rx_probe_state.rx_state));
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
        case kProbeStateSendReset:
            // Notify application that we are disconnected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusDisconnected, NULL);
            // If we have received a reset command from the remote Tx (client) connection, which contains the
            // remote IP and destination port, we can send reset commands to it.
            if (efa_endpoint_ptr->tx_control_handle) {
                // Send command to reset the remote Tx (client) connection. Will not expect an ACK back.
                ProbeControlSendCommand(probe_ptr, kProbeCommandReset, false);
            }
            wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            break;
        case kProbeStateResetDone:
            // If the reset was triggered by the remote connection, respond with an ACK command.
            if (probe_ptr->send_ack_command_valid) {
                ProbeControlSendAck(probe_ptr, probe_ptr->send_ack_command, probe_ptr->send_ack_control_packet_num);
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
                "Probe Rx stream ID[%d] EFA probe timeout. Sending reset to Tx.", stream_identifier);
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
#ifdef DISABLE_PROBE_MONITORING
            wait_timeout_ms = CDI_INFINITE;
#else
            // Just connected, so advance to ping state and timeout if we miss receiving a ping.
            probe_ptr->rx_probe_state.rx_state = kProbeStateEfaConnectedPing;
            wait_timeout_ms = TX_PING_ACK_TIMEOUT_MSEC;
#endif
            break;
        case kProbeStateEfaConnectedPing:
            // Did not get a ping within the timeout period. Reset the connection.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                "Probe Rx stream ID[%d] EFA ping timeout. Sending reset to Tx.",
                EndpointManagerEndpointStreamIdGet(cdi_endpoint_handle));
            ProcessPingTimeout(probe_ptr);
            wait_timeout_ms = 0; // Do immediately.
            break;
        case kProbeStateDestroy:
            // Nothing special needed here.
            break;
    }

    return wait_timeout_ms;
}
