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
#include "adapter_efa_probe_tx.h"

#include "adapter_api.h"
#include "adapter_efa_probe_control.h"
#include "cdi_os_api.h"
#include "endpoint_manager.h"
#include "internal.h"
#include "internal_utility.h"
#include "payload.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Legacy protocols before version 5 used 3000 for ping command frequency.
#define LEGACY_SEND_PING_COMMAND_FREQUENCY_MSEC     (3000)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Send a probe packet using the EFA adapter interface to the endpoint associated with the probe connection. Only one
 * packet is sent at a time, waiting for the packet's ACK before sending the next one. Probe doesn't send very many
 * packets so no need to optimize and require additional DMA packet buffers.
 *
 * @param probe_ptr Pointer to probe connection state data.
 *
 * @return True is successful, otherwise false is returned.
 */
static bool EfaEnqueueSendProbePacket(ProbeEndpointState* probe_ptr)
{
    bool ret = true;

    CdiSinglyLinkedList packet_list;
    CdiSinglyLinkedListInit(&packet_list);

    CdiProtocolHandle protocol_handle = probe_ptr->app_adapter_endpoint_handle->protocol_handle;
    TxPayloadState payload_state = { 0 };

    // For a EFA probe packet, create a work request and add it to a packet list. The list will be sent to the adapter's
    // endpoint.
    CdiAdapterState* adapter_state_ptr =
        (CdiAdapterState*)probe_ptr->app_adapter_endpoint_handle->adapter_con_state_ptr->adapter_state_ptr;
    ProbePacketWorkRequest* work_request_ptr =
        ProbeControlWorkRequestGet(adapter_state_ptr->probe_work_request_pool_handle);
    if (NULL == work_request_ptr) {
        ret = false;
    } else {
        EfaProbePacket* packet_ptr = &work_request_ptr->packet_data.efa_packet;
        packet_ptr->packet_sequence_num = probe_ptr->tx_probe_state.packets_enqueued_count;

        // Set the EFA data to a pattern.
        memset(packet_ptr->efa_data, EFA_PROBE_PACKET_DATA_PATTERN, sizeof(packet_ptr->efa_data));

        work_request_ptr->packet.sg_list.total_data_size = EFA_PROBE_PACKET_DATA_SIZE;
        work_request_ptr->packet.sg_list.sgl_head_ptr = &work_request_ptr->sgl_entry;
        work_request_ptr->packet.sg_list.sgl_tail_ptr = &work_request_ptr->sgl_entry;

        work_request_ptr->sgl_entry.size_in_bytes = EFA_PROBE_PACKET_DATA_SIZE;
        work_request_ptr->sgl_entry.address_ptr = packet_ptr->efa_data;
        work_request_ptr->sgl_entry.next_ptr = NULL;
        work_request_ptr->sgl_entry.internal_data_ptr = NULL;

        // Set the CDI common header.
        int msg_prefix_size =
            probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle->adapter_endpoint_ptr->msg_prefix_size;
        CdiRawPacketHeader* header_ptr = (CdiRawPacketHeader*)((char*)packet_ptr->efa_data + msg_prefix_size);
        payload_state.payload_packet_state.payload_type = kPayloadTypeProbe;
        payload_state.payload_packet_state.payload_num = 0;
        payload_state.payload_packet_state.packet_sequence_num = packet_ptr->packet_sequence_num;
        payload_state.payload_packet_state.packet_id = packet_ptr->packet_sequence_num;

        payload_state.source_sgl.total_data_size = EFA_PROBE_PACKET_DATA_SIZE;
        ProtocolPayloadHeaderInit(protocol_handle, header_ptr, sizeof(*header_ptr), &payload_state);

        // Set flag to true if last packet of the payload. This is used to decrement tx_in_flight_ref_count when
        // the last packet of a payload is ACKed.
        work_request_ptr->packet.payload_last_packet =
            (packet_ptr->packet_sequence_num + 1 == EFA_PROBE_PACKET_COUNT);

        CdiSinglyLinkedListPushTail(&packet_list, &work_request_ptr->packet.list_entry);
        // Increment in-flight reference counter once for each packet.
        CdiOsAtomicInc32(&probe_ptr->app_adapter_endpoint_handle->tx_in_flight_ref_count);
        probe_ptr->tx_probe_state.packets_enqueued_count++;
    }

    // Now that all the work requests have been created, put the list in the adapter's endpoint packet queue.
    if (ret && kCdiStatusOk != CdiAdapterEnqueueSendPackets(probe_ptr->app_adapter_endpoint_handle, &packet_list)) {
        // Put back all the probe control work requests into the pool.
        for (void* item_ptr = CdiSinglyLinkedListPopHead(&packet_list) ; NULL != item_ptr ;
            item_ptr = CdiSinglyLinkedListPopHead(&packet_list)) {
            Packet* packet_ptr = CONTAINER_OF(item_ptr, Packet, list_entry);
            ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;
            CdiPoolPut(adapter_state_ptr->probe_work_request_pool_handle, work_request_ptr);
        }
        ret = false;
    }

    if (!ret) {
        CDI_LOG_THREAD(kLogError, "Failed to enqueue send EFA Probe packets.");
    }

    return ret;
}

