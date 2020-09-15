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
#include "adapter_efa_probe_control.h"

#include "adapter_api.h"
#include "adapter_efa_probe_rx.h"
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

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

uint16_t ProbeControlChecksum(const uint16_t* buffer_ptr, int size)
{
    uint32_t cksum = 0;

    // Sum entire packet.
    while (size > 1) {
        cksum += *buffer_ptr++;
        size -= 2;
    }

    // Pad to 16-bit boundary if necessary.
    if (size == 1) {
        cksum += *(uint8_t*)buffer_ptr;
    }

    // Add carries and do one's complement.
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return (uint16_t)(~cksum);
}

bool ProbeControlEfaConnectionStart(ProbeEndpointState* probe_ptr)
{
    bool ret = true;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    // Set endpoint callback functions and related parameters to point to our probe variants.
    if (kEndpointDirectionSend == adapter_con_ptr->direction) {
        endpoint_ptr->msg_from_endpoint_func_ptr = ProbeTxEfaMessageFromEndpoint;
        // Reset EFA Tx packet/ack received counters.
        probe_ptr->tx_probe_state.ping_retry_count = 0;

        CdiOsSignalSet(adapter_con_ptr->poll_do_work_signal); // Ensure PollThread() is ready for work.
    } else {
        endpoint_ptr->msg_from_endpoint_func_ptr = ProbeRxEfaMessageFromEndpoint;
        // Reset EFA Rx packet/ping received counters.
        probe_ptr->rx_probe_state.packets_received_count = 0;
        probe_ptr->rx_probe_state.pings_received_count = 0;
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
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    // Setup message functions and related parameters to point to the application variants.
    endpoint_ptr->msg_from_endpoint_func_ptr = probe_ptr->app_msg_from_endpoint_func_ptr;
    endpoint_ptr->msg_from_endpoint_param_ptr = probe_ptr->app_msg_from_endpoint_param_ptr;

    if (kEndpointDirectionSend == adapter_con_ptr->direction) {
        // Tx probe is done with EFA, so can let PollThread() sleep.
        CdiOsSignalClear(adapter_con_ptr->poll_do_work_signal);
    }

    // Post control command to change to EFA connected mode. This will change the endpoint's connection state to
    // kCdiConnectionStatusConnected.
    ProbeControlQueueStateChange(probe_ptr, kProbeStateEfaConnected);
}

ProbePacketWorkRequest* ProbeControlWorkRequestGet(CdiPoolHandle work_request_pool_handle, int packet_size)
{
    ProbePacketWorkRequest* work_request_ptr = NULL;
    if (!CdiPoolGet(work_request_pool_handle, (void**)&work_request_ptr)) {
        CDI_LOG_THREAD(kLogError, "Unable to get a control work request from pool[%s]",
                       CdiPoolGetName(work_request_pool_handle));
        assert(false);
        return NULL;
    }

    work_request_ptr->sgl_entry.address_ptr = &work_request_ptr->packet_data;
    work_request_ptr->sgl_entry.size_in_bytes = packet_size;

    work_request_ptr->packet.sg_list.total_data_size = packet_size;
    work_request_ptr->packet.sg_list.sgl_head_ptr = &work_request_ptr->sgl_entry;
    work_request_ptr->packet.sg_list.sgl_tail_ptr = &work_request_ptr->sgl_entry;
    work_request_ptr->packet.sg_list.internal_data_ptr = work_request_ptr;

    return work_request_ptr;
}

void ProbeControlInitPacketCommonHeader(ProbeEndpointState* probe_ptr, ProbeCommand command,
                                        ControlPacketCommonHeader* header_ptr)
{
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    header_ptr->senders_version_num = CDI_SDK_VERSION;
    header_ptr->senders_major_version_num = CDI_SDK_MAJOR_VERSION;
    header_ptr->senders_minor_version_num = CDI_SDK_MINOR_VERSION;
    header_ptr->checksum = 0;

    header_ptr->command = command;

    CdiOsStrCpy(header_ptr->senders_ip_str, sizeof(header_ptr->senders_ip_str),
                adapter_con_ptr->adapter_state_ptr->adapter_data.adapter_ip_addr_str);

    // Get port being used by the Tx control adapter.
    int dest_port = 0;
    EfaConnectionState* efa_con_ptr = (EfaConnectionState*)adapter_con_ptr->type_specific_ptr;
    if (kCdiStatusOk != CdiAdapterGetPort(ControlInterfaceGetEndpoint(efa_con_ptr->rx_control_handle), &dest_port)) {
        assert(false);
    }
    header_ptr->senders_control_dest_port = (uint16_t)dest_port;

    EfaEndpointState* efa_endpoint_state_ptr = (EfaEndpointState*)probe_ptr->app_adapter_endpoint_handle->type_specific_ptr;
    memcpy(header_ptr->senders_gid_array, efa_endpoint_state_ptr->local_ipv6_gid_array,
           sizeof(header_ptr->senders_gid_array));

    const char* stream_name_str = EndpointManagerEndpointStreamNameGet(endpoint_ptr->cdi_endpoint_handle);
    if (stream_name_str) {
        CdiOsStrCpy(header_ptr->senders_stream_name_str, sizeof(header_ptr->senders_stream_name_str),
                    stream_name_str);
    }
    header_ptr->senders_stream_identifier = EndpointManagerEndpointStreamIdGet(endpoint_ptr->cdi_endpoint_handle);
    header_ptr->control_packet_num = CdiOsAtomicInc16(&probe_ptr->control_packet_num);
}

CdiReturnStatus ProbeControlSendCommand(ProbeEndpointState* probe_ptr, ProbeCommand command, bool requires_ack)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    ControlPacketCommand* packet_ptr = NULL;
    ProbePacketWorkRequest* work_request_ptr = ProbeControlWorkRequestGet(probe_ptr->control_work_request_pool_handle,
                                                                          sizeof(*packet_ptr));
    if (NULL == work_request_ptr) {
       rs = kCdiStatusAllocationFailed;
    } else {
        packet_ptr = (ControlPacketCommand*)&work_request_ptr->packet_data;

        ProbeControlInitPacketCommonHeader(probe_ptr, command, &packet_ptr->common_hdr);
        packet_ptr->requires_ack = requires_ack;

        if (requires_ack) {
            CdiOsCritSectionReserve(probe_ptr->ack_lock); // Lock access to the ack state data.
            probe_ptr->ack_is_pending = true;
            probe_ptr->ack_command = packet_ptr->common_hdr.command;
            probe_ptr->ack_control_packet_num = packet_ptr->common_hdr.control_packet_num;
            CdiOsCritSectionRelease(probe_ptr->ack_lock); // Release access to the ack state data.
        }

        // Calculate the packet checksum.
        packet_ptr->common_hdr.checksum = ProbeControlChecksum((uint16_t*)packet_ptr, sizeof(*packet_ptr));

        // Don't log the ping commands (generates too many log messages).
        if (kProbeCommandPing != command) {
            if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Tx stream ID[%d] sending command[%s] to Rx. packet_num[%d] ack[%d].",
                                         packet_ptr->common_hdr.senders_stream_identifier,
                                         InternalUtilityKeyEnumToString(kKeyProbeCommand, command),
                                         packet_ptr->common_hdr.control_packet_num, requires_ack);
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Probe Rx stream ID[%d] sending command[%s] to Tx. packet_num[%d] ack[%d].",
                                         packet_ptr->common_hdr.senders_stream_identifier,
                                         InternalUtilityKeyEnumToString(kKeyProbeCommand, command),
                                         packet_ptr->common_hdr.control_packet_num, requires_ack);
            }
        }

        // Put packet message in the adapter's endpoint packet queue. We use "true" here so the packet is sent
        // immediately.
        EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;
        rs = CdiAdapterEnqueueSendPacket(ControlInterfaceGetEndpoint(efa_endpoint_ptr->tx_control_handle),
                                         &work_request_ptr->packet);
    }

    if (kCdiStatusOk != rs && work_request_ptr) {
        // Put back work request into the pool.
        CdiPoolPut(probe_ptr->control_work_request_pool_handle, work_request_ptr);
    }

    return rs;
}

