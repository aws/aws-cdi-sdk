// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter_efa_probe.c.
 */

#ifndef CDI_ADAPTER_EFA_PROBE_H__
#define CDI_ADAPTER_EFA_PROBE_H__

#include "adapter_api.h"
#include "adapter_control_interface.h"
#include "private.h"
#include "protocol.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a probe connection. Each handle represents a
 * instance of a probe connection.
 */
/// @brief Forward declaration of structure to create pointers.
typedef struct ProbeEndpointState ProbeEndpointState;
/// @brief Forward declaration to create pointer to probe connection state when used.
typedef ProbeEndpointState* ProbeEndpointHandle;

/**
 * @brief This enumeration is used in the TxEndpointProbeState and RxEndpointProbeState structures to indicate the
 * current probe state for an application connection.
 * NOTE: Any changes made here MUST also be made to "probe_mode_key_array".
 */
typedef enum {
    /// Probe just started. Advance to kProbeStateSendReset.
    kProbeStateIdle,

    /// Use the control interface to send the kProbeCommandReset command to reset the remote connection. Must receive an
    /// ACK from the remote to confirm that it received the command.
    kProbeStateSendReset,

    /// After ACK from reset has been received by Tx, send protocol version to Rx.
    kProbeStateSendProtocolVersion,

    /// After the kProbeCommandReset command has been received, a request to reset the connection is sent to the
    /// Endpoint Manager. When the reset completes, probe state will advance to kProbeStateResetDone.
    kProbeStateResetting,

    /// The Endpoint Manager has finished resetting the connection. When the control interface has confirmed that the
    /// remote endpoint is connected, probe state will advance to kProbeStateWaitForStart.
    kProbeStateResetDone,

    /// The Endpont Manager has been sent a request to start the endpoint. When it completes, probe state is set to
    /// kProbeStateEfaStart.
    kProbeStateWaitForStart,

    /// Received notification from the Endpoint Manager that it is ok to start EFA connection. Probe state will advance
    /// to kProbeStateEfaProbe.
    kProbeStateEfaStart,

    /// Use the EFA interface to send enough probe packets to the remote so SRD can establish the initial network flows.
    /// When EFA probe completes, probe state will advance to kProbeStateEfaConnected.
    kProbeStateEfaProbe,

    /// The EFA Rx has received all the probe packets and now the TX is waiting to receive all the probe packet ACKs.
    kProbeStateEfaTxProbeAcks,

    /// The EFA connection is ready for use by the application. We will send an occasional "ping" using to the
    /// remote Rx (server) connection using the control interface to ensure the Rx's connection has not been
    /// reset and to ensure the control interface is working.
    kProbeStateEfaConnected,

    /// The EFA connection is ready and the transmitter has sent a ping. The transmitter is now waiting for an ACK in
    /// response. If the ACK is received within the expected timeout period, probe state will return to
    /// kProbeStateEfaConnected, otherwise it is changed to kProbeStateSendReset.
    kProbeStateEfaConnectedPing,

    /// Send a request to reset the connection to the Endpoint Manager and advance to the kProbeStateSendReset state.
    kProbeStateEfaReset,

    /// Endpoint is being destroyed.
    kProbeStateDestroy,
} ProbeState;

/**
 * @brief This defines a structure that contains all of the state information for the sending side of a single flow.
 */
typedef struct {
    ProbeState tx_state;      ///< Current Tx probe state.
    /// @brief When in kProbeStateEfaConnectedPing or kProbeStateSendProtocolVersion state, this is the number of
    /// consecutive commands that have been sent without receiving an ack.
    int send_command_retry_count;
    int packets_enqueued_count; ///< Number of probe packets that have been enqueued to send.
    int packets_acked_count; ///< Number of probe packets that have been acked.
    int packets_ack_wait_count; ///< Number of times have waited for probe packets ACKs to arrive.
} TxEndpointProbeState;

/**
 * @brief This defines a structure that contains all of the state information for the receiving side of a single flow.
 */
typedef struct {
    ProbeState rx_state;        ///< Current Rx probe state.
    /// @brief When in kProbeStateIdle or kProbeStateSendReset state, this is the number of consecutive reset commands
    /// that have been sent without receiving any commands back.
    int send_reset_retry_count;
    int packets_received_count; ///< Number of probe packets that have been received.
    uint32_t total_packet_count_snapshot; ///< Snapshot of total packet received count.
} RxEndpointProbeState;

typedef struct ProbeEndpointState ProbeEndpointState;

/**
 * @brief This defines a structure that contains all of the state information for the sending side of a single flow.
 * Its contents are opaque to the calling program.
 */
struct ProbeEndpointState {
    MessageFromEndpoint app_msg_from_endpoint_func_ptr; ///< Saved copy of original function pointer
    void* app_msg_from_endpoint_param_ptr; ///< Saved copy of original parameter

    AdapterEndpointHandle app_adapter_endpoint_handle; ///< Handle to the application's endpoint.
    union {
        /// The internal state of the structure if app_adapter_endpoint_handle.direction is kEndpointDirectionSend.
        TxEndpointProbeState tx_probe_state;
        /// The internal state of the structure if app_adapter_endpoint_handle.direction is kEndpointDirectionReceive.
        RxEndpointProbeState rx_probe_state;
    };

    CdiLogHandle log_handle; ///< Handle for the logging function.

    /// Data for worker thread used for ProbeThread().
    CdiThreadID probe_thread_id;   ///< Thread identifier

    /// Memory pool of send EFA work requests (ProbeEfaPacketWorkRequest).
    CdiPoolHandle efa_work_request_pool_handle;

