// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for the EFA Tx adapter.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h"

#include "adapter_efa_probe_rx.h"
#include "adapter_efa_probe_tx.h"
#include "endpoint_manager.h"
#include "internal_tx.h"
#include "private.h"
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
 * This function sends the packet using the libfabric fi_sendmsg function.
 *
 * @param endpoint_state_ptr Pointer to EFA endpoint state structure.
 * @param msg_iov_ptr   Pointer to vector structure containing the message to be sent.
 * @param iov_count     A count value to identify which msg_iov_ptr.
 * @param context_ptr   A pointer to a data structure holding packet context information.
 * @param flush_packets True to flush any cached Tx packets, otherwise cache them as libfabric allows.
 *
 * @return If kCdiStatusOk, no error. If kCdiStatusRetry, then need to check completions and then retry. Otherwise an
 *         error has occurred.
 *
 */
static CdiReturnStatus PostTxData(EfaEndpointState* endpoint_state_ptr, const struct iovec *msg_iov_ptr,
                                  int iov_count, const void* context_ptr, bool flush_packets)
{
    CdiReturnStatus ret = kCdiStatusOk;
    struct fid_ep *endpoint_ptr = endpoint_state_ptr->endpoint_ptr;

    // If we have reached our limit of caching sending the Tx packet or we don't have more to immediately send, then
    // don't use the FI_MORE flag so libfabric will update the NIC hardware registers with all the cached requests in an
    // optimized operation.
    uint64_t flags = FI_MORE;
    if (++endpoint_state_ptr->tx_state.tx_packets_sent_since_flush >= EFA_TX_PACKET_CACHE_SIZE || flush_packets) {
        flags = 0; // Clear the FI_MORE flag.
        endpoint_state_ptr->tx_state.tx_packets_sent_since_flush = 0; // Reset counter.
    }

    assert(NULL != endpoint_state_ptr->tx_state.tx_user_payload_memory_region_ptr);
    assert(NULL != endpoint_state_ptr->tx_state.tx_internal_memory_region_ptr);
    void* desc_ptr_array[MAX_TX_SGL_PACKET_ENTRIES];
    void* hdr_desc_ptr = endpoint_state_ptr->libfabric_api_ptr->fi_mr_desc(
                            endpoint_state_ptr->tx_state.tx_internal_memory_region_ptr);
    void* payload_desc_ptr = endpoint_state_ptr->libfabric_api_ptr->fi_mr_desc(
                                endpoint_state_ptr->tx_state.tx_user_payload_memory_region_ptr);

    // There are two types of adapter packets: user and probe. Probe packets do not use any headers and only use a
    // single SGL entry for the probe payload data. User packets always contain at least two SGL entries. The first
    // entry is generated internally by the CDI-SDK and contains a CDI header. The remaining entries are for user
    // payload data.
    if (1 == iov_count) {
        // Only one entry, so probe packet (only contains probe payload data).
        desc_ptr_array[0] = payload_desc_ptr;
    } else {
        // Contains multiple SGL entries, so user packet (contains CDI header and user payload data).
        for (int i = 0; i < iov_count; ++i) {
            // First packet uses header memory, rest use payload memory.
            desc_ptr_array[i] = (0 == i) ? hdr_desc_ptr : payload_desc_ptr;
        }
    }
    struct fi_msg msg = {
        .msg_iov = msg_iov_ptr,
        .desc = desc_ptr_array,
        .iov_count = iov_count,
        .addr = 0,
        .context = (void*)context_ptr,  // cast needed to override constness
        .data = 0
    };

    ssize_t fi_ret = 0;
    fi_ret = endpoint_state_ptr->libfabric_api_ptr->fi_sendmsg(endpoint_ptr, &msg, flags);
    if (0 != fi_ret) {
        if (-FI_EAGAIN != fi_ret) {
            CDI_LOG_THREAD(kLogError, "Got error [%ld (%s)] from fi_sendmsg().",
                fi_ret, endpoint_state_ptr->libfabric_api_ptr->fi_strerror(-fi_ret));
                ret = kCdiStatusSendFailed;
        } else {
            CDI_LOG_THREAD(kLogInfo, "Got retry [%ld (%s)] from fi_sendmsg().",
                fi_ret, endpoint_state_ptr->libfabric_api_ptr->fi_strerror(-fi_ret));
                ret = kCdiStatusRetry;
        }
    }
    return ret;
}