/**
 * @brief Process the state of command that can be resent multiple times, due to not receiving an ACK.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param remote_ip_str Pointer to remote endpoint's IP string.
 * @param remote_dest_port Remote endpoint's destination port.
 * @param command Command to send.
 *
 * @return Wait timeout in milliseconds.
 */
static int ProcessSendCommandRetry(ProbeEndpointState* probe_ptr, const char* remote_ip_str, int remote_dest_port,
                                   ProbeCommand command)
{
    int wait_timeout_ms = 0;
    bool send_command = true;

    // If first time here, then skip this logic and just send the command.
    if (++probe_ptr->tx_probe_state.send_command_retry_count > 1) {
        probe_ptr->app_adapter_endpoint_handle->endpoint_stats_ptr->probe_command_retry_count++;
        if (probe_ptr->tx_probe_state.send_command_retry_count <= TX_COMMAND_MAX_RETRIES) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Probe Tx remote IP[%s:%d] %s ACK timeout. Resending cmd #[%d].",
                    remote_ip_str, remote_dest_port, InternalUtilityKeyEnumToString(kKeyProbeCommand, command),
                    probe_ptr->tx_probe_state.send_command_retry_count);
        } else {
            //  Reset the connection.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                "Probe Tx remote IP[%s:%d] %s ACK timeout. Tried[%d] times. Now sending reset to Rx.",
                remote_ip_str, remote_dest_port, InternalUtilityKeyEnumToString(kKeyProbeCommand, command),
                TX_COMMAND_MAX_RETRIES);
            probe_ptr->tx_probe_state.tx_state = kProbeStateEfaReset; // Advance to resetting state.
            wait_timeout_ms = 0; // Do immediately.
            send_command = false;
        }
    }
    if (send_command) {
        ProbeControlSendCommand(probe_ptr, command, true);
        wait_timeout_ms = TX_COMMAND_ACK_TIMEOUT_MSEC;
    }

    return wait_timeout_ms;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ProbeTxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type)
{
    assert(kEndpointMessageTypePacketSent == message_type);
    (void)message_type;

    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)param_ptr;
    ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;

    if (kAdapterPacketStatusOk != packet_ptr->tx_state.ack_status) {
        ProbeEndpointError((ProbeEndpointHandle)probe_ptr);
    }

    // Put back work request into the pool.
    CdiAdapterState* adapter_state_ptr =
        (CdiAdapterState*)probe_ptr->app_adapter_endpoint_handle->adapter_con_state_ptr->adapter_state_ptr;
    CdiPoolPut(adapter_state_ptr->probe_work_request_pool_handle, work_request_ptr);

    probe_ptr->tx_probe_state.packets_acked_count++;

    CdiAdapterTxPacketComplete(probe_ptr->app_adapter_endpoint_handle, packet_ptr);

    // Do until we have queued all the probe packets.
    if (probe_ptr->tx_probe_state.packets_acked_count < EFA_PROBE_PACKET_COUNT) {
        // No need to check for errors. Probe will timeout and restart the connection negotiation process.
        EfaEnqueueSendProbePacket(probe_ptr);
    }
}

void ProbeTxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr)
{
    AdapterConnectionState* adapter_con_ptr = (AdapterConnectionState*)param_ptr;

    // Put back work request into the pool.
    ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;
    CdiPoolHandle control_work_request_pool_handle =
        ControlInterfaceGetWorkRequestPoolHandle(adapter_con_ptr->control_interface_handle);
    CdiPoolPut(control_work_request_pool_handle, work_request_ptr);
}

bool ProbeTxControlProcessPacket(ProbeEndpointState* probe_ptr, const CdiDecodedProbeHeader* probe_hdr_ptr,
                                 uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    EfaEndpointState* efa_endpoint_state_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;

    switch (probe_hdr_ptr->command) {
        case kProbeCommandReset:
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Tx remote IP[%s:%d] got Reset command from Rx. Restarting EFA connection.",
                                     probe_hdr_ptr->senders_ip_str, probe_hdr_ptr->senders_control_dest_port);
            // Queue Endpoint Manager to reset the EFA connection and notify the application that we are disconnected.
            ProbeControlEfaConnectionQueueReset(probe_ptr, NULL);

            // Get latest GID from remote.
            memcpy(efa_endpoint_state_ptr->remote_ipv6_gid_array, probe_hdr_ptr->senders_gid_array,
                   sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array));

            if (NULL == probe_ptr->app_adapter_endpoint_handle->protocol_handle) {
                // Negotiated protocol version has not been set yet, so do so now.
                if (!EfaAdapterEndpointProtocolVersionSet(efa_endpoint_state_ptr, &probe_hdr_ptr->senders_version)) {
                    break;
                }
            }

            probe_ptr->tx_probe_state.tx_state = kProbeStateResetting;
            *wait_timeout_ms_ptr = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
            ret_new_state = true;
            break;
        case kProbeCommandAck:
            CdiOsCritSectionReserve(probe_ptr->ack_lock); // Lock access to the ack state data.
            const CdiDecodedProbeAck* packet_ack_ptr = &probe_hdr_ptr->ack_packet;

            // Check if we sent a command and are waiting for an ACK for it. If not, ignore the ACK.
            if (probe_ptr->ack_is_pending) {
                // We are waiting for an ACK. Check if the ACK contains the same command and probe packet number of
                // the command that was sent.

                // Ensure the sizes of these values are the same, so wrapping doesn't affect results when comparing
                // them.
                CDI_STATIC_ASSERT(sizeof(packet_ack_ptr->ack_control_packet_num) == sizeof(probe_ptr->ack_control_packet_num), \
                    "Control packet sizes must match.");

                if (packet_ack_ptr->ack_command == probe_ptr->ack_command &&
                    packet_ack_ptr->ack_control_packet_num == probe_ptr->ack_control_packet_num) {
                    // It matches, so we got the ACK for the command that was sent.
                    probe_ptr->ack_is_pending = false;

                    // Don't log the ping ACK commands (generates too many log messages).
                    if (kProbeCommandPing != packet_ack_ptr->ack_command) {
                        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                                 "Probe Tx remote IP[%s:%d] accepted ACK.",
                                                 probe_hdr_ptr->senders_ip_str,
                                                 probe_hdr_ptr->senders_control_dest_port);
                        CDI_LOG_THREAD(kLogInfo, "Received connection response");
                    }

                    if (kProbeCommandReset == packet_ack_ptr->ack_command) {
                        // Get latest GID from remote.
                        memcpy(efa_endpoint_state_ptr->remote_ipv6_gid_array, probe_hdr_ptr->senders_gid_array,
                               sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array));

                        char gid_name_str[MAX_IPV6_ADDRESS_STRING_LENGTH];
                        DeviceGidToString(efa_endpoint_state_ptr->remote_ipv6_gid_array,
                                          sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array), gid_name_str,
                                          sizeof(gid_name_str));
                        CDI_LOG_THREAD(kLogInfo, "Probe Tx remote IP[%s:%d] using remote EFA device GID[%s].",
                                       probe_hdr_ptr->senders_ip_str, probe_hdr_ptr->senders_control_dest_port,
                                       gid_name_str);

                        // Reset negotiated protocol version.
                        ProtocolVersionDestroy(probe_ptr->app_adapter_endpoint_handle->protocol_handle);
                        probe_ptr->app_adapter_endpoint_handle->protocol_handle = NULL;

                        // Check if we received a probe version in the ACK that only supports probe versions before
                        // 3. Probe version 3 and later support the kProbeStateSendProtocolVersion command.
                        if (probe_hdr_ptr->senders_version.probe_version_num < 3) {
                            // Remote is using probe version before 3. It does not support the version command. So,
                            // queue endpoint start and advance state to wait for it to complete.
                            if (EfaAdapterEndpointProtocolVersionSet(efa_endpoint_state_ptr, &probe_hdr_ptr->senders_version)) {
                                EndpointManagerQueueEndpointStart(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle);
                                probe_ptr->tx_probe_state.tx_state = kProbeStateWaitForStart;
                                *wait_timeout_ms_ptr = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
                            }
                        } else {
                            // Remote supports probe later than version 2, so send it our protocol/probe version using a
                            // command that is only supported by probe versions later than 2.
                            probe_ptr->tx_probe_state.tx_state = kProbeStateSendProtocolVersion;
                            probe_ptr->tx_probe_state.send_command_retry_count = 0;
                            *wait_timeout_ms_ptr = 0; // Process immediately.
                        }
                        ret_new_state = true;
                    } else if (kProbeCommandProtocolVersion == packet_ack_ptr->ack_command) {
                        // Got an ACK for a protocol version command. Set protocol version.
                        if (EfaAdapterEndpointProtocolVersionSet(efa_endpoint_state_ptr, &probe_hdr_ptr->senders_version)) {
                            // Queue endpoint start and advance state to wait for it to complete.
                            EndpointManagerQueueEndpointStart(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle);
                            probe_ptr->tx_probe_state.tx_state = kProbeStateWaitForStart;
                            *wait_timeout_ms_ptr = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
                            ret_new_state = true;
                        }
                    } else if (kProbeCommandPing == packet_ack_ptr->ack_command) {
                        // Got an ACK for a ping command. Drop back to the EFA connected state, which will repeat the
                        // ping process. Setup wait period for next ping based on ping frequency.
                        probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnected;
                        CdiProtocolHandle protocol_handle = probe_ptr->app_adapter_endpoint_handle->protocol_handle;
                        if (protocol_handle->negotiated_version.probe_version_num >= 5) {
                            *wait_timeout_ms_ptr = SEND_PING_COMMAND_FREQUENCY_MSEC;
                        } else {
                            *wait_timeout_ms_ptr = LEGACY_SEND_PING_COMMAND_FREQUENCY_MSEC;
                        }
                        ret_new_state = true;
                    } else {
                        assert(false); // No other supported commands return an Ack.
                    }
                } else {
                    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                        "Probe Tx remote IP[%s:%d] ignoring ACK. Got ACK for command[%s] packet_num[%d]. Expected "
                        "command[%s] packet_num[%d].", probe_hdr_ptr->senders_ip_str,
                        probe_hdr_ptr->senders_control_dest_port,
                        InternalUtilityKeyEnumToString(kKeyProbeCommand, packet_ack_ptr->ack_command),
                        packet_ack_ptr->ack_control_packet_num,
                        InternalUtilityKeyEnumToString(kKeyProbeCommand, probe_ptr->ack_command),
                        probe_ptr->ack_control_packet_num);
                }
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Tx remote IP[%s:%d]  ignoring unexpected ACK.",
                                         probe_hdr_ptr->senders_ip_str, probe_hdr_ptr->senders_control_dest_port);
            }
            CdiOsCritSectionRelease(probe_ptr->ack_lock); // Release access to the ack state data.
            break;
        case kProbeCommandConnected:
            if (kProbeStateEfaProbe != probe_ptr->tx_probe_state.tx_state) {
                // We are not expecting a connection command yet, so send a reset.
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Tx remote IP[%s:%d] Received unexpected connected command. Sending reset.",
                                         probe_hdr_ptr->senders_ip_str, probe_hdr_ptr->senders_control_dest_port);
                probe_ptr->tx_probe_state.tx_state = kProbeStateSendReset;
                *wait_timeout_ms_ptr = 0; // Take effect immediately.
                ret_new_state = true;
            } else {
                // Got a connected command from receiver. Advance state to ensure probe ACKs have all been received.
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaTxProbeAcks;
                *wait_timeout_ms_ptr = 0; // Take effect immediately.
                ret_new_state = true;
            }
            break;

        // Should never get these commands.
        case kProbeCommandPing:
        default:
            assert(false);
    }

    return ret_new_state;
}

