// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter.c.
 */

#ifndef ADAPTER_API_H__
#define ADAPTER_API_H__

#include <stdbool.h>
#include <stdint.h>

#include "endpoint_manager.h"
#include "cdi_raw_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************


/// Forward reference
typedef struct ControlInterfaceState* ControlInterfaceHandle;

/**
 * @brief Values used for adapter packet acknowledgment status.
 */
typedef enum {
    kAdapterPacketStatusOk,           ///< The transmitted packet was acknowledged to have been received.
    kAdapterPacketStatusFailed,       ///< The packet transmission resulted in an error.
    kAdapterPacketStatusNotConnected, ///< The packet could not be sent because the adapter endpoint isn't connected.
} AdapterPacketAckStatus;

/**
 * Structure to represent a single packet that can also belong to a list of packets.
 */
typedef struct {
    CdiSinglyLinkedListEntry list_entry; ///< This member is needed for these to be members of a list.
    CdiSgList sg_list; ///< List of buffer fragments that comprise the packet's data.
    bool payload_last_packet; ///< True if last packet of a payload.
    union {
        struct TxState {
            AdapterPacketAckStatus ack_status; ///< Status of the packet.
        } tx_state;
    };

    union {
        /// @brief Data used by socket type adapters.
        struct SocketAdapterState {
            /// @brief Socket address. If packet is being transmitted, this contains the address of the sender,
            /// otherwise it contains the packet destination address.
            struct sockaddr_in address;
        } socket_adapter_state;
    };
} Packet;

/**
 * @brief The direction of packets that an endpoint will be used for.
 */
typedef enum {
    kEndpointDirectionSend,    ///< Endpoint can send packets to its remote host.
    kEndpointDirectionReceive, ///< Endpoint can receive packets from its remote host.
    kEndpointDirectionBidirectional, ///< Endpoint can send packets to and receive from the remote host.
} EndpointDirection;

/**
 * @brief The type of transmission data that an endpoint will be used for.
 */
typedef enum {
    kEndpointTypeData,    ///< Endpoint used for normal data transmission.
    kEndpointTypeControl, ///< Endpoint used for control command transmission (uses sockets).
} EndpointDataType;

/**
 * Enumeration of possible values that can be returned from the EfaGetTransmitQueueLevel() adapter API function.
 */
typedef enum {
    kEndpointTransmitQueueEmpty,        ///< Transmit queue has no packets in it.
    kEndpointTransmitQueueIntermediate, ///< Transmit queue is somewhere between empty and full.
    kEndpointTransmitQueueFull,         ///< Transmit queue has room for no more packets.
    kEndpointTransmitQueueNa,           ///< Endpoint is not a polled mode driver.
} EndpointTransmitQueueLevel;

/**
 * Enumeration of possible values used to specify the type of message generated from an endpoint using the
 * MessageFromEndpoint callback function.
 */
typedef enum {
    kEndpointMessageTypePacketSent, ///< Packet was sent.
    kEndpointMessageTypePacketReceived ///< Packet was received.
} EndpointMessageType;

/**
 * @brief Prototype of function used to process packet messages from the endpoint.
 *
 * @param param_ptr A pointer to data used by the function.
 * @param packet_ptr A pointer to packet state data.
 * @param message_type Endpoint message type.
 */