/**
 * Poll libfabric for completion queue events.
 *
 * @param libfabric_api_ptr Pointer to libfabric V-table API.
 * @param completion_queue_ptr Pointer to libfabric completion queue to poll.
 * @param comp_array Pointer to an array of completion queue data entries which will be filled in for any completion or
 *                   error events read for the endpoint.
 * @param packet_ack_count_ptr Pointer to the number of elements in ret_packet_ack_state_ptr_array on input; on output,
 *                             the location is updated with the number of events that were read into the array. This may
 *                             be zero up to the original value passed in or exactly one if an error event was read.
 *
 * @return true if zero or more completion events were read, false if an error event was read.
 */
static bool GetCompletions(LibfabricApi* libfabric_api_ptr, struct fid_cq* completion_queue_ptr,
                           struct fi_cq_data_entry* comp_array, int* packet_ack_count_ptr)
{
    bool ret = true;
    const int comp_array_size = *packet_ack_count_ptr;
    int fi_ret = libfabric_api_ptr->fi_cq_read(completion_queue_ptr, comp_array, *packet_ack_count_ptr);
    // If the returned value is greater than zero, then the value is the number of completion queue messages that were
    // returned in comp_array. Otherwise a negative value represents an error or -FI_EAGAIN which means no completions
    // were ready.
    if (fi_ret > 0) {
        *packet_ack_count_ptr = fi_ret;
    } else {
        *packet_ack_count_ptr = 0;
        if (fi_ret < 0 && fi_ret != -FI_EAGAIN) {
            // Got one or more errors.
            ret = false;
            if (-FI_EAVAIL == fi_ret) {
                // Read out completion errors.
                int i = 0;
                struct fi_cq_err_entry cq_err = { 0 };
                while (1 == libfabric_api_ptr->fi_cq_readerr(completion_queue_ptr, &cq_err, 0)) {
                    assert(0 != cq_err.err);
                    //char buf[1024] = { 0 };
                    CDI_LOG_THREAD(kLogError,
                        "Completion error: [%s]. Ensure outbound security group is properly configured.",
                        libfabric_api_ptr->fi_strerror(cq_err.err));
                        //libfabric_api_ptr->fi_cq_strerror(completion_queue_ptr, cq_err.prov_errno, cq_err.err_data, buf, sizeof(buf)));
                    if (NULL != cq_err.op_context) {
                        comp_array[i].op_context = cq_err.op_context;
                        comp_array[i].flags = cq_err.flags;
                        comp_array[i].len = cq_err.len;
                        comp_array[i].buf = cq_err.buf;
                        comp_array[i].data = cq_err.data;
                        i++;
                    }
                    if (comp_array_size == i) {
                        break;
                    }
                    memset(&cq_err, 0, sizeof(cq_err));
                }
                *packet_ack_count_ptr = i;
            } else {
                CDI_LOG_THREAD_WHEN(kLogError, true, 1000, "Failed to get completion event. fi_cq_read() failed[%d (%s)]",
                    fi_ret, libfabric_api_ptr->fi_strerror(-fi_ret));
            }
        }
    }

    return ret;
}

/**
 * Used to poll for any pending Tx completion events and process them.
 *
 * @param efa_endpoint_ptr Pointer to EFA endpoint state data.
 *
 * @return true if useful work was done, false if the function did nothing productive.
 */
