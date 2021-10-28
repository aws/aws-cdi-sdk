// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for the EFA Rx adapter.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h"

#include "endpoint_manager.h"
#include "internal_log.h"
#include "internal_tx.h"
#include "private.h"
#include "cdi_os_api.h"

#include "rdma/fi_domain.h"
#include "rdma/fi_endpoint.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Align each receive buffer to start at an address evenly divisible by 8.
static const int packet_buffer_alignment = 8;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Posts a Rx data buffer to the receive queue of the corresponding endpoint. Posted receives are searched in the order
 * in which they were posted in order to match sends. Message boundaries are maintained. The order in which the receives
 * complete is dependent on the endpoint type and protocol.
 *
 * @param endpoint_state_ptr Pointer to endpoint state data.
 * @param msg_iov_ptr An iovec structure with the address and size of the packet buffer to give to libfabric for use as
 *                    a receive packet buffer.
 * @param more_to_post Set this to true if this function will be immediately called again to post another packet buffer.
 *                     This allows libfabric to process packet buffers in an optimized fashion.
 *
 * @return Returns true if no error, otherwise false is returned.
 */
static bool PostRxBuffer(EfaEndpointState* endpoint_state_ptr, const struct iovec* msg_iov_ptr, bool more_to_post)
{
    void *desc = fi_mr_desc(endpoint_state_ptr->rx_state.memory_region_ptr);
    struct fi_msg msg = {
        .desc = &desc,
        .msg_iov = msg_iov_ptr,
        .iov_count = 1,
        .addr = FI_ADDR_UNSPEC,
        .context = NULL, // Currently not used
        .data = 0
    };

    const uint64_t flags = FI_RECV | (more_to_post ? FI_MORE : 0);
    const int max_num_tries = 5;
    int num_tries = 0;
    ssize_t fi_ret = 0;
    do {
        fi_ret = fi_recvmsg(endpoint_state_ptr->endpoint_ptr, &msg, flags);
        if (0 == fi_ret || -FI_EAGAIN != fi_ret) {
            break;
        }
    } while (++num_tries != max_num_tries);

    if (0 != fi_ret) {
        CDI_LOG_THREAD(kLogError, "Got [%ld (%s)] from fi_recvmsg(), tried [%d] times.",
            fi_ret, fi_strerror(-fi_ret), num_tries);
    }

    return 0 == fi_ret;
}

/**
 * Used to poll for any pending Rx completion events and process them.
 *
 * @param efa_endpoint_ptr Pointer to EFA endpoint state data.
 *
 * @return true if useful work was done, false if the function did nothing productive.
 */
