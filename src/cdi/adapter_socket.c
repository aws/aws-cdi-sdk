// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for the socket adapter.
*/

#include "adapter_api.h"

#include <sys/uio.h>

#include "cdi_os_api.h"
#include "internal.h"
#include "internal_log.h"
#include "private.h"
#include "protocol.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Ethernet frame size less MAC/IP/UDP headers.
#define kSocketMtu (1500 - 0x2a)

/// Forward declaration of function.
static CdiReturnStatus SocketConnectionCreate(AdapterConnectionHandle handle, int port_number,
                                              const char* bind_ip_addr_str);
/// Forward declaration of function.
static CdiReturnStatus SocketConnectionDestroy(AdapterConnectionHandle handle);
/// Forward declaration of function.
static CdiReturnStatus SocketEndpointOpen(AdapterEndpointHandle endpoint, const char* remote_address_str,
                                          int port_number, const char* bind_ip_addr_str);
/// Forward declaration of function.
static CdiReturnStatus SocketEndpointClose(AdapterEndpointHandle handle);
/// Forward declaration of function.
static CdiReturnStatus SocketEndpointSend(const AdapterEndpointHandle handle, const Packet* packet_ptr,
                                          bool flush_packets);
/// Forward declaration of function.
static CdiReturnStatus SocketEndpointRxBuffersFree(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr);
/// Forward declaration of function.
static CdiReturnStatus SocketEndpointGetPort(const AdapterEndpointHandle handle, int* ret_port_number_ptr);
/// Forward declaration of function.
static CdiReturnStatus SocketAdapterShutdown(CdiAdapterHandle adapter);

/**
 * Returns the adapter endpoint's transmit queue level which is always kEndpointTransmitQueueNa for this type.
 *
 * @param handle The handle of the adapter endpoint to query.
 *
 * @return kEndpointTransmitQueueNa always.
 */
static EndpointTransmitQueueLevel SocketGetTransmitQueueLevel(AdapterEndpointHandle handle)
{
    (void)handle;
    return kEndpointTransmitQueueNa;
}

/**
 * @brief Definition of memory space where rx data is placed.
 */
typedef struct {
    CdiSglEntry sgl_entry;  ///< SGL entry lent to connection layer to describe received packet.
    uint8_t buffer[kSocketMtu];  ///< Memory where received packet is placed and sent up to the connection layer.
} ReceiveBufferRecord;

/**
 * @brief State definition for socket endpoint.
 */
typedef struct {
    CdiSocket socket;  ///< OS specific implementation of a communications socket for sending or receving IP/UDP.
    int destination_port_number;  ///< Port number (for logging).
    CdiSignalType shutdown;  ///< This is set to cause the receive thread to exit.
    CdiThreadID receive_thread_id;  ///< The receive thread's id needed for joining.
    CdiPoolHandle receive_buffer_pool;  ///< Pool of ReceiveBufferRecords used for received packets.
} SocketEndpointState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/**
 * @brief Define the virtual table API interface for this adapter.
 */
