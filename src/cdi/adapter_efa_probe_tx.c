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
#include "endpoint_manager.h"
#include "internal.h"
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
 * Send a batch of probe packets using the EFA adapter interface to the endpoint associated with the probe connection.
 *
 * @param probe_ptr Pointer to probe connection state data.
 *
 * @return True is successful, otherwise false is returned.
 */
static bool EfaEnqueueSendProbePackets(ProbeEndpointState* probe_ptr)
{
    bool ret = true;

    CdiSinglyLinkedList packet_list;
    CdiSinglyLinkedListInit(&packet_list);

    // For each EFA probe packet, create a work request and add it to a packet list. The list will be sent to the
    // adapter's endpoint using a single call - CdiAdapterEnqueueSendPackets().
    for (int i = 0; i < EFA_PROBE_PACKET_COUNT && ret; i++) {
        ProbePacketWorkRequest* work_request_ptr = ProbeControlWorkRequestGet(probe_ptr->efa_work_request_pool_handle,
                                                                              sizeof(EfaProbePacket));
        if (NULL == work_request_ptr) {
            ret = false;
        } else {
            EfaProbePacket* packet_ptr = &work_request_ptr->packet_data.efa_packet;
            packet_ptr->packet_sequence_num = i;

            // Set the EFA data to a pattern.
            memset(packet_ptr->efa_data, EFA_PROBE_PACKET_DATA_PATTERN, sizeof(packet_ptr->efa_data));

            // Set the CDI common header.
            CdiCDIPacketCommonHeader* common_hdr_ptr = (CdiCDIPacketCommonHeader*)packet_ptr->efa_data;
            common_hdr_ptr->payload_type = kPayloadTypeProbe;
            common_hdr_ptr->payload_num = 0;
            common_hdr_ptr->packet_sequence_num = packet_ptr->packet_sequence_num;

            CdiSinglyLinkedListPushTail(&packet_list, &work_request_ptr->packet.list_entry);
        }
    }

    // Now that all the work requests have been created, put the list in the adapter's endpoint packet queue.
    if (kCdiStatusOk != CdiAdapterEnqueueSendPackets(probe_ptr->app_adapter_endpoint_handle, &packet_list)) {
        // Put back all the probe control work requests into the pool.
        for (void* item_ptr = CdiSinglyLinkedListPopHead(&packet_list) ; NULL != item_ptr ;
            item_ptr = CdiSinglyLinkedListPopHead(&packet_list)) {
            Packet* packet_ptr = CONTAINER_OF(item_ptr, Packet, list_entry);
            ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;
            CdiPoolPut(probe_ptr->efa_work_request_pool_handle, work_request_ptr);
        }
        ret = false;
    }

    if (!ret) {
        CDI_LOG_THREAD(kLogError, "Failed to enqueue send EFA Probe packets.");
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ProbeTxEfaMessageFromEndpoint(void* param_ptr, Packet* packet_ptr)
{
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)param_ptr;
    ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;

    if (kAdapterPacketStatusOk != packet_ptr->tx_state.ack_status) {
        ProbeEndpointError((ProbeEndpointHandle)probe_ptr);
    }

    // Put back work request into the pool.
    CdiPoolPut(probe_ptr->efa_work_request_pool_handle, work_request_ptr);
}

void ProbeTxControlMessageFromEndpoint(void* param_ptr, Packet* packet_ptr)
{
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)param_ptr;
    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
    AdapterEndpointHandle endpoint_handle = ControlInterfaceGetEndpoint(efa_endpoint_ptr->tx_control_handle);
    CdiOsAtomicDec32(&endpoint_handle->tx_packets_in_process);

    // Put back work request into the pool.
    ProbePacketWorkRequest* work_request_ptr = (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;
    CdiPoolPut(probe_ptr->control_work_request_pool_handle, work_request_ptr);
}

bool ProbeTxControlCreateInterface(ProbeEndpointState* probe_ptr, const char* dest_ip_addr_str, int dest_port)
{
    bool ret = true;

    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;

    ControlInterfaceConfigData config_data = {
        .control_interface_adapter_handle = EfaAdapterGetAdapterControlInterface(efa_endpoint_ptr),
        .msg_from_endpoint_func_ptr = ProbeTxControlMessageFromEndpoint,
        .msg_from_endpoint_param_ptr = probe_ptr,
        .log_handle = probe_ptr->log_handle,
        .tx_dest_ip_addr_str = dest_ip_addr_str,
        .port_number = dest_port
    };
    if (kCdiStatusOk != ControlInterfaceCreate(&config_data, kEndpointDirectionSend,
                                               &efa_endpoint_ptr->tx_control_handle)) {
        ret = false;
    }

    // The control interfaces are independent of the adapter endpoint, so we want to start them now.
    if (ret) {
        // Start Rx control interface.
        if (kCdiStatusOk != CdiAdapterStartEndpoint(ControlInterfaceGetEndpoint(efa_endpoint_ptr->tx_control_handle))) {
            ret = false;
        }
    }

    return ret;
}

bool ProbeTxControlProcessPacket(ProbeEndpointState* probe_ptr, const ControlPacketCommonHeader* common_hdr_ptr,
                                 uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    EfaEndpointState* efa_endpoint_state_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;

    switch (common_hdr_ptr->command) {
        case kProbeCommandReset:
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Tx stream ID[%d] got Reset command from Rx. Restarting EFA connection.",
                                     common_hdr_ptr->senders_stream_identifier);
            // Queue Endpoint Manager to reset the EFA connection and notify the application that we are disconnected.
            ProbeControlEfaConnectionQueueReset(probe_ptr, NULL);

            // Get latest GID from remote.
            memcpy(efa_endpoint_state_ptr->remote_ipv6_gid_array, common_hdr_ptr->senders_gid_array,
                   sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array));

            probe_ptr->tx_probe_state.tx_state = kProbeStateResetting;
            *wait_timeout_ms_ptr = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
            ret_new_state = true;
            break;
        case kProbeCommandAck:
            CdiOsCritSectionReserve(probe_ptr->ack_lock); // Lock access to the ack state data.
            ControlPacketAck* packet_ack_ptr = (ControlPacketAck*)common_hdr_ptr;

            // Check if we sent a command and are waiting for an ACK for it. If not, ignore the ACK.
            if (probe_ptr->ack_is_pending) {
                // We are waiting for an ACK. Check if the ACK contains the same command and probe packet number of
                // the command that was sent.

                // Ensure the sizes of these values are the same, so wrapping doesn't affect results when comparing
                // them.
                assert(sizeof(packet_ack_ptr->ack_control_packet_num) == sizeof(probe_ptr->ack_control_packet_num));

                if (packet_ack_ptr->ack_command == probe_ptr->ack_command &&
                    packet_ack_ptr->ack_control_packet_num == probe_ptr->ack_control_packet_num) {
                    // It matches, so we got the ACK for the command that was sent.
                    probe_ptr->ack_is_pending = false;

                    // Don't log the ping ACK commands (generates too many log messages).
                    if (kProbeCommandPing != packet_ack_ptr->ack_command) {
                        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe Tx stream ID[%d] accepted ACK.",
                                                 common_hdr_ptr->senders_stream_identifier);
                        CDI_LOG_THREAD(kLogInfo, "Received connection response");
                    }

                    if (kProbeCommandReset == packet_ack_ptr->ack_command) {
                        // Get latest GID from remote.
                        memcpy(efa_endpoint_state_ptr->remote_ipv6_gid_array, common_hdr_ptr->senders_gid_array,
                               sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array));

                        char gid_name_str[MAX_IPV6_ADDRESS_STRING_LENGTH];
                        DeviceGidToString(efa_endpoint_state_ptr->remote_ipv6_gid_array,
                                          sizeof(efa_endpoint_state_ptr->remote_ipv6_gid_array), gid_name_str,
                                          sizeof(gid_name_str));
                        CDI_LOG_THREAD(kLogInfo, "Probe Tx stream ID[%d] using remote EFA device GID[%s].",
                                       common_hdr_ptr->senders_stream_identifier, gid_name_str);
                        EndpointManagerQueueEndpointStart(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle);

                        // Advance to wait for start state.
                        probe_ptr->tx_probe_state.tx_state = kProbeStateWaitForStart;
                        *wait_timeout_ms_ptr = ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC;
                        ret_new_state = true;
                    } else if (kProbeCommandPing == packet_ack_ptr->ack_command) {
                        // Got an ACK for a ping command. Drop back to the EFA connected state, which will repeat the
                        // ping process. Setup wait period for next ping based on ping frequency.
                        probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnected;
                        *wait_timeout_ms_ptr = SEND_PING_COMMAND_FREQUENCY_MSEC;
                        ret_new_state = true;
                    } else {
                        assert(false); // No other supported commands return an Ack.
                    }
                } else {
                    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                        "Probe Tx stream ID[%d] ignoring ACK. Got ACK for command[%s] packet_num[%d]. Expected "
                        "command[%s] packet_num[%d].", common_hdr_ptr->senders_stream_identifier,
                        InternalUtilityKeyEnumToString(kKeyProbeCommand, packet_ack_ptr->ack_command),
                        packet_ack_ptr->ack_control_packet_num,
                        InternalUtilityKeyEnumToString(kKeyProbeCommand, probe_ptr->ack_command),
                        probe_ptr->ack_control_packet_num);
                }
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Tx stream ID[%d] ignoring unexpected ACK.",
                                         common_hdr_ptr->senders_stream_identifier);
            }
            CdiOsCritSectionRelease(probe_ptr->ack_lock); // Release access to the ack state data.
            break;
        case kProbeCommandConnected:
            if (kProbeStateEfaProbe != probe_ptr->tx_probe_state.tx_state) {
                // We are not expecting a connection command yet, so send a reset.
                probe_ptr->tx_probe_state.tx_state = kProbeStateSendReset;
                *wait_timeout_ms_ptr = 0; // Take effect immediately.
                ret_new_state = true;
            } else {
                // Got a connected command from receiver. Enable application connection.
                ProbeControlEfaConnectionEnableApplication(probe_ptr);

                // Advance to the connected state, which will start the ping process. Setup wait period for next ping
                // based on ping frequency.
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnected;
                *wait_timeout_ms_ptr = SEND_PING_COMMAND_FREQUENCY_MSEC;
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
    int stream_identifier = EndpointManagerEndpointStreamIdGet(cdi_endpoint_handle);

    // Don't log the kProbeStateEfaConnected state. It is used for ping (generates too many log messages).
    if (kProbeStateEfaConnected != probe_ptr->tx_probe_state.tx_state) {
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe Tx stream ID[%d] state[%s]",
                                 stream_identifier,
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
        case kProbeStateSendReset:
            // Notify application that we are disconnected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusDisconnected, NULL);
            // Send command to reset the remote Rx (server) connection. Will expect an ACK back from the remote.
            ProbeControlSendCommand(probe_ptr, kProbeCommandReset, true);
            wait_timeout_ms = SEND_RESET_COMMAND_FREQUENCY_MSEC;
            break;
        case kProbeStateEfaStart:
            // Enable the EFA connection for probe state. Use the EFA interface to send probe packets before allowing
            // application to use the connection. Once all the probe packets have been acknowledged as being received by
            // the remote, it will send a kProbeCommandConnected command back. Start this process here.
            CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                     "Probe Tx stream ID[%d] starting the SRD probe process", stream_identifier);
            ProbeControlEfaConnectionStart(probe_ptr);
            EfaEnqueueSendProbePackets(probe_ptr);
            probe_ptr->tx_probe_state.tx_state = kProbeStateEfaProbe;
            // If the EFA probe does not complete by this timeout, we return back to connection reset state.
            wait_timeout_ms = EFA_PROBE_MONITOR_TIMEOUT_MSEC;
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
        case kProbeStateEfaConnected:
#ifdef DISABLE_PROBE_MONITORING
            wait_timeout_ms = CDI_INFINITE;
#else
            // Notify application that we are connected.
            EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusConnected, NULL);

            // Send command to ping the remote Rx (server) connection. Will expect an ACK back from the remote.
            ProbeControlSendCommand(probe_ptr, kProbeCommandPing, true);
            probe_ptr->tx_probe_state.tx_state = kProbeStateEfaConnectedPing;
            probe_ptr->tx_probe_state.ping_retry_count = 0; // Reset ping retry counter.
            wait_timeout_ms = TX_PING_ACK_TIMEOUT_MSEC;
#endif
            break;
        case kProbeStateEfaConnectedPing:
            // Did not get an Ack back from the ping within the timeout period.
            probe_ptr->app_adapter_endpoint_handle->endpoint_stats_ptr->probe_command_retry_count++;
            if (probe_ptr->tx_probe_state.ping_retry_count++ < TX_PING_MAX_RETRIES) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Probe Tx stream ID[%d] EFA ping Ack timeout. Resending ping #[%d].",
                    stream_identifier, probe_ptr->tx_probe_state.ping_retry_count);
                ProbeControlSendCommand(probe_ptr, kProbeCommandPing, true);
                wait_timeout_ms = TX_PING_ACK_TIMEOUT_MSEC;
            } else {
                //  Reset the connection.
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Probe Tx stream ID[%d] EFA ping Ack timeout. Tried[%d] times. Now sending reset to Rx.",
                    stream_identifier, TX_PING_MAX_RETRIES);
                probe_ptr->tx_probe_state.tx_state = kProbeStateEfaReset; // Advance to resetting state.
                wait_timeout_ms = 0; // Do immediately.
            }
            break;
        case kProbeStateDestroy:
            // Nothing special needed.
            break;
    }

    return wait_timeout_ms;
}