    /// FIFO of control interface packet CdiSgList structures.
    CdiFifoHandle control_packet_fifo_handle;

    /// Packet number used for each packet on the control interface.
    uint16_t control_packet_num;

    CdiCsID ack_lock;                ///< Provides a critical section for all ack data below.
    bool ack_is_pending;             ///< A command is in progress that expects an ack.
    ProbeCommand ack_command;        ///< The command issued that needs an ack.
    /// @brief Packet number of the ack command being sent. NOTE: The size of this value must match the size of
    /// ack_control_packet_num in the structure ControlPacketAck to avoid wrapping problems when comparing the two
    /// values.
    uint16_t ack_control_packet_num;

    bool send_ack_command_valid;     ///< If true, the data below is valid.
    ProbeCommand send_ack_command;   ///< Command that needs to have an ACK returned.
    uint16_t send_ack_control_packet_num; ///< Packet number for returned ACK.
    uint8_t send_ack_probe_version; ///< Probe protocol version supported by ACK receiver.

    /// @brief Protocol handle of the current CDI-SDK version. See:
    /// CDI_PROTOCOL_VERSION.CDI_PROTOCOL_MAJOR_VERSION.CDI_PROBE_VERSION
    CdiProtocolHandle protocol_handle_sdk;
    CdiProtocolHandle protocol_handle_v1; ///< Protocol handle version 1.0.CDI_PROBE_VERSION
};

/**
 * @brief Structure used to hold a transmit packet work request. The lifespan of a work request starts when a packet is
 * queued to be sent and ends when a message is received that is has either been successfully sent or a failure has
 * occurred.
 */
typedef struct {
    Packet packet;          ///< The top level packet structure for the data in this work request.
    CdiSglEntry sgl_entry;  ///< The single SGL entry for the probe packet (we only use 1 for all probe packets).
    CdiRawProbeHeader packet_data; ///< Data for the probe packet
} ProbePacketWorkRequest;

/**
 * @brief This enumeration is used to define the type of command specified in the ControlCommand structure.
 */
typedef enum {
    kCommandTypeStateChange, ///< Command contains a value from the ProbeState enumeration.
    kCommandTypeRxPacket,    ///< Command contains a packet SGL that was received using the control interface.
} ControlCommandType;

/**
 * @brief Structure used to hold a control command.
 */
typedef struct {
    ControlCommandType command_type; ///< Determines which data in the union is valid.
    union {
        ProbeState probe_state; ///< Valid if kCommandTypeStateChange. Probe state to set.
        struct {
            CdiSgList packet_sgl; ///< Scatter-gather List for Rx packet.
            struct sockaddr_in source_address; ///< Source address of the received packet.
        } receive_packet; ///< Valid if kCommandTypeRxPacket.
    };
} ControlCommand;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of a new probe for the specifiec endpoint.
 *
 * @param app_adapter_endpoint_handle Application adapter endpoint handle.
 * @param log_handle Handle of log related to the specified endpoint.
 * @param ret_handle_ptr Pointer to returned probe endpoint handle.
 *
 * @return CdiReturnStatus kCdiStatusOk if the operation was successful or a value that indicates the nature of the
 *         failure.
 */
CdiReturnStatus ProbeEndpointCreate(AdapterEndpointHandle app_adapter_endpoint_handle,
                                    CdiLogHandle log_handle, ProbeEndpointHandle* ret_handle_ptr);

/**
 * Disables the EFA endpoint and puts the probe into connection reset state. The probe will then attempt to reset the
 * remote connection and re-establish the connection. Once the connection has been re-established, the user-registered
 * connection callback function will be invoked.
 *
 * @param handle Handle of probe related to the endpoint error.
 *
 * @return CdiReturnStatus kCdiStatusOk if the operation was successful or a value that indicates the nature of the
 *         failure.
 */
CdiReturnStatus ProbeEndpointError(ProbeEndpointHandle handle);

/**
 * Reset a probe endpoint.
 *
 * @param handle Handle of probe related to the endpoint.
 */
void ProbeEndpointReset(ProbeEndpointHandle handle);

/**
 * The Endpoint Manager is in the final process of completing a reset of the endpoint by calling
 * CdiAdapterResetEndpoint(), which uses this function to notify probe that the endpoint reset is done.
 *
 * @param handle Handle of probe related to the endpoint.
* @param reopen If true re-opens the endpoint, otherwise does not re-open it.
  *
 * @return CdiReturnStatus kCdiStatusOk if the operation was successful or a value that indicates the nature of the
 *         failure.
 */
CdiReturnStatus ProbeEndpointResetDone(ProbeEndpointHandle handle, bool reopen);

/**
 * The Endpoint Manager is in the final process of starting an endpoint by calling CdiAdapterStartEndpoint(), which uses
 * this function to notify probe that the endpoint can be started.
 *
 * @param handle Handle of probe related to the endpoint.
 *
 * @return CdiReturnStatus kCdiStatusOk if the operation was successful or a value that indicates the nature of the
 *         failure.
 */
CdiReturnStatus ProbeEndpointStart(ProbeEndpointHandle handle);

/**
 * Stop a probe endpoint and wait for its thread to exit.
 *
 * @param handle Handle of the probe endpoint to stop.
 */
void ProbeEndpointStop(ProbeEndpointHandle handle);

/**
 * Destroy a probe endpoint.
 *
 * @param handle Handle of the probe endpoint to destroy.
 */
void ProbeEndpointDestroy(ProbeEndpointHandle handle);

#endif  // CDI_ADAPTER_EFA_PROBE_H__