uint64_t ProbeTxControlProcessProbeState(ProbeEndpointState* probe_ptr)
{
    uint64_t wait_timeout_ms = DEFAULT_TIMEOUT_MSEC;
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;
    const char* remote_ip_str = EndpointManagerEndpointRemoteIpGet(cdi_endpoint_handle);
    int remote_dest_port = EndpointManagerEndpointRemotePortGet(cdi_endpoint_handle);

    // Don't log the kProbeStateEfaConnected state. It is used for ping (generates too many log messages).
    if (kProbeStateEfaConnected != probe_ptr->tx_probe_state.tx_state &&
        kProbeStateEfaConnectedPing != probe_ptr->tx_probe_state.tx_state) {
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe Tx remote IP[%s:%d] state[%s]",
                                 remote_ip_str, remote_dest_port,
                                 InternalUtilityKeyEnumToString(kKeyProbeState, probe_ptr->tx_probe_state.tx_state));
        if (kProbeStateSendReset == probe_ptr->tx_probe_state.tx_state ||
            kProbeStateWaitForStart == probe_ptr->tx_probe_state.tx_state) {
            CDI_LOG_THREAD(kLogInfo, "No reply to connection response received.");
        }
    }

    switch (probe_ptr->tx_probe_state.tx_state) {
        case kProbeStateResetting:
        case kProbeStateWaitForStart:
            // Got timeout before these commands completed. Go to connection reset state.
            ProbeControlSendCommand(probe_ptr, kProbeCommandReset, true);
            wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            break;
        case kProbeStateEfaReset:
            // Notify the application that we are disconnected and send a request to reset the connection to the
            // Endpoint Manager.
            ProbeControlEfaConnectionQueueReset(probe_ptr, NULL);
            ProbeControlSendCommand(probe_ptr, kProbeCommandReset, true);
            probe_ptr->tx_probe_state.tx_state = kProbeStateResetting; // Advance to resetting state.
            wait_timeout_ms = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
            break;
        case kProbeStateResetDone:
            // If the reset was triggered by the remote connection, respond with an ACK command.
            if (probe_ptr->send_ack_command_valid) {
                ProbeControlSendAck(probe_ptr, probe_ptr->send_ack_command, probe_ptr->send_ack_control_packet_num);
                probe_ptr->send_ack_command_valid = false;
            }
            probe_ptr->tx_probe_state.tx_state = kProbeStateWaitForStart; // Advance to wait for start state.
            wait_timeout_ms = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
            break;
        case kProbeStateIdle:
        case kProbeStateSendReset:
            // Notify application that we are disconnected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusDisconnected, NULL);
            // Send command to reset the remote Rx (server) connection. Will expect an ACK back from the remote.
            ProbeControlSendCommand(probe_ptr, kProbeCommandReset, true);
            probe_ptr->tx_probe_state.tx_state = kProbeStateSendReset; // Ensure we are in send reset state.
            wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            break;
        case kProbeStateSendProtocolVersion:
            // Either first time here and need to send the protocol version command or did not get an ACK back from it
            // within the timeout period.
            wait_timeout_ms = ProcessSendCommandRetry(probe_ptr, remote_ip_str, remote_dest_port,
                                                      kProbeCommandProtocolVersion);
            break;
        case kProbeStateEfaStart:
            // Enable the EFA connection for probe state. Use the EFA interface to send probe packets before allowing
            // application to use the connection. Once all the probe packets have been acknowledged as being received by
            // the remote, it will send a kProbeCommandConnected command back. Start this process here.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Tx remote IP[%s:%d] starting the SRD probe process",
                                     remote_ip_str, remote_dest_port);

            if (!ProbeControlEfaConnectionStart(probe_ptr)) {
                CDI_LOG_THREAD(kLogError, "Starting EFA connection failed during probe. Resetting connection.");
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaReset; // Advance to resetting state.
                wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            } else {
                probe_ptr->tx_probe_state.packets_enqueued_count = 0; // Initialize counter to start.
                EfaEnqueueSendProbePacket(probe_ptr);
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaProbe;
                // If the EFA probe does not complete by this timeout, we return back to connection reset state.
                wait_timeout_ms = EFA_PROBE_MONITOR_TIMEOUT_MSEC;
            }
            break;
        case kProbeStateEfaProbe:
            // Got timeout before EFA probe completed. Go to connection reset state.
            probe_ptr->tx_probe_state.tx_state = kProbeStateEfaReset; // Advance to resetting state.
            wait_timeout_ms = 0; // Do immediately.
            CDI_LOG_THREAD(kLogError, "Control handshake was successful. However, an insufficient number of probe "
                                      "packets were received. Verify the security group settings are correctly "
                                      "configured. See the CDI SDK Install and Setup Guide for proper security group "
                                      "configuration.");

            break;
        case kProbeStateEfaTxProbeAcks:
            if (probe_ptr->tx_probe_state.packets_acked_count >= EFA_PROBE_PACKET_COUNT) {
                // Received all ACKs from probe packets, so advance to the EFA connected state.
                ProbeControlEfaConnectionEnableApplication(probe_ptr);
                // Advance to the connected state, which will start the ping process.
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnected;
                CdiProtocolHandle protocol_handle = probe_ptr->app_adapter_endpoint_handle->protocol_handle;
                if (protocol_handle->negotiated_version.probe_version_num >= 5) {
                    // Since all probe EFA packets have been ACK'ed, no need to wait for ping delay to advance to
                    // connected state.
                    wait_timeout_ms = TX_CONNECTION_DELAY_MSEC;
                } else {
                    wait_timeout_ms = LEGACY_SEND_PING_COMMAND_FREQUENCY_MSEC;
                }
            } else {
                if (++probe_ptr->tx_probe_state.packets_ack_wait_count < EFA_TX_PROBE_ACK_MAX_RETRIES) {
                    // Wait a little while and retry if we have not received all the ACKs yet.
                    wait_timeout_ms = EFA_TX_PROBE_ACK_TIMEOUT;
                } else {
                    CDI_LOG_THREAD(kLogError, "Did not get all ACKs from probe packets. Resetting connection.");
                    probe_ptr->tx_probe_state.tx_state = kProbeStateEfaReset; // Advance to resetting state.
                    wait_timeout_ms = 0; // Do immediately.
                }
            }
            break;
        case kProbeStateEfaConnected:
#ifdef DISABLE_PROBE_MONITORING
            wait_timeout_ms = CDI_INFINITE;
#else
            // Notify application that we are connected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusConnected, NULL);

            // Advance state to send ping to the remote Rx (server) connection. Will expect an ACK back from the remote.
            probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnectedPing;
            probe_ptr->tx_probe_state.send_command_retry_count = 0; // Reset command retry counter.
            wait_timeout_ms = 0; // Do immediately
#endif
            break;
        case kProbeStateEfaConnectedPing:
            // Either first time here and need to send the ping command or did not get an ACK back from it within the
            // timeout period.
            wait_timeout_ms = ProcessSendCommandRetry(probe_ptr, remote_ip_str, remote_dest_port, kProbeCommandPing);
            break;
        case kProbeStateDestroy:
            // Nothing special needed.
            break;
    }

    return wait_timeout_ms;
}