static bool Poll(EfaEndpointState* efa_endpoint_ptr)
{
    AdapterEndpointState* aep_ptr = efa_endpoint_ptr->adapter_endpoint_ptr;
    const size_t msg_prefix_size = aep_ptr->adapter_con_state_ptr->adapter_state_ptr->msg_prefix_size;
    const int maximum_payload_bytes = aep_ptr->adapter_con_state_ptr->adapter_state_ptr->maximum_payload_bytes;

    struct fi_cq_data_entry comp_array[MAX_RX_BULK_COMPLETION_QUEUE_MESSAGES];
    int fi_ret = fi_cq_read(efa_endpoint_ptr->completion_queue_ptr, &comp_array,
                            MAX_RX_BULK_COMPLETION_QUEUE_MESSAGES);
    // If the returned value is greater than zero, then the value is the number of completion queue messages that
    // were returned in comp_array. If zero is returned, completion queue was empty. Otherwise a negative value
    // represents an error or -FI_EAGAIN.

    // In message prefix mode some messages may not contain application data but are for the provider only. Keep track
    // of how many such messages we receive.
    int num_provider_messages = 0;
    if (fi_ret > 0) {
        for (int i = 0; i < fi_ret; i++) {
            const size_t message_length = comp_array[i].len;

            // Note: We have not seen this code path taken, so it is untested and possibly incorrect. The EFA provider
            // probably does not sent provider-only messages, which means this code is superfluous.
            if (message_length <= msg_prefix_size) {
                num_provider_messages += 1;
                if (0 == message_length) {
                    CDI_LOG_THREAD(kLogWarning, "Unexpected zero-size message from fi_cq_read (buffer [%p]); skipping.",
                        comp_array[i].buf);
                } else {
                    CDI_LOG_THREAD(kLogInfo, "Skipping small message of length: %zu", message_length);

                    // This message is meant just for the provider (prefix mode) because there is no data beyond the
                    // prefix section. There is nothing to process, so we immediately return the buffer to libfabric.
                    struct iovec msg_iov = {
                        .iov_len = maximum_payload_bytes + msg_prefix_size,
                        .iov_base = comp_array[i].buf
                    };

                    if (!PostRxBuffer(efa_endpoint_ptr, &msg_iov, false)) {
                        // Something went terribly wrong in libfabric. Notify the probe component so it can start the
                        // connection reset process.
                        ProbeEndpointError(efa_endpoint_ptr->probe_endpoint_handle);
                    }
                }
                continue;
            }

            CdiSglEntry* sgl_entry_ptr = NULL;
            // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
            if (!CdiPoolGet(efa_endpoint_ptr->rx_state.packet_sgl_entries_pool_handle, (void**)&sgl_entry_ptr)) {
                assert(false);
            }

            Packet packet = {
                .sg_list = {
                    .sgl_head_ptr = sgl_entry_ptr,
                    .sgl_tail_ptr = sgl_entry_ptr,
                    .total_data_size = message_length - msg_prefix_size,
                    .internal_data_ptr = NULL,
                },
                .tx_state = {
                    .ack_status = kAdapterPacketStatusOk
                }
            };

            if (sgl_entry_ptr) {
                sgl_entry_ptr->address_ptr = (char*)comp_array[i].buf + msg_prefix_size;
                sgl_entry_ptr->size_in_bytes = message_length - msg_prefix_size;
                sgl_entry_ptr->internal_data_ptr = NULL;
                sgl_entry_ptr->next_ptr = NULL;
            }

#ifdef DEBUG_PACKET_SEQUENCES
            CdiProtocolHandle protocol_handle = efa_endpoint_ptr->adapter_endpoint_ptr->protocol_handle;
            CdiDecodedPacketHeader decoded_header = { 0 };
            ProtocolPayloadHeaderDecode(protocol_handle, sgl_entry_ptr->address_ptr, sgl_entry_ptr->size_in_bytes,
                                        &decoded_header);
            CDI_LOG_THREAD(kLogInfo, "CQ T[%d] P[%d] S[%d] A[%p]", decoded_header.payload_type,
                           decoded_header.payload_num, decoded_header.packet_sequence_num, sgl_entry_ptr->address_ptr);
#endif

            // Send the completion message for the packet.
            (aep_ptr->msg_from_endpoint_func_ptr)(aep_ptr->msg_from_endpoint_param_ptr, &packet,
                                                  kEndpointMessageTypePacketReceived);

            // NOTE: Instead of using PostRxBuffer() here to make a new Rx buffer available to libfabric, we will do
            // it after the packet's buffer has been freed. See EfaRxEndpointRxBuffersFree(). This can be done
            // because used PostRxBuffer() for all the Rx buffers when the endpoint was created in
            // EfaRxEndpointOpen().
        }
    } else if (fi_ret < 0 && fi_ret != -FI_EAGAIN) {
        CDI_LOG_THREAD(kLogError, "Got[%d (%s)] from fi_cq_read().", fi_ret, fi_strerror(-fi_ret));
    }
    return fi_ret > num_provider_messages;
}

/**
 * Allocates a hunk of memory, registers it with libfabric, and posts packet sized portions of the allocation as receive
 * buffers.
 *
 * @param endpoint_state_ptr Pointer to endpoint.
 * @param packet_size The size of each packet.
 * @param packet_count How many packets to allocate.
 *
 * @return On success returns 0 on failure returns codes in accordance with libfabric rdma/fi_errno.h.
 */
