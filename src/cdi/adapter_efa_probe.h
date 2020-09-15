// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
    /// Use the control interface to send the kProbeCommandReset command to reset the remote connection. Must receive an
    /// ACK from the remote to confirm that it received the command.
    kProbeStateSendReset,

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
 * @brief This enumeration is used in the ProbePacketHeader structure to indicate a probe command.
 * NOTE: Any changes made here MUST also be made to "probe_command_key_array".
 */
typedef enum {
    kProbeCommandReset = 1, ///< Request to reset the connection.  Start with 1 so no commands have the value 0.
    kProbeCommandPing,      ///< Request to ping the connection.
    kProbeCommandConnected, ///< Notification that connection has been established (probe has completed).
    kProbeCommandAck,       ///< Packet is an ACK response to a previously sent command.
} ProbeCommand;

/**
 * @brief This defines a structure that contains all of the state information for the sending side of a single flow.
 * Its contents are opaque to the calling program.
 */
typedef struct {
    ProbeState tx_state;      ///< Current Tx probe state.
    /// @brief When in kProbeStateEfaConnectedPing state, this is the number of pings that have been sent without
    /// receiving an ack.
    int ping_retry_count;
} TxEndpointProbeState;

/**
 * @brief This defines a structure that contains all of the state information for the sending side of a single flow.
 * Its contents are opaque to the calling program.
 */
typedef struct {
    ProbeState rx_state;        ///< Current Rx probe state.
    int packets_received_count; ///< Number of probe packets that have been received.
    int pings_received_count;   ///< Number of pings that have been received.
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

    /// Memory pool of send control work requests (ProbeControlPacketWorkRequest).
    CdiPoolHandle control_work_request_pool_handle;

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
};

// --------------------------------------------------------------------
// All structures in the block below are byte packed (no byte padding).
// --------------------------------------------------------------------
#pragma pack(push, 1)

/**
 * @brief Common header for all probe control packets.
 */
typedef struct {
    uint8_t senders_version_num;       ///< Sender's CDI SDK version number.
    uint8_t senders_major_version_num; ///< Sender's CDI SDK major version number.
    uint8_t senders_minor_version_num; ///< Sender's CDI SDK minor version number.

    ProbeCommand command; ///< Sender's command
    char senders_ip_str[MAX_IP_STRING_LENGTH];   ///< Sender's IP address.
    char senders_gid_array[MAX_IPV6_GID_LENGTH]; ///< Sender's device GID. contains GID + QPN (see efa_ep_addr).
    char senders_stream_name_str[MAX_STREAM_NAME_STRING_LENGTH]; ///< Sender's stream name string.
    int senders_stream_identifier; ///< Sender's stream identifier.

    /// @brief Sender's control interface source port. Sent from Tx (client) to Rx (server) so the Rx can establish a
    /// transmit connection back to the Tx.
    uint16_t senders_control_dest_port;

    /// @brief Probe packet number that is incremented for each command sent. Value begins at zero when a new connection
    /// is establish and is only unique to the connection.
    uint16_t control_packet_num;
    uint16_t checksum; ///< The checksum for this control packet.
} ControlPacketCommonHeader;

/**
 * @brief Probe command packet that is being transmitted.
 */
typedef struct {
    ControlPacketCommonHeader common_hdr; ///< Common header of all probe packets.
    bool requires_ack; ///< A control flag that, when true, indicates the specified command requires ack.
} ControlPacketCommand;

/**
 * @brief Control ACK packet that is a response for a transmitted command.
 */
typedef struct {
    ControlPacketCommonHeader common_hdr; ///< Common header of all probe packets.
    ProbeCommand ack_command;           ///< Command that ACK corresponds to.
    uint16_t ack_control_packet_num;    ///< Command's control packet number that ACK corresponds to.
} ControlPacketAck;

/**
 * @brief Packet format used by probe when sending probe packets over the EFA interface.
 */
typedef struct {
    uint16_t packet_sequence_num; ///< Probe packet sequence number.
    uint8_t efa_data[EFA_PROBE_PACKET_DATA_SIZE]; ///< Probe packet data.
} EfaProbePacket;

/**
 * @brief Structure used to hold a union of packets that are transmitted over the control or EFA interface.
 */
typedef struct {
    union {
        ControlPacketCommand command_packet; ///< Command packet transmitted over the control interface.
        ControlPacketAck ack_packet;         ///< ACK packet transmitted over the control interface.
        EfaProbePacket efa_packet;           ///< Packet used for EFA probe transmitted over the EFA interface.
    };
} ProbePacketUnion;

#pragma pack(pop)
// --------------------------------------------------------------------
// End of byte packed structures (no byte padding).
// --------------------------------------------------------------------

/**
 * @brief Structure used to hold a transmit packet work request. The lifespan of a work request starts when a packet is
 * queued to be sent and ends when a message is received that is has either been successfully sent or a failure has
 * occurred.
 */
typedef struct {
    Packet packet;          ///< The top level packet structure for the data in this work request.
    CdiSglEntry sgl_entry;  ///< The single SGL entry for the probe packet (we only use 1 for all probe packets).
    ProbePacketUnion packet_data; ///< Data for the probe packet
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
        CdiSgList packet_sgl; ///< Valid if kCommandTypeRxPacket. Scatter-gather List for Rx packet.
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
 *
 * @return CdiReturnStatus kCdiStatusOk if the operation was successful or a value that indicates the nature of the
 *         failure.
 */
CdiReturnStatus ProbeEndpointResetDone(ProbeEndpointHandle handle);

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
 * Destroy a probe endpoint.
 *
 * @param handle Handle of the probe endpoint to destroy.
 */
void ProbeEndpointDestroy(ProbeEndpointHandle handle);

#endif  // CDI_ADAPTER_EFA_PROBE_H__
