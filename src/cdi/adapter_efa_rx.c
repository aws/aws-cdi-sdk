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
    bool ret = true;

    struct fi_msg msg = {
        .desc = fi_mr_desc(endpoint_state_ptr->rx_state.memory_region_ptr),
        .msg_iov = msg_iov_ptr,
        .iov_count = 1,
        .addr = FI_ADDR_UNSPEC,
        .context = NULL, // Currently not used
        .data = 0
    };

    const uint64_t flags = FI_RECV | (more_to_post ? FI_MORE : 0);

    const ssize_t fi_ret = fi_recvmsg(endpoint_state_ptr->endpoint_ptr, &msg, flags);
    if (-FI_EAGAIN == fi_ret) {
        CDI_LOG_THREAD(kLogError, "Got -FI_EAGAIN from fi_recvmsg(). This is not expected.");
        ret = false; // Return an error.
    } else if (0 != fi_ret) {
        CDI_LOG_THREAD(kLogError, "Got[%d] from fi_recvmsg().", fi_ret);
        ret = false; // Return an error.
    }

    return ret;
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
    bool ret = false;
    struct fi_cq_data_entry comp_array[MAX_RX_BULK_COMPLETION_QUEUE_MESSAGES];
    int fi_ret = fi_cq_read(efa_endpoint_ptr->completion_queue_ptr, &comp_array,
                            MAX_RX_BULK_COMPLETION_QUEUE_MESSAGES);
    // If the returned value is greater than zero, then the value is the number of completion queue messages that
    // were returned in comp_array. If zero is returned, completion queue was empty. Otherwise a negative value
    // represents an error or -FI_EAGAIN.
    if (fi_ret > 0) {
        ret = true;
        for (int i = 0; i < fi_ret; i++) {
            CdiSglEntry* sgl_entry_ptr = NULL;
            // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
            if (!CdiPoolGet(efa_endpoint_ptr->rx_state.packet_sgl_entries_pool_handle, (void**)&sgl_entry_ptr)) {
                assert(false);
            }

            Packet packet = {
                .sg_list = {
                    .sgl_head_ptr = sgl_entry_ptr,
                    .sgl_tail_ptr = sgl_entry_ptr,
                    .total_data_size = comp_array[i].len,
                    .internal_data_ptr = NULL,
                },
                .tx_state = {
                    .ack_status = kAdapterPacketStatusOk
                }
            };

            if (sgl_entry_ptr) {
                sgl_entry_ptr->address_ptr = comp_array[i].buf;
                sgl_entry_ptr->size_in_bytes = comp_array[i].len;
                sgl_entry_ptr->internal_data_ptr = NULL;
                sgl_entry_ptr->next_ptr = NULL;
            }

#ifdef DEBUG_PACKET_SEQUENCES
            CdiCDIPacketCommonHeader* common_hdr_ptr = (CdiCDIPacketCommonHeader*)comp_array[i].buf;
            CDI_LOG_THREAD(kLogInfo, "CQ T[%d] P[%d] S[%d] A[%p]", common_hdr_ptr->payload_type,
                            common_hdr_ptr->payload_num, common_hdr_ptr->packet_sequence_num, comp_array[i].buf);
#endif

            // Send the completion message for the packet.
            AdapterEndpointState* aep_ptr = efa_endpoint_ptr->adapter_endpoint_ptr;
            (aep_ptr->msg_from_endpoint_func_ptr)(aep_ptr->msg_from_endpoint_param_ptr, &packet);

            // NOTE: Instead of using PostRxBuffer() here to make a new Rx buffer available to libfabric, we will do
            // it after the packet's buffer has been freed. See EfaRxEndpointRxBuffersFree(). This can be done
            // because used PostRxBuffer() for all the Rx buffers when the endpoint was created in
            // EfaRxEndpointOpen().
        }
    } else if (fi_ret < 0 && fi_ret != -FI_EAGAIN) {
        CDI_LOG_THREAD(kLogError, "Got[%d] from fi_cq_read().", fi_ret);
    }
    return ret;
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

    const int aligned_packet_size = (packet_size + packet_buffer_alignment - 1) & ~(packet_buffer_alignment - 1);

    // Huge pages are not guaranteed to be aligned at all. Add enough padding to be able to shift the starting address
    // to an aligned location.
    const int allocated_size = aligned_packet_size * packet_count + packet_buffer_alignment;

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
        if (0 == fi_mr_reg(endpoint_state_ptr->domain_ptr, mem_ptr, aligned_packet_size * packet_count,
                           FI_SEND | FI_RECV | FI_MULTI_RECV, 0, 0, 0,
                           &endpoint_state_ptr->rx_state.memory_region_ptr, NULL)) {

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
        }
    }

    if (ret) {
        endpoint_state_ptr->rx_state.allocated_buffer_ptr = allocated_ptr;
        endpoint_state_ptr->rx_state.allocated_buffer_size = allocated_size;
    } else {
        if (NULL != allocated_ptr) {
            CdiOsMemFreeHugePage(allocated_ptr, allocated_size);
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
        fi_close(&endpoint_state_ptr->rx_state.memory_region_ptr->fid);

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

    return kCdiStatusOk;
}

CdiReturnStatus EfaRxEndpointClose(EfaEndpointState* endpoint_state_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    ControlInterfaceDestroy(endpoint_state_ptr->tx_control_handle);
    endpoint_state_ptr->tx_control_handle = NULL;

    ProbeEndpointDestroy(endpoint_state_ptr->probe_endpoint_handle);
    endpoint_state_ptr->probe_endpoint_handle = NULL;

    // NOTE: This pool is not thread-safe, so  must ensure that only one thread is accessing it at a time.
    CdiPoolDestroy(endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle);
    endpoint_state_ptr->rx_state.packet_sgl_entries_pool_handle = NULL;

    return kCdiStatusOk;
}

CdiReturnStatus EfaRxEndpointRxBuffersFree(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr)
{
    AdapterEndpointState* adapter_state_ptr = (AdapterEndpointState*)handle;
    EfaEndpointState* endpoint_state_ptr = (EfaEndpointState*)adapter_state_ptr->type_specific_ptr;
    CdiReturnStatus rs = kCdiStatusOk;

    struct iovec msg_iov = {
        .iov_len = adapter_state_ptr->adapter_con_state_ptr->adapter_state_ptr->maximum_payload_bytes
    };

    // Free SGL data buffers and SGL entries.
    CdiSglEntry *sgl_entry_ptr = sgl_ptr->sgl_head_ptr;
    while (sgl_entry_ptr) {
        // Don't need to free resources if not connected, since all libfabric resources get reset whenever the
        // connection is lost.
        if (kCdiConnectionStatusConnected == handle->connection_status_code) {
            msg_iov.iov_base = sgl_entry_ptr->address_ptr;

            // NOTE: This function is called from PollThread(), so no need to use libfabric's FI_THREAD_SAFE option.
            // Access to libfabric functions such as fi_recvmsg() and fi_cq_read() use PollThread().
            if (!PostRxBuffer(endpoint_state_ptr, &msg_iov, NULL != sgl_entry_ptr->next_ptr)) {
                // Something terribly wrong in libfabric. Notify the probe component so it can start the connection
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

CdiReturnStatus EfaRxEndpointStart(EfaEndpointState* endpoint_state_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    int reserve_packets =
        endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->rx_state.reserve_packet_buffers;
    int max_payload_size =
        endpoint_state_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->maximum_payload_bytes;

    if (!CreatePacketPool(endpoint_state_ptr, max_payload_size, reserve_packets)) {
        rs =  kCdiStatusNotEnoughMemory;
    }

    return rs;
}

void EfaRxEndpointStop(EfaEndpointState* endpoint_state_ptr)
{
    FreePacketPool(endpoint_state_ptr);
}