static bool Poll(EfaEndpointState* efa_endpoint_ptr)
{
    bool ret = false;
    AdapterEndpointState* adapter_endpoint_ptr = efa_endpoint_ptr->adapter_endpoint_ptr;

    struct fi_cq_data_entry comp_array[MAX_TX_BULK_COMPLETION_QUEUE_MESSAGES];
    int packet_ack_count = CDI_ARRAY_ELEMENT_COUNT(comp_array);
    bool status = GetCompletions(efa_endpoint_ptr->libfabric_api_ptr, efa_endpoint_ptr->completion_queue_ptr,
                                 comp_array, &packet_ack_count);

    // Capture whether any useful work was done this time.
    ret = packet_ack_count > 0;

    // Account for the packets acknowledged.
    efa_endpoint_ptr->tx_state.tx_packets_in_process -= packet_ack_count;

    // Process any completions that were received.
    for (int i = 0; i < packet_ack_count; i++) {
        Packet* packet_ptr = comp_array[i].op_context;
        assert(packet_ptr);
        packet_ptr->tx_state.ack_status = status ? kAdapterPacketStatusOk : kAdapterPacketStatusFailed;

        // Send the completion message for the packet.
        (adapter_endpoint_ptr->msg_from_endpoint_func_ptr)(adapter_endpoint_ptr->msg_from_endpoint_param_ptr,
                                                           packet_ptr, kEndpointMessageTypePacketSent);

#ifdef DEBUG_PACKET_SEQUENCES
        CdiProtocolHandle protocol_handle = adapter_endpoint_ptr->protocol_handle;
        CdiDecodedPacketHeader decoded_header;
        ProtocolPayloadHeaderDecode(protocol_handle, packet_ptr->sg_list.sgl_head_ptr->address_ptr,
                                    packet_ptr->sg_list.sgl_head_ptr->size_in_bytes, &decoded_header);
        CDI_LOG_THREAD(kLogInfo, "CQ T[%d] P[%d] S[%d]%s",
                        decoded_header.payload_type, decoded_header.payload_num,
                        decoded_header.packet_sequence_num,
                        (kAdapterPacketStatusOk != packet_ptr->tx_state.ack_status) ? " Err" : "");
#endif
    }

    if (!status && kCdiConnectionStatusConnected == adapter_endpoint_ptr->connection_status_code) {
        // Must assume the connection to the receiver has gone down and must reset it. Notify the probe component so
        // it can start the connection reset process.
        ProbeEndpointError(efa_endpoint_ptr->probe_endpoint_handle);
    }
    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus EfaTxEndpointOpen(EfaEndpointState* endpoint_state_ptr, const char* remote_address_str, int dest_port)
{
    (void)remote_address_str;
    (void)dest_port;

    // Setup additional Tx specific resources.
    return EfaAdapterProbeEndpointCreate(endpoint_state_ptr, &endpoint_state_ptr->probe_endpoint_handle);
}

CdiReturnStatus EfaTxEndpointPoll(EfaEndpointState* endpoint_state_ptr)
{
    return Poll(endpoint_state_ptr) ? kCdiStatusOk : kCdiStatusInternalIdle;
}

CdiReturnStatus EfaTxEndpointReset(EfaEndpointState* endpoint_state_ptr)
{
    ProbeEndpointReset(endpoint_state_ptr->probe_endpoint_handle);

    endpoint_state_ptr->tx_state.tx_packets_in_process = 0;
    endpoint_state_ptr->tx_state.tx_packets_sent_since_flush = 0;

    return kCdiStatusOk;
}

CdiReturnStatus EfaTxEndpointClose(EfaEndpointState* endpoint_state_ptr)
{
    // Stop the probe endpoint (stops its thread) before freeing probe related resources.
    ProbeEndpointStop(endpoint_state_ptr->probe_endpoint_handle);

    ProbeEndpointDestroy(endpoint_state_ptr->probe_endpoint_handle);
    endpoint_state_ptr->probe_endpoint_handle = NULL;

    return kCdiStatusOk;
}

EndpointTransmitQueueLevel EfaGetTransmitQueueLevel(const AdapterEndpointHandle handle)
{
    EfaEndpointState* endpoint_state_ptr = (EfaEndpointState*)handle->type_specific_ptr;
    if (0 == endpoint_state_ptr->tx_state.tx_packets_in_process) {
        return kEndpointTransmitQueueEmpty;
    } else if (endpoint_state_ptr->tx_state.tx_packets_in_process < SIMULTANEOUS_TX_PACKET_LIMIT) {
        return kEndpointTransmitQueueIntermediate;
    } else {
        return kEndpointTransmitQueueFull;
    }
}

CdiReturnStatus EfaTxEndpointSend(const AdapterEndpointHandle handle, const Packet* packet_ptr, bool flush_packets)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EfaEndpointState* endpoint_state_ptr = (EfaEndpointState*)handle->type_specific_ptr;

    struct iovec msg_iov_array[MAX_TX_SGL_PACKET_ENTRIES];
    int iov_count = 0;

    for (CdiSglEntry *sgl_entry_ptr = packet_ptr->sg_list.sgl_head_ptr; NULL != sgl_entry_ptr;
            sgl_entry_ptr = sgl_entry_ptr->next_ptr) {
        msg_iov_array[iov_count].iov_base = sgl_entry_ptr->address_ptr;
        msg_iov_array[iov_count].iov_len = sgl_entry_ptr->size_in_bytes;
        iov_count++;
    }

#ifdef DEBUG_PACKET_SEQUENCES
    CdiProtocolHandle protocol_handle = handle->protocol_handle;
    CdiDecodedPacketHeader decoded_header = { 0 };
    ProtocolPayloadHeaderDecode(protocol_handle, packet_ptr->sg_list.sgl_head_ptr->address_ptr,
                                packet_ptr->sg_list.sgl_head_ptr->size_in_bytes, &decoded_header);
    CDI_LOG_THREAD(kLogInfo, "T[%d] P[%3d] S[%3d]", decoded_header.payload_type, decoded_header.payload_num,
                   decoded_header.packet_sequence_num);
#endif

    rs = PostTxData(endpoint_state_ptr, msg_iov_array, iov_count, packet_ptr, flush_packets);
    if (kCdiStatusOk == rs) {
        // Increment the Tx packets in progress count.
        endpoint_state_ptr->tx_state.tx_packets_in_process++;
    }

    if (kCdiStatusOk != rs && kCdiStatusRetry != rs) {
        // For now, we must assume the connection to the receiver has gone down and must reset it. Notify the probe
        // component so it can start the connection reset process.
        ProbeEndpointError(endpoint_state_ptr->probe_endpoint_handle);
    }

    return rs;
}

