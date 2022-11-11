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
#include "adapter_efa_probe_control.h"

#include "adapter_api.h"
#include "adapter_efa_probe_rx.h"
#include "adapter_efa_probe_tx.h"
#include "cdi_os_api.h"
#include "endpoint_manager.h"
#include "internal_utility.h"
#include "protocol.h"

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
 * Initialize the command packet header of a control interface packet.
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param command Probe command to use.
 * @param decoded_hdr_ptr Pointer to decoded header data. On entry, the union data must already be set.
 * @param work_request_ptr Pointer to work request state data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus EncodeProbeHeader(ProbeEndpointState* probe_ptr, ProbeCommand command,
                                         CdiDecodedProbeHeader* decoded_hdr_ptr,
                                         ProbePacketWorkRequest* work_request_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;
    CdiProtocolHandle protocol_handle = probe_ptr->app_adapter_endpoint_handle->protocol_handle;
    CdiRawProbeHeader* dest_hdr_ptr = &work_request_ptr->packet_data;

    // Get port being used by the Tx control adapter.
    int dest_port = 0;
    rs = CdiAdapterGetPort(ControlInterfaceGetEndpoint(adapter_con_ptr->control_interface_handle), &dest_port);
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "CdiAdapterGetPort failed. Reason[%s].", CdiCoreStatusToString(rs));
    } else {
        decoded_hdr_ptr->command = command;

        // NOTE: The decoded variant uses pointers to strings and arrays, so no need to copy memory. The encoded variant
        // uses its own memory, so the memory copy is done once as part of the encode process.
        decoded_hdr_ptr->senders_ip_str = adapter_con_ptr->adapter_state_ptr->adapter_data.adapter_ip_addr_str;

        decoded_hdr_ptr->senders_control_dest_port = (uint16_t)dest_port;

        EfaEndpointState* efa_endpoint_state_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
        decoded_hdr_ptr->senders_gid_array = efa_endpoint_state_ptr->local_ipv6_gid_array;

        const char* stream_name_str = EndpointManagerEndpointStreamNameGet(endpoint_ptr->cdi_endpoint_handle);
        decoded_hdr_ptr->senders_stream_name_str = stream_name_str;
        decoded_hdr_ptr->control_packet_num = CdiOsAtomicInc16(&probe_ptr->control_packet_num);

        // Now encode the header data using the desired protocol version.
        if (NULL == protocol_handle) {
            // If command is an ACK and the protocol on the remote endpoint supports protocols v3 OR if sending the
            // protocol version command, then encode the header with the version from this SDK.
            if ((kProbeCommandAck == command && (probe_ptr->send_ack_probe_version >= 3)) ||
                (kProbeCommandProtocolVersion == command)) {
                protocol_handle = probe_ptr->protocol_handle_sdk;
            } else {
                // Otherwise, use the legacy protocol.
                protocol_handle = probe_ptr->protocol_handle_v1;
            }
        }

        // Encode the probe header. The protocol version data is set within the call (so no need to set it here).
        int encoded_size = ProtocolProbeHeaderEncode(protocol_handle, decoded_hdr_ptr, dest_hdr_ptr);

        // Update size of the SGL packet in the work request.
        ProbeControlWorkRequestPacketSizeSet(work_request_ptr, encoded_size);
    }

    return rs;
}

/**
 * Process a received control packet.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param probe_ptr Pointer to probe endpoint state data.
 * @param control_command_ptr Pointer to control packet to process.
 * @param wait_timeout_ms_ptr Pointer to current wait timeout. This function may alter the contents of the value
 *                            depending on the command contained within the control packet.
 */