CdiReturnStatus ProbeControlSendAck(ProbeEndpointState* probe_ptr, ProbeCommand ack_command,
                                    uint16_t ack_probe_packet_num)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    ControlPacketAck* packet_ptr = NULL;
    ProbePacketWorkRequest* work_request_ptr = ProbeControlWorkRequestGet(probe_ptr->control_work_request_pool_handle,
                                                                          sizeof(*packet_ptr));
    if (NULL == work_request_ptr) {
        rs = kCdiStatusAllocationFailed;
    } else {
        packet_ptr = (ControlPacketAck*)&work_request_ptr->packet_data;
        ProbeControlInitPacketCommonHeader(probe_ptr, kProbeCommandAck, &packet_ptr->common_hdr);
        packet_ptr->ack_command = ack_command;
        packet_ptr->ack_control_packet_num = ack_probe_packet_num;
        packet_ptr->common_hdr.checksum = ProbeControlChecksum((uint16_t*)packet_ptr, sizeof(*packet_ptr));

        // Don't log the ping ACK commands (generates too many log messages).
        if (kProbeCommandPing != ack_command) {
            if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Tx stream ID[%d] got command[%s], packet_num[%d]. Sending Ack packet_num[%d] to Rx.",
                    EndpointManagerEndpointStreamIdGet(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle),
                    InternalUtilityKeyEnumToString(kKeyProbeCommand, ack_command),
                    ack_probe_packet_num, packet_ptr->common_hdr.control_packet_num);
            } else {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                    "Rx stream ID[%d] got command[%s], packet_num[%d]. Sending Ack packet_num[%d] to Tx.",
                    EndpointManagerEndpointStreamIdGet(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle),
                    InternalUtilityKeyEnumToString(kKeyProbeCommand, ack_command),
                    ack_probe_packet_num, packet_ptr->common_hdr.control_packet_num);
            }
        }

        // Put packet message in the adapter's endpoint packet queue. We use "true" here so the packet is sent
        // immediately.
        EfaEndpointState* efa_endpoint_ptr = (EfaEndpointState*)endpoint_ptr->type_specific_ptr;
        rs = CdiAdapterEnqueueSendPacket(ControlInterfaceGetEndpoint(efa_endpoint_ptr->tx_control_handle),
                                         &work_request_ptr->packet);
    }

    return rs;
}