static bool CreatePacketPool(EfaEndpointState* endpoint_state_ptr, int packet_size, int packet_count)
{
    bool ret = false;

    // Ensure buffer was properly freed before allocating a new one. See FreePacketPool().
    assert(NULL == endpoint_state_ptr->rx_state.allocated_buffer_ptr);

    const int aligned_packet_size = (packet_size + packet_buffer_alignment - 1) & ~(packet_buffer_alignment - 1);

    // Huge pages are not guaranteed to be aligned at all. Add enough padding to be able to shift the starting address
    // to an aligned location.
    int allocated_size = aligned_packet_size * packet_count + packet_buffer_alignment;

    // Round up to next even-multiple of hugepages byte size.
    allocated_size = ((allocated_size + CDI_HUGE_PAGES_BYTE_SIZE-1) / CDI_HUGE_PAGES_BYTE_SIZE) * CDI_HUGE_PAGES_BYTE_SIZE;

    uint8_t* allocated_ptr = CdiOsMemAllocHugePage(allocated_size);
    if (NULL == allocated_ptr) {
        // Fallback using heap memory.
        allocated_ptr = CdiOsMemAlloc(allocated_size);
        endpoint_state_ptr->rx_state.allocated_buffer_was_from_heap = true;
    } else {
        // Buffer was allocated using huge pages. Set flag to know how to later free it.
        endpoint_state_ptr->rx_state.allocated_buffer_was_from_heap = false;
    }

    if (NULL != allocated_ptr) {
        // Move the address pointer up to the next aligned position.
        uint8_t* mem_ptr = (uint8_t*)(((uint64_t)(allocated_ptr + packet_buffer_alignment - 1))
                                      & ~(packet_buffer_alignment - 1));

        // Register the newly allocated and aligned region with libfabric.
        int fi_ret = fi_mr_reg(endpoint_state_ptr->domain_ptr, mem_ptr, aligned_packet_size * packet_count,
                           FI_RECV, 0, 0, 0,
                           &endpoint_state_ptr->rx_state.memory_region_ptr, NULL);
        if (0 == fi_ret) {
            // Give fragments of allocated memory to libfabric for receiving packet data into.
            struct iovec msg_iov = {
                .iov_len = packet_size
            };

            ret = true;
            for (int i = 0; ret && i < packet_count; i++) {
                msg_iov.iov_base = mem_ptr;
                if (!PostRxBuffer(endpoint_state_ptr, &msg_iov, (i + 1 != packet_count))) {
                    ret = false;
                }
                mem_ptr += aligned_packet_size;
            }
        } else {
            CDI_LOG_THREAD(kLogError, "Libfabric failed to register allocated aligned memory [%d (%s)]. This could be "
                           "caused by insufficient ulimit locked memory.", fi_ret, fi_strerror(-fi_ret));
        }
    }

    if (ret) {
        endpoint_state_ptr->rx_state.allocated_buffer_ptr = allocated_ptr;
        endpoint_state_ptr->rx_state.allocated_buffer_size = allocated_size;
    } else {
        if (NULL != allocated_ptr) {
            if (endpoint_state_ptr->rx_state.allocated_buffer_was_from_heap) {
                CdiOsMemFree(allocated_ptr);
            } else {
                CdiOsMemFreeHugePage(allocated_ptr, allocated_size);
            }
        }
    }

    return ret;
}

/**
 * Frees the previously allocated receive packet buffer pool for the endpoint.
 *
 * @param endpoint_state_ptr Pointer to the endpoint whose receive packet buffer pool is to be freed.
 */