static bool ProcessPacket(ProbeEndpointState* probe_ptr, const ControlCommand* control_command_ptr,
                          uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;
    const CdiSgList* packet_sgl_ptr = &control_command_ptr->receive_packet.packet_sgl;

    CdiDecodedProbeHeader header = { 0 };
    CdiReturnStatus rs = ProtocolProbeHeaderDecode(packet_sgl_ptr->sgl_head_ptr->address_ptr,
                                                   packet_sgl_ptr->total_data_size, &header);

    if (kCdiStatusOk == rs) {
        assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
        if (kEndpointDirectionSend == adapter_con_ptr->direction) {
            ret_new_state = ProbeTxControlProcessPacket(probe_ptr, &header, wait_timeout_ms_ptr);
        } else {
            ret_new_state = ProbeRxControlProcessPacket(probe_ptr, &header,
                                                        &control_command_ptr->receive_packet.source_address,
                                                        wait_timeout_ms_ptr);
        }
    }

    CdiAdapterFreeBuffer(ControlInterfaceGetEndpoint(adapter_con_ptr->control_interface_handle), packet_sgl_ptr);

    return ret_new_state;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool ProbeControlEfaConnectionStart(ProbeEndpointState* probe_ptr)
{
    bool ret = true;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    // Set endpoint callback functions and related parameters to point to our probe variants.
    assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
    if (kEndpointDirectionSend == adapter_con_ptr->direction) {
        endpoint_ptr->msg_from_endpoint_func_ptr = ProbeTxEfaMessageFromEndpoint;
        // Reset EFA Tx packet/ack received counters.
        probe_ptr->tx_probe_state.send_command_retry_count = 0;
        probe_ptr->tx_probe_state.packets_acked_count = 0;
        probe_ptr->tx_probe_state.packets_ack_wait_count = 0;

        // Set initial value of payload in-flight reference count to one (represents one payload). All probe packets use
        // a single payload.
        CdiOsAtomicStore32(&endpoint_ptr->tx_in_flight_ref_count, 1);
        CdiOsSignalSet(adapter_con_ptr->tx_poll_do_work_signal); // Ensure PollThread() is ready for work.
    } else {
        endpoint_ptr->msg_from_endpoint_func_ptr = ProbeRxEfaMessageFromEndpoint;
        // Reset EFA Rx packet/ping received counters.
        probe_ptr->rx_probe_state.send_reset_retry_count = 0;
        probe_ptr->rx_probe_state.packets_received_count = 0;
    }
    endpoint_ptr->msg_from_endpoint_param_ptr = probe_ptr;

    // Start the application's EFA connection.
    EfaEndpointState* efa_endpoint_state_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;
    if (kCdiStatusOk != EfaAdapterEndpointStart(efa_endpoint_state_ptr)) {
        ret = false;
    }

    return ret;
}

void ProbeControlEfaConnectionQueueReset(ProbeEndpointState* probe_ptr, const char* error_msg_str)
{
    CdiEndpointHandle cdi_endpoint_handle = probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle;

    // Notify the application of the connection state change.
    EndpointManagerConnectionStateChange(cdi_endpoint_handle, kCdiConnectionStatusDisconnected, error_msg_str);

    EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
    // Clear GID.
    memset(efa_endpoint_ptr->remote_ipv6_gid_array, 0, sizeof(efa_endpoint_ptr->remote_ipv6_gid_array));

    // Notify Endpoint Manager to reset the connection.
    EndpointManagerQueueEndpointReset(cdi_endpoint_handle);
}

void ProbeControlQueueStateChange(ProbeEndpointState* probe_ptr, ProbeState probe_state)
{
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    ControlCommand control_cmd = {
        .command_type = kCommandTypeStateChange,
        .probe_state = probe_state,
    };

    CdiSignalType shutdown_signal = adapter_con_ptr->shutdown_signal;
    CdiFifoWrite(probe_ptr->control_packet_fifo_handle, CDI_INFINITE, shutdown_signal, &control_cmd);
}

void ProbeControlEfaConnectionEnableApplication(ProbeEndpointState* probe_ptr)
{
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;

    // Setup message functions and related parameters to point to the application variants.
    endpoint_ptr->msg_from_endpoint_func_ptr = probe_ptr->app_msg_from_endpoint_func_ptr;
    endpoint_ptr->msg_from_endpoint_param_ptr = probe_ptr->app_msg_from_endpoint_param_ptr;

    ProbeControlQueueStateChange(probe_ptr, kProbeStateEfaConnected);
}

ProbePacketWorkRequest* ProbeControlWorkRequestGet(CdiPoolHandle work_request_pool_handle)
{
    ProbePacketWorkRequest* work_request_ptr = NULL;
    if (!CdiPoolGet(work_request_pool_handle, (void**)&work_request_ptr)) {
        CDI_LOG_THREAD(kLogError, "Unable to get a control work request from pool[%s]",
                       CdiPoolGetName(work_request_pool_handle));
        return NULL;
    }

    work_request_ptr->sgl_entry.address_ptr = &work_request_ptr->packet_data;
    work_request_ptr->sgl_entry.size_in_bytes = 0;

    work_request_ptr->packet.sg_list.total_data_size = 0;
    work_request_ptr->packet.sg_list.sgl_head_ptr = &work_request_ptr->sgl_entry;
    work_request_ptr->packet.sg_list.sgl_tail_ptr = &work_request_ptr->sgl_entry;
    work_request_ptr->packet.sg_list.internal_data_ptr = work_request_ptr;

    return work_request_ptr;
}

void ProbeControlWorkRequestPacketSizeSet(ProbePacketWorkRequest* work_request_ptr, int packet_size)
{
    work_request_ptr->sgl_entry.size_in_bytes = packet_size;
    work_request_ptr->packet.sg_list.total_data_size = packet_size;
}

CdiReturnStatus ProbeControlSendCommand(ProbeEndpointState* probe_ptr, ProbeCommand command, bool requires_ack)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    CdiDecodedProbeHeader header = { 0 };
    CdiPoolHandle control_work_request_pool_handle =
        ControlInterfaceGetWorkRequestPoolHandle(adapter_con_ptr->control_interface_handle);
    ProbePacketWorkRequest* work_request_ptr = ProbeControlWorkRequestGet(control_work_request_pool_handle);
    if (NULL == work_request_ptr) {
       rs = kCdiStatusAllocationFailed;
    } else {
        header.command_packet.requires_ack = requires_ack;
        EncodeProbeHeader(probe_ptr, command, &header, work_request_ptr);
    }
    const char* command_str = InternalUtilityKeyEnumToString(kKeyProbeCommand, command);

    if (kCdiStatusOk == rs) {
        if (requires_ack) {
            CdiOsCritSectionReserve(probe_ptr->ack_lock); // Lock access to the ack state data.
            probe_ptr->ack_is_pending = true;
            probe_ptr->ack_command = command;
            probe_ptr->ack_control_packet_num = header.control_packet_num;
            CdiOsCritSectionRelease(probe_ptr->ack_lock); // Release access to the ack state data.
        }

        // Don't log the ping commands (generates too many log messages).
        if (kProbeCommandPing != command) {
            assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
            if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Tx remote IP[%s:%d] local port[%d] sending command[%s] to Rx. packet_num[%d] ack[%d].",
                                         EndpointManagerEndpointRemoteIpGet(endpoint_ptr->cdi_endpoint_handle),
                                         EndpointManagerEndpointRemotePortGet(endpoint_ptr->cdi_endpoint_handle),
                                         header.senders_control_dest_port,
                                         command_str, header.control_packet_num, requires_ack);
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Rx remote IP[%s:%d] sending command[%s] to Tx. packet_num[%d] ack[%d].",
                                         EndpointManagerEndpointRemoteIpGet(endpoint_ptr->cdi_endpoint_handle),
                                         EndpointManagerEndpointRemotePortGet(endpoint_ptr->cdi_endpoint_handle),
                                         command_str, header.control_packet_num, requires_ack);
            }
            if (kProbeCommandReset == command) {
                CDI_LOG_THREAD(kLogInfo, "Sending connection request.");
            }
        }

        // Put packet message in the adapter's endpoint packet queue. We use "true" here so the packet is sent
        // immediately.
        rs = CdiAdapterEnqueueSendPacket(ControlInterfaceGetEndpoint(adapter_con_ptr->control_interface_handle),
                                         EndpointManagerEndpointRemoteAddressGet(endpoint_ptr->cdi_endpoint_handle),
                                         &work_request_ptr->packet);
    }

    if (kCdiStatusOk != rs && work_request_ptr) {
        CDI_LOG_THREAD(kLogError, "Failed to send probe command[%s].", command_str);
        // Put back work request into the pool.
        CdiPoolPut(control_work_request_pool_handle, work_request_ptr);
    }

    return rs;
}