typedef void (*MessageFromEndpoint)(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

/**
 * @brief Structure used to hold adapter endpoint state.
 */
struct AdapterEndpointState {
    CdiListEntry list_entry;              ///< Allow these structures to live in a list in the adapter connection.

    CdiEndpointHandle cdi_endpoint_handle; ///< Handle of CDI endpoint associated with this adapter endpoint.

    AdapterConnectionState* adapter_con_state_ptr; ///< Pointer back to the adapter connection used by this endpoint.

    /// @brief Address of function used to queue packet messages from the endpoint.
    MessageFromEndpoint msg_from_endpoint_func_ptr;
    void* msg_from_endpoint_param_ptr;    ///< Parameter passed to queue message function.

    /// @brief Current state of this endpoint. NOTE: Made volatile, since it is written to and read by different
    /// threads. The reader uses the value within a loop, so we don't want the value to be cached and held in a
    /// register.
    volatile CdiConnectionStatus connection_status_code;

    CdiQueueHandle tx_packet_queue_handle; ///< Circular queue of TxPacketState structures.
    CdiSinglyLinkedList tx_packet_waiting_list; ///< List of packets from packet queue waiting to be sent.

    /// @brief Number of Tx packets that are in process (sent but haven't received ACK/error response).
    volatile uint32_t tx_packets_in_process;

    CdiAdapterEndpointStats* endpoint_stats_ptr; ///< Address where to store adapter endpoint statistics.

    /// @brief Signal used to start adapter endpoint threads. A separate signal is used for the connection (see
    /// AdapterConnectionState.start_signal).
    CdiSignalType start_signal;
    CdiSignalType shutdown_signal; ///< Signal used to shutdown adapter endpoint threads.

    CdiProtocolHandle protocol_handle; ///< Handle of protocol being used. Value is NULL if none configured.

    /// @brief Number of Tx payloads/packets in flight. When a payload is being queued to transmit, the count is
    /// incremented by one. As each packet within a payload is being queued, the value is also incremented by 1. After
    /// each packet has been ACKed, this value is decremented by 1. In addition, when the last packet of a payload has
    /// been ACKed the value is decremented by 1. This is done to ensure that the value remains non-zero until all the
    /// packets of a payload have been ACKed.
    uint32_t tx_in_flight_ref_count;

    void* type_specific_ptr; ///< Adapter specific endpoint data.
};

/// Forward declaration to create pointer to adapter endpoint state when used.
typedef struct AdapterEndpointState* AdapterEndpointHandle;

/**
 * @brief This defines a structure that contains the state information for an rx adapter connection.
 */
typedef struct {
    /// @brief Number of packet buffers to reserve for incoming payloads.
    int reserve_packet_buffers;
} RxAdapterConnectionState;

/**
 * @brief Values used to determine current running state of a poll thread.
 *
 */
typedef enum {
    kPollStart, ///< Poll thread is starting and has not ran through a pool loop yet.
    kPollRunning, ///< Poll thread has ran through at least one poll loop.
    kPollStopping, ///< Poll thread is stopping.
    kPollStopped ///< Poll thread stopped.
} PollState;

/**
 * @brief This defines a structure that contains the state information for a data adapter connection (type is
 * kEndpointTypeData). This adapter type is used to transmit all data that is not part of a control interface.
 */
typedef struct {
    CdiConnectionHandle cdi_connection_handle; ///< A handle to the CDI connection for this connection.

    CdiCoreConnectionCallback connection_cb_ptr; ///< Connection callback pointer.
    CdiUserCbParameter connection_user_cb_param; ///< User data for connection callback pointer.
} AdapterDataConnectionState;

/**
 * @brief This defines a structure that contains the state information for a control interface adapter connection (type
 * is kEndpointTypeControl).
 */
typedef struct {
    AdapterEndpointHandle control_endpoint_handle; ///< Control interface endpoint handle.
} AdapterControlConnectionState;

/**
 * @brief This defines a structure that contains the state information for a single instance of a poll thread.
 */
typedef struct {
    CdiListEntry list_entry; ///< Allow these structures to live in a list in the Adapter.

    CdiThreadID thread_id; ///< thread ID used by both Tx/Rx endpoints.

    int shared_thread_id; ///< User defined shared poll thread identifier.

    /// @brief The core to dedicate to this hardware poll thread. The value is the 0-based CPU number or -1 to
    /// disable pinning the thread to a specific core.
    int thread_core_num;

    EndpointDataType data_type; ///< The type of endpoint data this poll thread supports.

    /// @brief True if connection requires polling, otherwise connection does not require polling. NOTE: Connections
    /// that share the same poll thread must either be all polling or non-polling (cannot have a mix of both types).
    bool is_poll;

    bool only_transmit; ///< True if all endpoints using this poll thread only transmit.

    CdiSignalType connection_list_changed_signal; ///< Signal set when connection_list has been changed.
    CdiSignalType connection_list_processed_signal; ///< Signal set when connection_list has been processed.

    /// @brief Lock used to protect access to connection_list.
    CdiCsID connection_list_lock;

    CdiList connection_list; ///< List of connections (AdapterConnectionState*) used by this poll thread.

    /// @brief Signal used to start poll thread. A separate signal is used for endpoints (see
    /// AdapterEndpointState.start_signal).
    CdiSignalType start_signal;
} PollThreadState;

/**
 * @brief Type used to hold thread utilization state data.
 */
typedef struct {
    uint64_t top_time;          ///< Time at start of each poll loop.
    uint64_t busy_accumulator;  ///< Number of productive microseconds accumulated over an averaging period.
    uint64_t idle_accumulator;  ///< Number of idle microseconds accumulated over an averaging period.
    uint64_t start_time;        ///< Time to use for start of each averaging period.
} ThreadUtilizationState;

/**
 * @brief Structure used to hold adapter connection state.
 */
struct AdapterConnectionState {
    CdiListEntry list_entry;              ///< Allow these structures to live in a list in the Adapter.
    CdiAdapterState* adapter_state_ptr;   ///< Pointer back to the adapter that is being used by this connection.

    /// @brief Current state of this endpoint. NOTE: Made volatile, since it is written to and read by different
    /// threads. The reader uses the value within a loop, so we don't want the value to be cached and held in a
    /// register.
    volatile CdiConnectionStatus connection_status_code;

    /// @brief Handle (a pointer) to the log associated with the connection used by this endpoint.
    CdiLogHandle log_handle;

    EndpointDirection direction;          ///< The direction that this endpoint supports.
    RxAdapterConnectionState rx_state;    ///< Valid if direction supports receive.

    union {
        /// @brief Valid if PollThreadState.data_type = kEndpointTypeData. Data connection specific state data.
        AdapterDataConnectionState data_state;
        /// @brief Valid if PollThreadState.data_type = kEndpointTypeControl. Control interface connection specific
        /// state data.
        AdapterControlConnectionState control_state;
    };

    PollState poll_state; ///< State of poll operation for this adapter connection (start, running or stopped).

    bool can_transmit; ///< True if connection can transmit.
    bool can_receive; ///< True if connection can receive.

    /// @brief Used for computing the CPU utilization of the poll thread for the connection.
    ThreadUtilizationState load_state;

    int port_number; ///< Port number related to this connection.

    /// @brief Valid if direction supports transmit. Tx Signal/flag used to notify PollThread() that it can sleep. This
    /// signal is set whenever a Tx payload transaction begins. Probe also sets the signal in
    /// ProbeControlEfaConnectionStart(). It is cleared after all the Tx packets for the payload have been sent, ACKs
    /// received and the Tx payload queue is empty. See PollThread(). It also is cleared whenever an adapter endpoint is
    /// reset. See CdiAdapterResetEndpoint().
    CdiSignalType tx_poll_do_work_signal;

    PollThreadState* poll_thread_state_ptr; ///< Pointer to poll thread state data associated with this connection.

    CdiSignalType shutdown_signal; ///< Signal used to shutdown adapter connection threads.

    ControlInterfaceHandle control_interface_handle; ///< Handle of control interface for the connection.

    CdiCsID endpoint_lock; ///< Lock used to protect access to endpoint resources.

    void* type_specific_ptr;       ///< Adapter specific connection data.
};

/// Forward declaration to create pointer to adapter connection state when used.
typedef struct AdapterConnectionState* AdapterConnectionHandle;

/**
 * @brief Structure definition used to define the virtual table API interface for adapters.
 */
struct AdapterVirtualFunctionPtrTable {
    /// @brief Create a new connection.
    /// @see CdiAdapterCreateConnection
    CdiReturnStatus (*CreateConnection)(AdapterConnectionHandle handle, int port_number);

    /// @brief Destroy an open connection.
    /// @see CdiAdapterDestroyConnection
    CdiReturnStatus (*DestroyConnection)(AdapterConnectionHandle handle);

    /// @brief Opens a new endpoint. For send type (transmit) endpoints, a pointer to the remote_address_str must
    /// be provided, otherwise the value must be NULL. etc.). @see CdiAdapterOpenEndpoint
    CdiReturnStatus (*Open)(AdapterEndpointHandle handle, const char* remote_address_str, int port_number);

    /// @brief Closes an open endpoint.
    /// @see CdiAdapterCloseEndpoint
    CdiReturnStatus (*Close)(AdapterEndpointHandle handle);

    /// @brief Perform poll on an endpoint.
    /// @see CdiAdapterPollEndpoint
    CdiReturnStatus (*Poll)(AdapterEndpointHandle handle);

    /// @brief Checks transmit queue level. A polled mode endpoint will return kEndpointTransmitBufferFull when its
    /// transmit queue is full while awaiting acknowledgements. Non-polled endpoints always return
    /// kEndpointTransmitBufferNa.
    /// @see CdiAdapterGetTransmitQueueLevel
    EndpointTransmitQueueLevel (*GetTransmitQueueLevel)(AdapterEndpointHandle handle);

    /// @brief Sends the data in memory specified by the SGL to the endpoint.
    /// @see CdiAdapterEnqueueSendPacket
    CdiReturnStatus (*Send)(const AdapterEndpointHandle handle, const Packet* packet_ptr, bool flush_packets);

    /// @brief Returns a receive data buffer to the endpoint's free pool.
    /// @see CdiAdapterFreeBuffer
    CdiReturnStatus (*RxBuffersFree)(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr);

    /// @brief Returns the port number being used by the endpoint.
    /// @see CdiAdapterGetPort
    CdiReturnStatus (*GetPort)(const AdapterEndpointHandle handle, int* ret_port_number_ptr);

    /// @brief Reset an open endpoint.
    /// @see CdiAdapterResetEndpoint
    CdiReturnStatus (*Reset)(AdapterEndpointHandle handle, bool reopen);

    /// @brief Start an open endpoint.
    /// @see CdiAdapterStartEndpoint
    CdiReturnStatus (*Start)(AdapterEndpointHandle handle);

    /// @brief Shuts down an adapter, freeing all of its associated resources.
    /// @see CdiAdapterShutdown
    CdiReturnStatus (*Shutdown)(CdiAdapterHandle adapter);
};

/**
 * @brief Structure definition behind the handles shared with the user's application program. Its contents are opaque
 * to the user's program where it only has a pointer to a declared but not defined structure.
 */
struct CdiAdapterState {
    /// Used to store an instance of this object in a list using this element as the list item.
    CdiListEntry list_entry;

    /// Set to kMagicAdapter when allocated, checked at every API function to help ensure validity.
    uint32_t magic;

    /// @brief Lock used to protect access to adapter state data.
    CdiCsID adapter_lock;

    /// @brief Copy of the adapter's IP address string. "adapter_data.adapter_ip_addr_str" (see below) points to this
    /// value.
    char adapter_ip_addr_str[MAX_IP_STRING_LENGTH];

    /// Adapter configuration data.
    CdiAdapterData adapter_data;

    /// @brief Pointer to the table of adapter specific operation functions.
    struct AdapterVirtualFunctionPtrTable* functions_ptr;

    /// @brief Lock used to protect access to connection_list.
    CdiCsID connection_list_lock;

    /// @brief List of connections using this adapter.
    CdiList connection_list;

    /// @brief List of poll threads using this adapter (holds PollThreadState*). NOTE: Must acquire adapter_lock before
    /// using.
    CdiList poll_thread_list;

    /// @brief Data specific to the type of underlying adapter.
    void* type_specific_ptr;

    /// @brief The maximum number of bytes that can be sent in a packet through this adapter. The number is computed by
    /// subtracting the number of bytes required for transmitting a packet through the medium supported by the adapter
    /// from the maximum number of bytes in a packet on the medium. In other words, this is the maximum number of bytes
    /// that the scatter-gather list provided to CdiAdapterSendPacket() can contain.
    int maximum_payload_bytes;

    /// @brief The maximum number of SGL entries that can be used to represent a single Tx packet of data.
    int maximum_tx_sgl_entries;

    /// @brief The size of any required message prefix buffer space that an application must provide in front of all
    /// message send and receive buffers for use by the provider. The contents of the prefix space should
    /// be treated as opaque. This will be zero for providers that don't support prefix mode.
    int msg_prefix_size;

    /// @brief If true, tx_buffer_ptr is using hugepages, otherwise it is using heap memory.
    bool tx_buffer_is_hugepages;

    /// @brief Size in bytes of Tx payload buffer allocated. Pointer to buffer is in CdiAdapterData.ret_tx_buffer_ptr.
    /// NOTE: The allocation may be larger than requested, due to rounding.
    uint64_t tx_buffer_allocated_size;
};

/**
 * @brief Structure definition used to configure an adapter endpoint.
 */
typedef struct {
    CdiAdapterHandle cdi_adapter_handle; ///< A handle to the adapter using this connection.
    CdiConnectionHandle cdi_connection_handle; ///< A handle to the CDI connection.
    EndpointManagerHandle endpoint_manager_handle; ///< A handle to the Endpoint Manager for this connection.

    /// @brief Address of connection callback function.
    CdiCoreConnectionCallback connection_cb_ptr;

    /// @brief User defined connection callback parameter.
    CdiUserCbParameter connection_user_cb_param;

    /// @brief A handle to the log to use for this endpoint.
    CdiLogHandle log_handle;

    /// @brief Indicates whether the endpoint will be used for sending or for receiving packets.
    EndpointDirection direction;

    /// @brief For send type endpoints, the port number on the remote host to connect to. For receive type endpoints,
    /// the port number on the local host to listen to.
    int port_number;

    /// @brief Identifier of poll thread to associate with this connection. Specify -1 to create a unique poll thread
    /// only used by this connection.
    int shared_thread_id;

    /// @brief The core to dedicate to this connection's hardware poll thread. The value is the 0-based CPU number or -1
    /// to disable pinning the thread to a specific core.
    int thread_core_num;

    /// @brief Valid if direction = kEndpointDirectionReceive or kEndpointDirectionBidirectional.
    RxAdapterConnectionState rx_state;

    /// @brief Indicates the type of transmission data this endpoint supports.
    EndpointDataType data_type;
} CdiAdapterConnectionConfigData;

/**
 * @brief Structure definition used to configure an adapter endpoint.
 */
typedef struct {
    AdapterConnectionHandle connection_handle; ///< A handle to the adapter connection related to this endpoint.
    CdiEndpointHandle cdi_endpoint_handle; ///< Handle of CDI endpoint associated with this adapter endpoint.

    /// @brief Address of function used to queue messages from this endpoint.
    MessageFromEndpoint msg_from_endpoint_func_ptr;
    void* msg_from_endpoint_param_ptr; ///< Pointer to parameter passed to queue message function.

    /// @brief Address where to write adapter endpoint statistics.
    CdiAdapterEndpointStats* endpoint_stats_ptr;

    /// @brief A string representing a remote host's IP address in dotted decimal format. Only applies to send type
    /// endpoints.
    const char* remote_address_str;

    /// @brief For send type endpoints, the port number on the remote host to connect to. For receive type endpoints,
    /// the port number on the local host to listen to.
    int port_number;
} CdiAdapterEndpointConfigData;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initializes an EFA adapter specified by the values in the provided CdiAdapterState structure.
 *
 * @param adapter_state_ptr The address of the generic adapter state preinitialized with the generic values including
 *                          the CdiAdapterData structure which contains the values provided to the SDK by the user
 *                          program.
 * @param is_socket_based Specifies whether the adapter is socket-based (true) or EFA-based (false).
 *
 * @return CdiReturnStatus kCdiStausOk if successful, otherwise a value indicating the nature of failure.
 */
CdiReturnStatus EfaNetworkAdapterInitialize(CdiAdapterState* adapter_state_ptr, bool is_socket_based);

/**
 * Initializes socket based adapter specified by the values in the provided CdiAdapterState structure.
 *
 * @param adapter_state_ptr The address of the generic adapter state preinitialized with the generic values including
 *                          the CdiAdapterData structure which contains the values provided to the SDK by the user
 *                          program.
 *
 * @return CdiReturnStatus kCdiStausOk if successful, otherwise a value indicating the nature of failure.
 */
CdiReturnStatus SocketNetworkAdapterInitialize(CdiAdapterState* adapter_state_ptr);

/**
 * Create an adapter connection. An endpoint is a one-way communications channel on which packets can
 * be sent to or received from a remote host whose address and port number are specified here.
 *
 * NOTE: This API only creates resources used by the endpoint. The CdiAdapterStartEndpoint() function must be used to
 * start it.
 *
 * @param config_data_ptr Pointer to adapter endpoint config data.
 * @param return_handle_ptr The address of a location to have the newly opened endpoint's handle written.
 *
 * @return CdiReturnStatus kCdiStatusOk upon successful open otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterCreateConnection(CdiAdapterConnectionConfigData* config_data_ptr,
                                           AdapterConnectionHandle* return_handle_ptr);

/**
 * Stop a connection, shutting down thread resources.
 *
 * @param handle The handle of the connection which is to be stopped.
 *
 * @return CdiReturnStatus kCdiStatusOk upon success otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterStopConnection(AdapterConnectionHandle handle);

/**
 * Destroy a connnection closing related open endpoints, freeing their resources.
 *
 * @param handle The handle of the connection which is to be closed.
 *
 * @return CdiReturnStatus kCdiStatusOk upon success otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterDestroyConnection(AdapterConnectionHandle handle);

/**
 * Opens an endpoint for the specified connection. An endpoint is a one-way communications channel on which packets can
 * be sent to or received from a remote host whose address and port number are specified here.
 *
 * NOTE: This function only creates resources used by the endpoint. The CdiAdapterStartEndpoint() function must be
 * used to start it.
 *
 * @param config_data_ptr Pointer to adapter endpoint config data.
 * @param return_handle_ptr The address of a location to have the newly opened endpoint's handle written.
 *
 * @return CdiReturnStatus kCdiStatusOk upon successful open otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterOpenEndpoint(CdiAdapterEndpointConfigData* config_data_ptr,
                                       AdapterEndpointHandle* return_handle_ptr);

/**
 * Starts an endpoint for the specified connection.
 *
 * @param handle The handle of the endpoint to started.
 *
 * @return CdiReturnStatus kCdiStatusOk upon successful open otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterStartEndpoint(AdapterEndpointHandle handle);

/**
 * Reset an endpoint and free its resources.
 *
 * @param handle The handle of the endpoint to reset.
 * @param reopen If true the endpoint is re-opened after resetting it, otherwise just reset it.
 *
 * @return CdiReturnStatus kCdiStatusOk upon success otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterResetEndpoint(AdapterEndpointHandle handle, bool reopen);

/**
 * Close an endpoint and free its resources.
 *
 * @param handle The handle of the endpoint which is to be closed.
 *
 * @return CdiReturnStatus kCdiStatusOk upon success otherwise an indication of the failure.
 */
CdiReturnStatus CdiAdapterCloseEndpoint(AdapterEndpointHandle handle);

/**
 * While a connection is open, this API function must be called on a regular basis to perform poll mode processing
 * without having to create additional adapter worker threads.
 *
 * @param handle The handle of the endpoint to poll.
 *
 * @return either kCdiStatusInternalIdle or kCdiStatusOk if successful, otherwise a value that indicates the nature of
 *         the failure is returned. kCdiStatusInternalIdle means that the function performed no productive work while
 *         kCdiStatusOk says that did.
 */
CdiReturnStatus CdiAdapterPollEndpoint(AdapterEndpointHandle handle);

/**
 * Checks transmitter queue level. A polled mode endpoint will return kEndpointTransmitBufferFull when its transmit
 * queue is full while awaiting acknowledgements. Non-polled endpoints always return kEndpointTransmitBufferNa.
 *
 * @param handle The handle of the endpoint for which to check its transmit queue level.
 *
 * @return EndpointTransmitQueueLevel a value indicating the current fullness of the transmit queue.
 */
EndpointTransmitQueueLevel CdiAdapterGetTransmitQueueLevel(AdapterEndpointHandle handle);

/**
 * Add a single packet to the send packet queue. The packet will be sent to the remote host for the specified endpoint.
 * Only valid for endpoints that were open for sending.
 * NOTE: This function is called by the main payload thread TxPayloadThread() as well as by EFA probe control.
 * MEMORY NOTE: The Packet structure is not copied, it is merely referenced. Its storage must come from a pool.
 *
 * @param handle The handle of the endpoint through which to send the packet.
 * @param destination_address_ptr Pointer to destination address data (sockaddr_in).
 * @param packet_ptr The address of the packet to enqueue for sending to the connected endpoint.
 *
 * @return CdiReturnStatus kCdiStatusOk if the packet was successfully queued for sending or a value
 *         that indicates the nature of the failure.
 */
CdiReturnStatus CdiAdapterEnqueueSendPacket(const AdapterEndpointHandle handle,
                                            const struct sockaddr_in* destination_address_ptr, Packet* packet_ptr);

/**
 * Add a list of packets to the send packet queue. The packets will be sent to the remote host for the specified
 * endpoint. Only valid for endpoints that were open for sending.
 * NOTE: This function is called by the main payload thread TxPayloadThread() as well as by EFA probe control.
 * MEMORY NOTE: While the packet_list_ptr's contents are copied, the Packet structures referenced by it are not copied.
 * Their storage must come from a pool.
 *
 * @param handle The handle of the endpoint through which to send the packet.
 * @param packet_list_ptr The address of the list of packets to enqueue for sending to the connected endpoint.
 *
 * @return CdiReturnStatus kCdiStatusOk if the packet was successfully queued for sending or kCdiStatusQueueFull if the
 *         packet could not be queued on account of the queue being full.
 */
CdiReturnStatus CdiAdapterEnqueueSendPackets(const AdapterEndpointHandle handle,
                                             const CdiSinglyLinkedList *packet_list_ptr);

/**
 * Free a buffer that was provided by the endpoint in a received packet through the queue function. The resources
 * returned to the endpoint include the SGList structure, all of the SGL entries in the list, and the memory buffers
 * addressed by the entries.
 *
 * @param handle The handle of the endpoint from which the packet was received.
 * @param sgl_ptr The SGL describing the resources to be returned to the endpoint.
 *
 * @return CdiReturnStatus kCdiStatusOk if the packet buffer memory was successfully freed or a value
 *         that indicates the nature of the failure.
 */
CdiReturnStatus CdiAdapterFreeBuffer(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr);

/**
 * Gets the number of the port to which the specified endpoint is using.
 *
 * @param handle The handle of the endpoint to get the port number from.
 * @param port_number_ptr Address of where the port number will be written.
 *
 * @return CdiReturnStatus kCdiStatusOk if the packet buffer memory was successfully freed or a value
 *         that indicates the nature of the failure.
 */
CdiReturnStatus CdiAdapterGetPort(const AdapterEndpointHandle handle, int* port_number_ptr);

/**
 * Shut down the adapter and free all of the resources associated with it. The caller must not use the adapter's handle
 * for any purpose after this function returns.
 *
 * @param adapter A handle to the adapter to close.
 *
 * @return CdiReturnStatus kCdiStatusOk if the close operation was successfully or a value that
 *         indicates the nature of the failure.
 */
CdiReturnStatus CdiAdapterShutdown(CdiAdapterHandle adapter);

/**
 * Flush resources associate with PollThread(). NOTE: This function should only be called after the thread has
 * been paused using EndpointManagerThreadWait().
 *
 * @param handle The handle of the endpoint to flush resources.
 */
void CdiAdapterPollThreadFlushResources(AdapterEndpointHandle handle);

/**
 * Tx packet has ACKed.
 *
 * @param handle The handle of the endpoint that a Tx packet is related to.
 * @param packet_ptr Pointer to packet.
 */
void CdiAdapterTxPacketComplete(AdapterEndpointHandle handle, const Packet* packet_ptr);

#endif // ADAPTER_API_H__