static void FreePacketPool(EfaEndpointState* endpoint_state_ptr)
{
    if (NULL != endpoint_state_ptr->rx_state.allocated_buffer_ptr) {
        // Unregister the region from libfabric.
        int rs = fi_close(&endpoint_state_ptr->rx_state.memory_region_ptr->fid);
        if (0 != rs) {
            CDI_LOG_THREAD(kLogError, "Got[%d (%s)] from fi_flose().", rs, fi_strerror(-rs));
        }

        if (endpoint_state_ptr->rx_state.allocated_buffer_was_from_heap) {
            CdiOsMemFree(endpoint_state_ptr->rx_state.allocated_buffer_ptr);
        } else {
            CdiOsMemFreeHugePage(endpoint_state_ptr->rx_state.allocated_buffer_ptr,
                                 endpoint_state_ptr->rx_state.allocated_buffer_size);
        }
        endpoint_state_ptr->rx_state.allocated_buffer_ptr = NULL;
        endpoint_state_ptr->rx_state.allocated_buffer_size = 0;
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus EfaRxEndpointOpen(EfaEndpointState* endpoint_state_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    int reserve_packets = endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->rx_state.reserve_packet_buffers;

    if (kCdiStatusOk == rs) {
        // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
        if (!CdiPoolCreate("EfaRxEndpoint CdiSglEntry Pool", reserve_packets,
                           MAX_RX_PACKETS_PER_CONNECTION_GROW, MAX_POOL_GROW_COUNT, sizeof(CdiSglEntry),
                           false, // false= Not thread-safe (don't use OS resource locks)
                           &endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        rs = EfaAdapterProbeEndpointCreate(endpoint_state_ptr, &endpoint_state_ptr->probe_endpoint_handle);
    }

    if (kCdiStatusOk != rs) {
        EfaRxEndpointClose(endpoint_state_ptr);
    }

    return rs;
}

CdiReturnStatus EfaRxEndpointPoll(EfaEndpointState* endpoint_state_ptr)
{
    return Poll(endpoint_state_ptr) ? kCdiStatusOk : kCdiStatusInternalIdle;
}

CdiReturnStatus EfaRxEndpointReset(EfaEndpointState* endpoint_state_ptr)
{
    // Clean up resources used by PollThread().

    // This pool only contains pool buffers (so nothing else needs to be freed).
    // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
    CdiPoolPutAll(endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle);
    ProbeEndpointReset(endpoint_state_ptr->probe_endpoint_handle);

    EfaConnectionState* efa_con_ptr = (EfaConnectionState*)endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->type_specific_ptr;

    // If Tx control handle exists, flush its adapter Tx queue.
    if (efa_con_ptr->control_interface_handle) {
        AdapterEndpointHandle control_handle = ControlInterfaceGetEndpoint(efa_con_ptr->control_interface_handle);
        CdiQueueFlush(control_handle->tx_packet_queue_handle);
    }

    return kCdiStatusOk;
}

CdiReturnStatus EfaRxEndpointClose(EfaEndpointState* endpoint_state_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    // Stop the probe endpoint (stops its thread) before freeing probe related resources.
    ProbeEndpointStop(endpoint_state_ptr->probe_endpoint_handle); // Ensure probe thread is stopped.

    ProbeEndpointDestroy(endpoint_state_ptr->probe_endpoint_handle);
    endpoint_state_ptr->probe_endpoint_handle = NULL;

    // NOTE: This pool is not thread-safe, so  must ensure that only one thread is accessing it at a time.
    CdiPoolDestroy(endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle);
    endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle = NULL;

    return kCdiStatusOk;
}

CdiReturnStatus EfaRxEndpointRxBuffersFree(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr)
{
    AdapterEndpointState* adapter_endpoint_ptr = (AdapterEndpointState*)handle;
    EfaEndpointState* endpoint_state_ptr = (EfaEndpointState*)adapter_endpoint_ptr->type_specific_ptr;
    CdiReturnStatus rs = kCdiStatusOk;

    const size_t msg_prefix_size = adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->msg_prefix_size;
    struct iovec msg_iov = {
        .iov_len = adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->maximum_payload_bytes +
                   msg_prefix_size
    };

    // Free SGL data buffers and SGL entries.
    CdiSglEntry *sgl_entry_ptr = sgl_ptr->sgl_head_ptr;
    while (sgl_entry_ptr) {
        // Don't need to free resources if not connected, since all libfabric resources get reset whenever the
        // connection is lost.
        if (kCdiConnectionStatusConnected == handle->connection_status_code) {
            msg_iov.iov_base = (char*)sgl_entry_ptr->address_ptr - msg_prefix_size;

            // NOTE: This function is called from PollThread(), so no need to use libfabric's FI_THREAD_SAFE option.
            // Access to libfabric functions such as fi_recvmsg() and fi_cq_read() use PollThread().
            if (!PostRxBuffer(endpoint_state_ptr, &msg_iov, NULL != sgl_entry_ptr->next_ptr)) {
                // Something went terribly wrong in libfabric. Notify the probe component so it can start the connection
                // reset process.
                ProbeEndpointError(endpoint_state_ptr->probe_endpoint_handle);
                rs = kCdiStatusNotConnected;
            }
        }
        // Free SGL entry buffer.
        CdiSglEntry *next_ptr = sgl_entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
        // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
        CdiPoolPut(endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle, sgl_entry_ptr);
        sgl_entry_ptr = next_ptr; // Point to next SGL entry
    }

    return rs;
}

CdiReturnStatus EfaRxPacketPoolCreate(EfaEndpointState* endpoint_state_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    int reserve_packets =
        endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->rx_state.reserve_packet_buffers;
    int max_payload_size =
        endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->maximum_payload_bytes;
    size_t msg_prefix_size =
        endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->msg_prefix_size;
    if (!CreatePacketPool(endpoint_state_ptr, max_payload_size + msg_prefix_size, reserve_packets)) {
        rs = kCdiStatusNotEnoughMemory;
    }

    return rs;
}

void EfaRxPacketPoolFree(EfaEndpointState* endpoint_state_ptr)
{
    FreePacketPool(endpoint_state_ptr);
}