CdiReturnStatus ProbeControlSendAck(ProbeEndpointState* probe_ptr, ProbeCommand ack_command,
                                    uint16_t ack_probe_packet_num)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    CdiDecodedProbeHeader header = { 0 };
    CdiPoolHandle control_work_request_pool_handle =
        ControlInterfaceGetWorkRequestPoolHandle(adapter_con_ptr->control_interface_handle);
    ProbePacketWorkRequest* work_request_ptr = ProbeControlWorkRequestGet(control_work_request_pool_handle);
    if (NULL == work_request_ptr) {
        rs = kCdiStatusAllocationFailed;
    } else {
        header.ack_packet.ack_command = ack_command;
        header.ack_packet.ack_control_packet_num = ack_probe_packet_num;
        EncodeProbeHeader(probe_ptr, kProbeCommandAck, &header, work_request_ptr);
    }

    if (kCdiStatusOk == rs) {
        // Don't log the ping ACK commands (generates too many log messages).
        if (kProbeCommandPing != ack_command) {
            assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
            if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Tx remote IP[%s:%d] got command[%s], packet_num[%d]. Sending ACK packet_num[%d] to Rx.",
                    EndpointManagerEndpointRemoteIpGet(endpoint_ptr->cdi_endpoint_handle),
                    EndpointManagerEndpointRemotePortGet(endpoint_ptr->cdi_endpoint_handle),
                    InternalUtilityKeyEnumToString(kKeyProbeCommand, ack_command),
                    header.control_packet_num, ack_probe_packet_num);
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Rx remote IP[%s:%d] got command[%s], packet_num[%d]. Sending ACK packet_num[%d]"
                    " to Tx.",
                    EndpointManagerEndpointRemoteIpGet(endpoint_ptr->cdi_endpoint_handle),
                    EndpointManagerEndpointRemotePortGet(endpoint_ptr->cdi_endpoint_handle),
                    InternalUtilityKeyEnumToString(kKeyProbeCommand, ack_command),
                    header.control_packet_num, ack_probe_packet_num);
            }
        }

        // Put packet message in the adapter's endpoint packet queue.
        rs = CdiAdapterEnqueueSendPacket(ControlInterfaceGetEndpoint(adapter_con_ptr->control_interface_handle),
                                         EndpointManagerEndpointRemoteAddressGet(endpoint_ptr->cdi_endpoint_handle),
                                         &work_request_ptr->packet);
    }

    if (kCdiStatusOk != rs && work_request_ptr) {
        // Put back work request into the pool.
        CdiPoolPut(control_work_request_pool_handle, work_request_ptr);
    }

    return rs;
}

