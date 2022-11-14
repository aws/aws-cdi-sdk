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

// Page for Connection Probe.
/*!
 * @page probe_main_page Connection Probe Architecture
 * @tableofcontents
 *
 * @section probe_overview Architecture Overview
 *
 * In order to establish an SRD connection between two EC2 instances using EFA adapters, a specific sequence of events
 * must occur. The EC2 instance used as a transmitter must obtain an EFA device identifier of the remote EC2 instance in
 * order to establish the EFA connection. Initial startup and optimization of the SRD network flows need to be
 * established before the EFA connection can be used by the application. For this, a socket based interface is used to
 * control communication. All requests contain information about the sender such as #CdiProtocolVersionNumber, IP
 * address, port and EFA device identifier. #CdiProtocolVersionNumber is used to negotiate compatible probe and CDI-SDK
 * protocols. In order to support legacy protocols, the negotiation process involves multiple steps and is described
 * below:
 *
 * 1. Create the socket based control interface. The instances start in #kProbeStateIdle and then advance to
 *    #kProbeStateSendReset.
 *
 * 2. Transmitter sends reset requests that contain protocol v1.0.x to receiver until an ACK is received. This value is
 *    used to allow backwards compatibility with legacy versions of the protocols. For legacy versions of the probe
 *    protocol, the value of "x" is ignored. For all other versions:
 *
 *      If the value of "x" (defined by CDI_PROBE_VERSION) is 3 or greater, then the probe protocol supports the new
 *      #kProbeCommandProtocolVersion command. Otherwise, the legacy protocol v1.0.x is used.
 *
 * 3. Once the receiver has received the reset request, non-legacy versions of the probe protocol examine the
 *    transmitter's CDI_PROBE_VERSION. If the value is 3 or greater than the ACK response will contain the receiver's
 *    CDI-SDK and probe version and wait for the transmitter to send the new #kProbeCommandProtocolVersion command.
 *    Otherwise, the receiver sets its negotiated protocol to legacy v1.0.x, which will be used for all future probe
 *    communication until another reset command is received or the connection is lost. In either case it advances the
 *    state to #kProbeStateEfaReset, using the Endpoint Manager to reset the local connection. While this is occurring,
 *    the state is set to #kProbeStateResetting. When complete, the state is set to #kProbeStateResetDone, which causes
 *    the ACK to be sent back to the transmitter. State then advances to #kProbeStateEfaProbe, which is used to transmit
 *    several SRD packets over the EFA interface to establish the initial network flows.
 *
 * 4. Once the transmitter has received the ACK for a reset request, for non-legacy probe protocols the receiver's probe
 *    protocol version is evaluated as described below:
*
 *      If the probe protocol version is 3 or greater, then the state advances to #kProbeStateSendProtocolVersion and
 *      the #kProbeCommandProtocolVersion command is sent to the receiver. After the transmitter receives the ACK for
 *      #kProbeCommandProtocolVersion, it sets its negotiated CDI-SDK and probe protocol version. Otherwise, the
 *      transmitter sets its negotiated protocol to legacy v1.0.x.
 *
 * 5. After the transmitter's negotiated protocols have been set, the transmitter then uses the Endpoint Manager to
 *    prepare the EFA connection so it can be started. While this is occurring, the state is set to
 *    #kProbeStateWaitForStart. When complete, the state is set to #kProbeStateEfaStart, which causes the connection to
 *    be started and begins transmitting SRD probe packets over the EFA interface. State is set to #kProbeStateEfaProbe.
 *
 * 6. After the desired number of SRD probe packets have been successfully transmitted and confirmed as being received
 *    by the receiver, the receiver will advance its state to #kProbeStateEfaConnected, invoke the user registered
 *    callback function CdiCoreConnectionCallback(), and send #kProbeCommandConnected to the transmitter. After the
 *    transmitter receives the command, it advances the state to #kProbeStateEfaConnected and the user registered
 *    callback function CdiCoreConnectionCallback() is invoked.
 *
 * 7. While connected, the transmitter will send #kProbeCommandPing commands using the control interface to the receiver
 *    to ensure both transmitter and receiver are operating correctly. This is done at a regular interval
 *    (#SEND_PING_COMMAND_FREQUENCY_MSEC). If the transmitter does not receive an ACK back within a timeout period
 *    (#TX_COMMAND_ACK_TIMEOUT_MSEC), a few more attempts are made. If these attempts fail, the transmitter disables the
 *    EFA connection and returns to #kProbeStateSendReset state.
 *
 * NOTE: The user registered callback function CdiCoreConnectionCallback() is invoked whenever the connection state
 * changes (#kCdiConnectionStatusConnected or #kCdiConnectionStatusDisconnected).
 *
 * The diagram shown below provides an overview of the connection probe architecture.
 * @image html "probe_high_level_architecture.jpg"
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h" // Must include this file first due to #define _GNU_SOURCE
#include "adapter_efa_probe.h"

#include "adapter_api.h"
#include "adapter_efa_probe_control.h"
#include "adapter_efa_probe_rx.h"
#include "adapter_efa_probe_tx.h"
#include "internal.h"
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

CdiReturnStatus ProbeEndpointCreate(AdapterEndpointHandle app_adapter_endpoint_handle,
                                    CdiLogHandle log_handle, ProbeEndpointHandle* ret_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    ProbeEndpointState* probe_ptr = CdiOsMemAllocZero(sizeof(ProbeEndpointState));
    if (probe_ptr == NULL) {
        rs = kCdiStatusAllocationFailed;
    }

    if (kCdiStatusOk == rs) {
        // Save data used by the probe.
        probe_ptr->app_adapter_endpoint_handle = app_adapter_endpoint_handle;

        probe_ptr->app_msg_from_endpoint_func_ptr = app_adapter_endpoint_handle->msg_from_endpoint_func_ptr;
        probe_ptr->app_msg_from_endpoint_param_ptr = app_adapter_endpoint_handle->msg_from_endpoint_param_ptr;

        probe_ptr->log_handle = log_handle;
    }

    if (kCdiStatusOk == rs) {
        // Create instance of the protocol used by current version of the SDK.
        CdiProtocolVersionNumber version = {
            .version_num = CDI_PROTOCOL_VERSION,
            .major_version_num = CDI_PROTOCOL_MAJOR_VERSION,
            .probe_version_num = CDI_PROBE_VERSION
        };
        ProtocolVersionSet(&version, &probe_ptr->protocol_handle_sdk);

        // Create instance of the protocol compatible with version 1.
        ProtocolVersionSetLegacy(&probe_ptr->protocol_handle_v1);
    }

    if (kCdiStatusOk == rs) {
        // Create receive control command queue. This FIFO is used by the control interface's receiver (see
        // rx_control_endpoint_handle), which uses ProbeRxControlMessageFromEndpoint() to write to the FIFO. So,
        // the FIFO must be created first.
        if (!CdiFifoCreate("Receive ControlCommand FIFO", MAX_PROBE_CONTROL_COMMANDS_PER_CONNECTION,
                        sizeof(ControlCommand), NULL, NULL, &probe_ptr->control_packet_fifo_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create critical section.
        if (!CdiOsCritSectionCreate(&probe_ptr->ack_lock)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // ProbePacketWorkRequests are used for sending the probe packets which go through the EFA.
        if (!CdiPoolCreate("Send EFA ProbePacketWorkRequest Pool", EFA_PROBE_PACKET_COUNT,
                           NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(ProbePacketWorkRequest), true, // true= Make thread-safe
                           &probe_ptr->efa_work_request_pool_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Start the thread which will service items from the queue.
        if (!CdiOsThreadCreate(ProbeControlThread, &probe_ptr->probe_thread_id, "EfaProbe", probe_ptr,
                               app_adapter_endpoint_handle->start_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk != rs) {
        ProbeEndpointDestroy((ProbeEndpointHandle)probe_ptr);
        probe_ptr = NULL;
    }

    *ret_handle_ptr = (ProbeEndpointHandle)probe_ptr;

    return rs;
}

CdiReturnStatus ProbeEndpointError(ProbeEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;
    if (probe_ptr && kCdiConnectionStatusConnected == probe_ptr->app_adapter_endpoint_handle->connection_status_code) {
        // Notify the application of the connection state change.
        EndpointManagerConnectionStateChange(probe_ptr->app_adapter_endpoint_handle->cdi_endpoint_handle,
                                             kCdiConnectionStatusDisconnected, NULL);
        // Post control command to change to EFA reset mode. This will cause the EFA connection to be reset, change the
        // endpoint's connection state to kCdiConnectionStatusDisconnected and then change to the kProbeStateSendReset
        // state.
        ProbeControlQueueStateChange(probe_ptr, kProbeStateEfaReset);
    }

    return rs;
}

CdiReturnStatus ProbeEndpointResetDone(ProbeEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;

    if (probe_ptr) {
        // NOTE: Receiver will be started after protocol version has been negotiated. This prevents in-flight packet
        // acks from erroneously being received from a previously established connection. In this case, in
        // rxr_cq_insert_addr_from_rts() the packet type can be RXR_CONNACK_PKT instead of RXR_RTS_PKT. Transmitter must
        // wait until we have the remote GID before it can start.

        // Post control command to notify probe that resetting the connection has completed.
        ProbeControlQueueStateChange(probe_ptr, kProbeStateResetDone);
    }

    return rs;
}

CdiReturnStatus ProbeEndpointStart(ProbeEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;
    AdapterConnectionState* adapter_con_ptr = probe_ptr->app_adapter_endpoint_handle->adapter_con_state_ptr;

    // Post control command to notify probe that it can start the EFA endpoint.
    if (probe_ptr) {
        if (kEndpointDirectionSend == adapter_con_ptr->direction) {
            if (kProbeStateWaitForStart == probe_ptr->tx_probe_state.tx_state) {
                ProbeControlQueueStateChange(probe_ptr, kProbeStateEfaStart);
            }
        } else {
            if (kProbeStateWaitForStart == probe_ptr->rx_probe_state.rx_state) {
                ProbeControlQueueStateChange(probe_ptr, kProbeStateEfaStart);
            }
        }
    }

    return rs;
}

void ProbeEndpointReset(ProbeEndpointHandle handle)
{
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;
    CdiPoolPutAll(probe_ptr->efa_work_request_pool_handle);
}

void ProbeEndpointStop(ProbeEndpointHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;
    if (probe_ptr) {
        // Clean-up thread resources. We will wait for it to exit using thread join.
        SdkThreadJoin(probe_ptr->probe_thread_id, probe_ptr->app_adapter_endpoint_handle->shutdown_signal);
        probe_ptr->probe_thread_id = NULL;
    }
}

void ProbeEndpointDestroy(ProbeEndpointHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    ProbeEndpointState* probe_ptr = (ProbeEndpointState*)handle;
    if (probe_ptr) {
        if (probe_ptr->probe_thread_id) {
            // Clean-up thread resources, waiting for the thread to exit.
            ProbeEndpointStop(probe_ptr);
        }

        // Now that the thread has stopped, it is safe to clean up the remaining resources. Since we are destroying this
        // connection, ensure that all buffers within these pools are freed and FIFOs emptied before destroying them.

        // NOTE: The SGL resources used in this FIFO don't need to be freed.
        CdiFifoFlush(probe_ptr->control_packet_fifo_handle);
        CdiFifoDestroy(probe_ptr->control_packet_fifo_handle);
        probe_ptr->control_packet_fifo_handle = NULL;

        CdiOsCritSectionDelete(probe_ptr->ack_lock);
        probe_ptr->ack_lock = NULL;

        // NOTE: The SGL entries in this pool are stored within the pool buffer, so no additional resource freeing needs
        // to be done here.
        CdiPoolPutAll(probe_ptr->efa_work_request_pool_handle);
        CdiPoolDestroy(probe_ptr->efa_work_request_pool_handle);
        probe_ptr->efa_work_request_pool_handle = NULL;

        ProtocolVersionDestroy(probe_ptr->protocol_handle_sdk);
        probe_ptr->protocol_handle_sdk = NULL;

        ProtocolVersionDestroy(probe_ptr->protocol_handle_v1);
        probe_ptr->protocol_handle_v1 = NULL;

        CdiOsMemFree(probe_ptr);
    }
}