static struct AdapterVirtualFunctionPtrTable socket_endpoint_functions = {
    .CreateConnection = SocketConnectionCreate,
    .DestroyConnection = SocketConnectionDestroy,
    .Open = SocketEndpointOpen,
    .Close = SocketEndpointClose,
    .Poll = NULL, // Not implemented
    .GetTransmitQueueLevel = SocketGetTransmitQueueLevel,
    .Send = SocketEndpointSend,
    .RxBuffersFree = SocketEndpointRxBuffersFree,
    .GetPort = SocketEndpointGetPort,
    .Reset = NULL, // Not implemented
    .Start = NULL, // Not implemented
    .Shutdown = SocketAdapterShutdown,
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Thread to receive packets over socket.
 *
 * @param arg Pointer to thread.
 * @return Return value not used.
 */
static CDI_THREAD SocketReceiveThread(void* arg)
{
    AdapterEndpointState* endpoint_state_ptr = (AdapterEndpointState*)arg;
    SocketEndpointState* private_state_ptr = (SocketEndpointState*)endpoint_state_ptr->type_specific_ptr;

    // Check whether the thread should be shut down.
    bool read_fail_logged = false;
    ReceiveBufferRecord* receive_buffer_ptr = NULL;
    while (!CdiOsSignalGet(private_state_ptr->shutdown)) {
        // Get a structure including the buffer memory to read into from the pool.
        if (receive_buffer_ptr != NULL || CdiPoolGet(private_state_ptr->receive_buffer_pool,
                                                     (void**)&receive_buffer_ptr)) {
            int byte_count = kSocketMtu;
            struct sockaddr_in source_address = { 0 };
            if (CdiOsSocketReadFrom(private_state_ptr->socket, receive_buffer_ptr->buffer, &byte_count,
                                    &source_address)) {
                if (byte_count > 0) {
                    receive_buffer_ptr->sgl_entry.size_in_bytes = byte_count;
                    // Connection may have set this last time it was used.
                    receive_buffer_ptr->sgl_entry.next_ptr = NULL;

                    Packet packet = {
                        .sg_list = {
                            .sgl_head_ptr = &receive_buffer_ptr->sgl_entry,
                            .sgl_tail_ptr = &receive_buffer_ptr->sgl_entry,
                            .total_data_size = byte_count,
                            .internal_data_ptr = NULL
                        },
                        .tx_state = {
                            .ack_status = kAdapterPacketStatusOk
                        }
                    };

                    // Set source address (sockaddr_in) in packet state.
                    packet.socket_adapter_state.address = source_address;
                    // Pass the received packet up to the associated connection for reassembly.
                    (endpoint_state_ptr->msg_from_endpoint_func_ptr)(endpoint_state_ptr->msg_from_endpoint_param_ptr,
                                                                     &packet, kEndpointMessageTypePacketReceived);
                    receive_buffer_ptr = NULL;  // That buffer is in use, force getting a new one from the pool.
                }
                if (read_fail_logged) {
                    CDI_LOG_THREAD(kLogInfo, "Reads recovered on port[%d].",
                                   private_state_ptr->destination_port_number);
                    read_fail_logged = false;
                }
            } else {
                // Read failed; try to handle this condition gracefully.
                if (!read_fail_logged) {
                    CDI_LOG_THREAD(kLogError, "Read on port[%d] failed.", private_state_ptr->destination_port_number);
                    read_fail_logged = true;
                    CdiOsSleep(10);  // Don't hog the CPU.
                }
            }
        } else {
            // Out of pool entries... wait a bit and try again.
            CdiOsSleep(1);
        }
    }

    // If we did not use the buffer, return it to the pool.
    if (NULL != receive_buffer_ptr) {
        CdiPoolPut(private_state_ptr->receive_buffer_pool, receive_buffer_ptr);
    }

    return 0;
}

/**
 * Initialization function for socket pool item.
 *
 * @param context_ptr Unused, reserve for future use.
 * @param item_ptr Pointer to item being initialized.
 * @return true always
 */
static bool SocketEndpointPoolItemInit(const void* context_ptr, void* item_ptr)
{
    (void)context_ptr;
    ReceiveBufferRecord* p = (ReceiveBufferRecord*)item_ptr;
    p->sgl_entry.address_ptr = p->buffer;
    return true;
}

static CdiReturnStatus SocketConnectionCreate(AdapterConnectionHandle handle, int port_number,
                                              const char* bind_ip_addr_str)
{
    CdiReturnStatus ret = kCdiStatusOk;
    (void)port_number;
    (void)bind_ip_addr_str;

    if (kEndpointDirectionSend == handle->direction &&
        0 == handle->adapter_state_ptr->adapter_data.tx_buffer_size_bytes) {
        SDK_LOG_GLOBAL(kLogError, "Payload transmit buffer size cannot be zero. Set tx_buffer_size_bytes when using"
                       " CdiCoreNetworkAdapterInitialize().");
        ret = kCdiStatusFatal;
    }

    return ret;
}

static CdiReturnStatus SocketConnectionDestroy(AdapterConnectionHandle handle)
{
    (void)handle;
    return kCdiStatusOk; // Nothing required here.
}

/**
 * Open a socket endpoint using the specified adapter.
 *
 * @param endpoint_handle Handle of adapter endpoint to open.
 * @param remote_address_str Pointer to remote target's IP address string.
 * @param port_number Destination port to use.
 * @param bind_address_str Pointer to optional bind IP address string.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 *
 */
static CdiReturnStatus SocketEndpointOpen(AdapterEndpointHandle endpoint_handle, const char* remote_address_str,
                                          int port_number, const char* bind_address_str)
{
    CdiReturnStatus ret = kCdiStatusOk;

    // Create an Internet socket which will be used for writing or reading.
    CdiSocket new_socket;
    if (CdiOsSocketOpen(remote_address_str, port_number, bind_address_str, &new_socket)) {
        // Allocate memory in which to store socket endpoint specific state.
        endpoint_handle->type_specific_ptr = (SocketEndpointState*)CdiOsMemAllocZero(sizeof(SocketEndpointState));
        SocketEndpointState* private_state_ptr = (SocketEndpointState*)endpoint_handle->type_specific_ptr;
        if (private_state_ptr == NULL) {
            ret = kCdiStatusNotEnoughMemory;
        } else {
            // Save the now open file descriptor for use inside of receive thread or transmit function.
            private_state_ptr->socket = new_socket;
            private_state_ptr->destination_port_number = port_number;

            if (endpoint_handle->adapter_con_state_ptr->direction == kEndpointDirectionReceive ||
                endpoint_handle->adapter_con_state_ptr->direction == kEndpointDirectionBidirectional) {
                bool pool_created = false;
                bool thread_created = false;

                // Create the receive thread shutdown signal.
                bool signal_created = CdiOsSignalCreate(&private_state_ptr->shutdown);
                if (!signal_created) {
                    CDI_LOG_THREAD(kLogError, "Failed to create socket receive thread shutdown signal.");
                } else {
                    // Create a pool of ReceiveBufferRecord structures.
                    pool_created = CdiPoolCreateAndInitItems("socket receiver", RX_SOCKET_BUFFER_SIZE,
                                                             RX_SOCKET_BUFFER_SIZE_GROW, MAX_POOL_GROW_COUNT,
                                                             sizeof(ReceiveBufferRecord), true,
                                                             &private_state_ptr->receive_buffer_pool,
                                                             SocketEndpointPoolItemInit, NULL);
                    if (!pool_created) {
                        CDI_LOG_THREAD(kLogError, "Failed to allocate socket receive buffer pool.");
                    }
                }
                if (pool_created) {
                    // Start the receive thread.
                    thread_created = CdiOsThreadCreate(SocketReceiveThread, &private_state_ptr->receive_thread_id,
                                                       "socket receiver", endpoint_handle, NULL);
                    if (!thread_created) {
                        CDI_LOG_THREAD(kLogError, "Failed to start socket receive thread.");
                    }
                }

                // Make sure that everything got created. If not, clean up and return error.
                if (!(signal_created && pool_created && thread_created)) {
                    CdiPoolDestroy(private_state_ptr->receive_buffer_pool); // Not set to NULL (freed below).
                    CdiOsSignalDelete(private_state_ptr->shutdown);
                    CdiOsSocketClose(new_socket);
                    ret = kCdiStatusAllocationFailed;
                }
            }
        }
    } else {
        CDI_LOG_HANDLE(endpoint_handle->adapter_con_state_ptr->log_handle, kLogError,
                       "Failed to open socket on Destination Port[%d].", port_number);
        ret = kCdiStatusOpenFailed;
    }

    if (kCdiStatusOk == ret && (endpoint_handle->adapter_con_state_ptr->direction == kEndpointDirectionSend ||
        endpoint_handle->adapter_con_state_ptr->direction == kEndpointDirectionBidirectional)) {
        // This small delay helps when using cdi_test to send to a receiver in the same invocation. No means of
        // synchronizing between the transmitting and receiving connections is available so delaying the transmitter
        // helps give the receiver a better chance of being ready before packets start flowing to it.
        CdiOsSleep(50);
    }

    if (kCdiStatusOk == ret) {
        CdiProtocolVersionNumber version = {
            .version_num = 1,
            .major_version_num = 0,
            .probe_version_num = 0
        };
        if (endpoint_handle->cdi_endpoint_handle) {
            EndpointManagerProtocolVersionSet(endpoint_handle->cdi_endpoint_handle, &version);
        } else {
            // The control interface does not have a cdi_endpoint_handle, so set the protocol version directly here.
            ProtocolVersionSet(&version, &endpoint_handle->protocol_handle);
        }

        endpoint_handle->connection_status_code = kCdiConnectionStatusConnected;

        if (endpoint_handle->adapter_con_state_ptr->data_state.connection_cb_ptr) {
            // Notify application that we are connected.
            CdiCoreConnectionCbData cb_data = {
                .status_code = kCdiConnectionStatusConnected,
                .err_msg_str = NULL,
                .connection_user_cb_param = endpoint_handle->adapter_con_state_ptr->data_state.connection_user_cb_param
            };
            (endpoint_handle->adapter_con_state_ptr->data_state.connection_cb_ptr)(&cb_data);
        }
    } else {
        // An error occurred, so free the private memory, if it was allocated.
        if (endpoint_handle->type_specific_ptr) {
            CdiOsMemFree(endpoint_handle->type_specific_ptr);
            endpoint_handle->type_specific_ptr = NULL;
        }
    }

    return ret;
}

/**
 * Closes the endpoint and frees any resources associated with it.
 *
 * @param endpoint_handle The handle of the endpoint to be closed.
 *
 * @return kCdiStatusFatal if shutting down the endpoint failed, otherwise kCdiStatusOk.
 */
static CdiReturnStatus SocketEndpointClose(AdapterEndpointHandle endpoint_handle)
{
    CdiReturnStatus ret = kCdiStatusOk;

    AdapterEndpointState* endpoint_state_ptr = (AdapterEndpointState*)endpoint_handle;
    SocketEndpointState* private_state_ptr = (SocketEndpointState*)endpoint_state_ptr->type_specific_ptr;

    // SocketEndpointOpen() ensures that the private state is fully formed else the pointer is NULL.
    if (private_state_ptr != NULL) {
        if (kEndpointDirectionReceive == endpoint_state_ptr->adapter_con_state_ptr->direction ||
            kEndpointDirectionBidirectional == endpoint_state_ptr->adapter_con_state_ptr->direction) {
            // Wait for receive thread to complete whatever it's doing.
            SdkThreadJoin(private_state_ptr->receive_thread_id, private_state_ptr->shutdown);
            private_state_ptr->receive_thread_id = NULL;

            // Since we are destroying this endpoint, ensure that all buffers within this pool are freed before
            // destroying them. NOTE: This pool only contains pool buffers (so nothing else needs to be freed).
            CdiPoolPutAll(private_state_ptr->receive_buffer_pool);
            CdiPoolDestroy(private_state_ptr->receive_buffer_pool); // Not setting to NULL (it is freed below).

            // Free the shutdown signal's resources.
            CdiOsSignalDelete(private_state_ptr->shutdown); // Not setting to NULL (it is freed below).
        }

        // Close the send or receive socket.
        CdiOsSocketClose(private_state_ptr->socket);

        // Free the socket endpoint specific state memory.
        CdiOsMemFree(private_state_ptr);
        endpoint_state_ptr->type_specific_ptr = NULL;
    }

    return ret;
}

/**
 * Sends a packet to the destination of the endpoint.
 *
 * @param handle The handle of the endpoint on which to send the packet.
 * @param packet_ptr A pointer to the packet data to be sent to the remote endpoint.
 * @param flush_packets true if this packet and any that might be queued to be sent should be sent immediately or false
 *                      if this packet can wait in the queue. The socket adapter does not queue packets so this argument
 *                      is ignored and always treated as true.
 *
 * @return CdiReturnStatus kCdiStatusOk if the packet was sent or kCdiStatusSendFailed if the writing to the
 *         socket failed.
 */
static CdiReturnStatus SocketEndpointSend(const AdapterEndpointHandle handle, const Packet* packet_ptr,
                                          bool flush_packets)
{
    (void)flush_packets; // Not used.
    CdiReturnStatus ret = kCdiStatusOk;
    SocketEndpointState* state_ptr = (SocketEndpointState*)handle->type_specific_ptr;

    // Convert SGL to iovec so only one call to the OS is made. The ensures that all of the data for this packet is
    // sent in a single packet on the media.
    struct iovec vectors[CDI_OS_SOCKET_MAX_IOVCNT];
    int iovcnt = 0;
    for (const CdiSglEntry* entry_ptr = packet_ptr->sg_list.sgl_head_ptr; entry_ptr != NULL;
            entry_ptr = entry_ptr->next_ptr) {
        if (iovcnt >= CDI_ARRAY_ELEMENT_COUNT(vectors)) {
            ret = kCdiStatusSendFailed;
            assert(false);
            break;
        } else {
            vectors[iovcnt].iov_base = entry_ptr->address_ptr;
            vectors[iovcnt].iov_len = entry_ptr->size_in_bytes;

            iovcnt++;
        }
    }

    if (kCdiStatusOk == ret) {
        int byte_count = 0;
        if (0 == packet_ptr->socket_adapter_state.address.sin_addr.s_addr) {
            if (!CdiOsSocketWrite(state_ptr->socket, vectors, iovcnt, &byte_count)) {
                ret = kCdiStatusSendFailed;
            }
        } else {
            if (!CdiOsSocketWriteTo(state_ptr->socket, vectors, iovcnt, &packet_ptr->socket_adapter_state.address,
                                    &byte_count)) {
                ret = kCdiStatusSendFailed;
            }
        }
    }

    // A copy of the data has been made so the application's buffer is available now. Send the message to the upper
    // layers.
    Packet rx_packet = *packet_ptr; // Make a copy of the packet, so we can modify ack_status.
    rx_packet.tx_state.ack_status = (kCdiStatusOk == ret) ? kAdapterPacketStatusOk : kAdapterPacketStatusNotConnected;

    (handle->msg_from_endpoint_func_ptr)(handle->msg_from_endpoint_param_ptr, &rx_packet,
                                         kEndpointMessageTypePacketSent);

    return ret;
}

/**
 * Returns the SGL entries contained in the supplied SGL to their free pool.
 *
 * @param handle The endpoint to which the SGL entries belong.
 * @param sgl_ptr Pointer to the SGL that contains the entries to be freed.
 *
 * @return CdiReturnStatus kCdiStatusOk always.
 */
static CdiReturnStatus SocketEndpointRxBuffersFree(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr)
{
    AdapterEndpointState* endpoint_state_ptr = (AdapterEndpointState*)handle;
    SocketEndpointState* private_state_ptr = (SocketEndpointState*)endpoint_state_ptr->type_specific_ptr;

    // Iterate through the SGL returning each ReceiveBufferRecord in it.
    CdiSglEntry* entry_ptr = sgl_ptr->sgl_head_ptr;
    while (entry_ptr) {
        ReceiveBufferRecord* receive_buffer_ptr = CONTAINER_OF(entry_ptr, ReceiveBufferRecord, sgl_entry);
        CdiSglEntry* next_ptr = entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
        CdiPoolPut(private_state_ptr->receive_buffer_pool, receive_buffer_ptr);
        entry_ptr = next_ptr;
    }

    return kCdiStatusOk;
}

/**
 * Returns the destination port number associated with the specified endpoint.
 *
 * @param handle The handle of the endpoint whose port number is of interest.
 * @param ret_port_number_ptr Address of the location where the port number is to be written.
 *
 * @return CdiReturnStatus kCdiStatusGetPortFailed if the port number could not be ascertained or
 *         kCdiStatusOk if the port number was written to the specified address.
 */
static CdiReturnStatus SocketEndpointGetPort(const AdapterEndpointHandle handle, int* ret_port_number_ptr)
{
    AdapterEndpointState* endpoint_state_ptr = (AdapterEndpointState*)handle;
    SocketEndpointState* private_state_ptr = (SocketEndpointState*)endpoint_state_ptr->type_specific_ptr;

    if (!CdiOsSocketGetPort(private_state_ptr->socket, ret_port_number_ptr)) {
        return kCdiStatusGetPortFailed;
    }
    return kCdiStatusOk;
}

/**
 * Shuts down the adapter, freeing any resources associated with it.
 *
 * @param adapter The handle of the adapter which is to be shut down.
 *
 * @return CdiReturnStatus kCdiStatusOk always.
 */
static CdiReturnStatus SocketAdapterShutdown(CdiAdapterHandle adapter)
{
    if (adapter != NULL) {
        CdiOsMemFree(adapter->adapter_data.ret_tx_buffer_ptr);
        adapter->adapter_data.ret_tx_buffer_ptr = NULL;
    }

    return kCdiStatusOk;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus SocketNetworkAdapterInitialize(CdiAdapterState* adapter_state_ptr)
{
    assert(adapter_state_ptr != NULL);

    CdiReturnStatus rs = kCdiStatusOk;

    // Allocate transmit buffers. For this adapter type, it can be regular memory.
    adapter_state_ptr->adapter_data.ret_tx_buffer_ptr =
        CdiOsMemAlloc(adapter_state_ptr->adapter_data.tx_buffer_size_bytes);
    if (NULL == adapter_state_ptr->adapter_data.ret_tx_buffer_ptr) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        // Set up the virtual function pointer table for this adapter type.
        adapter_state_ptr->functions_ptr = &socket_endpoint_functions;
        // Provide the number of bytes usable by the connection layer to the connection.
        adapter_state_ptr->maximum_payload_bytes = kSocketMtu;
        adapter_state_ptr->maximum_tx_sgl_entries = MAX_TX_SGL_PACKET_ENTRIES;
        adapter_state_ptr->msg_prefix_size = 0;
    } else {
        // Something bad happened--free any resources that were allocated in this function.
        if (adapter_state_ptr->adapter_data.ret_tx_buffer_ptr) {
            CdiOsMemFree(adapter_state_ptr->adapter_data.ret_tx_buffer_ptr);
            adapter_state_ptr->adapter_data.ret_tx_buffer_ptr = NULL;
        }
    }

    return rs;
}