void ProbeControlMessageFromBidirectionalEndpoint(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type)
{
    AdapterConnectionState* adapter_con_ptr = (AdapterConnectionState*)param_ptr;
    if (kEndpointMessageTypePacketSent == message_type) {
        ProbeTxControlMessageFromEndpoint(adapter_con_ptr, packet_ptr);
    } else {
        ProbeRxControlMessageFromEndpoint(adapter_con_ptr, packet_ptr);
    }
}

CDI_THREAD ProbeControlThread(void* ptr)
{
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)ptr;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;
    const char* remote_ip_str = EndpointManagerEndpointRemoteIpGet(endpoint_ptr->cdi_endpoint_handle);
    const int remote_port = EndpointManagerEndpointRemotePortGet(endpoint_ptr->cdi_endpoint_handle);

    CdiSignalType shutdown_signal = endpoint_ptr->shutdown_signal;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(probe_ptr->log_handle);

    uint64_t start_time_us = CdiOsGetMicroseconds();
    uint64_t wait_timeout_ms = 0; // Start trying immediately to establish a connection.
    assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
    if (kEndpointDirectionSend == adapter_con_ptr->direction) {
        probe_ptr->tx_probe_state.tx_state = kProbeStateIdle;
    } else {
        probe_ptr->rx_probe_state.rx_state = kProbeStateSendReset;
    }

    while (!CdiOsSignalGet(shutdown_signal)) {
        // Wait for an incoming control command message to arrive, timeout or abort if we are shutting down.
        ControlCommand control_cmd;
        if (CdiFifoRead(probe_ptr->control_packet_fifo_handle, wait_timeout_ms, shutdown_signal, &control_cmd)) {
            if (kCommandTypeStateChange == control_cmd.command_type) {
                // Received a probe command directly from the local instance.
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe remote IP[%s:%d] process state[%s] change.",
                    remote_ip_str, remote_port, InternalUtilityKeyEnumToString(kKeyProbeState, control_cmd.probe_state));
                if (kProbeStateEfaConnected == control_cmd.probe_state) {
                    CdiProtocolHandle protocol_handle = endpoint_ptr->protocol_handle;
                    CDI_LOG_THREAD(kLogInfo, "Connection established using protocol version[%d.%d.%d].",
                            protocol_handle->negotiated_version.version_num,
                            protocol_handle->negotiated_version.major_version_num,
                            protocol_handle->negotiated_version.probe_version_num);
                }

                // Set probe state, depending on endpoint direction type.
                assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
                ProbeState* current_probe_state_ptr = (kEndpointDirectionSend == adapter_con_ptr->direction) ?
                                                      &probe_ptr->tx_probe_state.tx_state :
                                                      &probe_ptr->rx_probe_state.rx_state;
                *current_probe_state_ptr = control_cmd.probe_state;
                wait_timeout_ms = 0; // Set to zero so the state change is executed immediately in the code below.
            } else {
                // Received a control packet.
                if (ProcessPacket(probe_ptr, &control_cmd, &wait_timeout_ms)) {
                    // We have a new probe state, so setup our start time to wait for it before it gets processed.
                    start_time_us = CdiOsGetMicroseconds();
                }
            }
        } else {
            // Got a FIFO timeout or shutdown signal.
            if (CdiOsSignalGet(shutdown_signal)) {
                break; // Got shutdown signal, so exit this loop.
            }
        }

        // Either we got a command in the FIFO or the wait timed-out. Check to see if we have any additional time to
        // wait before processing the current probe state.
        uint64_t elapsed_time_ms = (CdiOsGetMicroseconds() - start_time_us) / 1000; // Convert to milliseconds
        if (elapsed_time_ms < wait_timeout_ms) {
            // Still have some time remaining before timeout period has elapsed. Adjust how long to wait and then wait
            // again.
            wait_timeout_ms -= elapsed_time_ms;
        } else {
            // Got timeout. Perform operation based on our current state.
            do {
                assert(kEndpointDirectionBidirectional != adapter_con_ptr->direction);
                if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                    wait_timeout_ms = ProbeTxControlProcessProbeState(probe_ptr); // Transmitter
                } else {
                    wait_timeout_ms = ProbeRxControlProcessProbeState(probe_ptr); // Receiver
                }
                // Stay in the loop, in case we need to process multiple states.
            } while (0 == wait_timeout_ms);
        }

        // Processed a command, so reset the command start time to the current time.
        start_time_us = CdiOsGetMicroseconds();
    }

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Shut down probe thread for endpoint IP[%s:%d].",
        remote_ip_str, remote_port);

    CdiLoggerThreadLogUnset();
    return 0; // Return code not used.
}