bool ProbeControlProcessPacket(ProbeEndpointState* probe_ptr, CdiSgList* packet_sgl_ptr,
                               uint64_t* wait_timeout_ms_ptr)
{
    bool ret_new_state = false;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;
    ControlPacketCommonHeader* common_hdr_ptr = (ControlPacketCommonHeader*)packet_sgl_ptr->sgl_head_ptr->address_ptr;

    if (CDI_SDK_VERSION != common_hdr_ptr->senders_version_num ||
        CDI_SDK_MAJOR_VERSION != common_hdr_ptr->senders_major_version_num) {
        char error_msg_str[MAX_ERROR_STRING_LENGTH];

        snprintf(error_msg_str, sizeof(error_msg_str),
                 "Remote CDI SDK not compatible. This version[%d.%d.%d]. Remote version[%d.%d.%d]",
                 CDI_SDK_VERSION, CDI_SDK_MAJOR_VERSION, CDI_SDK_MINOR_VERSION,
                 common_hdr_ptr->senders_version_num, common_hdr_ptr->senders_major_version_num,
                 common_hdr_ptr->senders_minor_version_num);

        CDI_LOG_THREAD(kLogError, "%s", error_msg_str);
        // Queue endpoint manager to reset the EFA connection and notify the application that we are disconnected.
        ProbeControlEfaConnectionQueueReset(probe_ptr, error_msg_str);

        // Set new state to send reset.
        probe_ptr->tx_probe_state.tx_state = kProbeStateSendReset;
        *wait_timeout_ms_ptr = 0; // Take effect immediately.
        ret_new_state = true;
    }

    if (kEndpointDirectionSend == adapter_con_ptr->direction) {
        ret_new_state = ProbeTxControlProcessPacket(probe_ptr, common_hdr_ptr, wait_timeout_ms_ptr);
    } else {
        ret_new_state = ProbeRxControlProcessPacket(probe_ptr, common_hdr_ptr, wait_timeout_ms_ptr);
    }

    EfaConnectionState* efa_con_ptr = (EfaConnectionState*)adapter_con_ptr->type_specific_ptr;
    CdiAdapterFreeBuffer(ControlInterfaceGetEndpoint(efa_con_ptr->rx_control_handle), packet_sgl_ptr);

    return ret_new_state;
}

THREAD ProbeControlThread(void* ptr)
{
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)ptr;
    AdapterEndpointState* endpoint_ptr = probe_ptr->app_adapter_endpoint_handle;
    AdapterConnectionState* adapter_con_ptr = endpoint_ptr->adapter_con_state_ptr;

    CdiSignalType shutdown_signal = endpoint_ptr->shutdown_signal;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(probe_ptr->log_handle);

    uint64_t start_time_us = CdiOsGetMicroseconds();
    uint64_t wait_timeout_ms = 0; // Start trying immediately to establish a connection.

    while (!CdiOsSignalGet(shutdown_signal)) {
        // Wait for an incoming control command message to arrive, timeout or abort if we are shutting down.
        ControlCommand control_cmd;
        if (CdiFifoRead(probe_ptr->control_packet_fifo_handle, wait_timeout_ms, shutdown_signal, &control_cmd)) {
            if (kCommandTypeStateChange == control_cmd.command_type) {
                // Received a probe command directly from the local instance.
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe, "Probe stream ID[%d] process state[%s] change.",
                    EndpointManagerEndpointStreamIdGet(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle),
                    InternalUtilityKeyEnumToString(kKeyProbeState, control_cmd.probe_state));

                // Set probe state, depending on endpoint direction type.
                ProbeState* current_probe_state_ptr = (kEndpointDirectionSend == adapter_con_ptr->direction) ?
                                                      &probe_ptr->tx_probe_state.tx_state :
                                                      &probe_ptr->rx_probe_state.rx_state;
                *current_probe_state_ptr = control_cmd.probe_state;
                wait_timeout_ms = 0; // Set to zero so the state change is executed immediately in the code below.
            } else {
                // Received a control packet.
                if (ProbeControlProcessPacket(probe_ptr, &control_cmd.packet_sgl, &wait_timeout_ms)) {
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
            continue;
        }

        // Got timeout. Perform operation based on our current state.
        do {
            if (kEndpointDirectionSend == adapter_con_ptr->direction) {
                wait_timeout_ms = ProbeTxControlProcessProbeState(probe_ptr); // Transmitter
            } else {
                wait_timeout_ms = ProbeRxControlProcessProbeState(probe_ptr); // Receiver
            }
            // Stay in the loop, in case we need to process multiple states.
        } while (0 == wait_timeout_ms);

        // Processed a command, so reset the command start time to the current time.
        start_time_us = CdiOsGetMicroseconds();
    }

    CdiLoggerThreadLogUnset();
    return 0; // Return code not used.
}