CdiReturnStatus EfaTxEndpointStart(EfaEndpointState* endpoint_state_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    //  Initialize address vector (av) destination address.
    assert(endpoint_state_ptr->address_vector_ptr);
    assert(FI_ADDR_UNSPEC == endpoint_state_ptr->remote_fi_addr); // fi_av_insert has not yet been called
    int count = 1;
    uint64_t flags = 0;
    void* context_ptr = NULL;
    int fi_ret = endpoint_state_ptr->libfabric_api_ptr->fi_av_insert(endpoint_state_ptr->address_vector_ptr,
                        (void*)endpoint_state_ptr->remote_ipv6_gid_array, count,
                        &endpoint_state_ptr->remote_fi_addr, flags, context_ptr);
    if (count != fi_ret) {
        // This is a fatal error.
        CDI_LOG_THREAD(kLogError, "Failed to start Tx connection. fi_av_insert() failed[%d (%s)]",
            fi_ret, endpoint_state_ptr->libfabric_api_ptr->fi_strerror(-fi_ret));
        rs = kCdiStatusFatal;
    }

    // Reset endpoint state data.
    endpoint_state_ptr->tx_state.tx_packets_in_process = 0;

    return rs;
}

void EfaTxEndpointStop(EfaEndpointState* endpoint_state_ptr)
{
    if (endpoint_state_ptr->address_vector_ptr && FI_ADDR_UNSPEC != endpoint_state_ptr->remote_fi_addr) {
        int count = 1;
        uint64_t flags = 0;
        int ret = endpoint_state_ptr->libfabric_api_ptr->fi_av_remove(endpoint_state_ptr->address_vector_ptr,
                        &endpoint_state_ptr->remote_fi_addr, count, flags);
        if (0 != ret) {
            CDI_LOG_THREAD(kLogWarning, "Unexpected return [%d] from fi_av_remove.", ret);
        }
        endpoint_state_ptr->remote_fi_addr = FI_ADDR_UNSPEC;
    }
}
