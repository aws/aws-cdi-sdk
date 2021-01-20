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

#include "adapter_efa_probe_tx.h"
#include "endpoint_manager.h"
#include "internal_tx.h"
#include "private.h"
#include "cdi_os_api.h"

#include "rdma/fabric.h"
#include "rdma/fi_endpoint.h"

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
 * This function sends the packet using the libfabric fi_tsendv or fi_sendv functions depending on whether
 * it is sending tagged or untagged packets.
 *
 * @param endpoint_state_ptr Pointer to EFA endpoint state structure.
 * @param msg_iov_ptr   Pointer to vector structure containing the message to be sent.
 * @param iov_count     A count value to identify which msg_iov_ptr.
 * @param context_ptr   A pointer to a data structure holding packet context information.
 * @param flush_packets True to flush any cached Tx packets, otherwise cache them as libfabric allows.
 *
 * @return True if successful, otherwise false is returned.
 */
static bool PostTxData(EfaEndpointState* endpoint_state_ptr, const struct iovec *msg_iov_ptr,
                       int iov_count, const void* context_ptr, bool flush_packets)
{
    bool ret = true;

    struct fid_ep *endpoint_ptr = endpoint_state_ptr->endpoint_ptr;

    // If we have reached our limit of caching sending the Tx packet or we don't have more to immediately send, then
    // don't use the FI_MORE flag so libfabric will update the NIC hardware registers with all the cached requests in an
    // optimized operation.
    uint64_t flags = FI_MORE;
    if (++endpoint_state_ptr->tx_state.tx_packets_sent_since_flush >= EFA_TX_PACKET_CACHE_SIZE || flush_packets) {
        flags = 0; // Clear the FI_MORE flag.
        endpoint_state_ptr->tx_state.tx_packets_sent_since_flush = 0; // Reset counter.
    }

    while (true) {
        struct fi_msg msg = {
            .msg_iov = msg_iov_ptr,
            .desc = NULL,
            .iov_count = iov_count,
            .addr = 0,
            .context = (void*)context_ptr,  // cast needed to override constness
            .data = 0
        };
        ssize_t fi_ret = fi_sendmsg(endpoint_ptr, &msg, flags);
        if (0 == fi_ret) {
            break;
        }

        if (-FI_EAGAIN != fi_ret) {
            CDI_LOG_THREAD(kLogError, "Got[%d] from fi_sendmsg().", fi_ret);
            ret = false;
            break;
        }
        // We only get here if the underlying EFA hardware is unable to send packets, causing the libfabric pipeline to
        // fill. It cannot accept another packet.
        // Until additional features in libfabric and the EFA driver are implemented, we must return
        // an error here and require the caller to restart the connection.
        CDI_LOG_THREAD(kLogError, "Got -FI_EAGAIN from fi_sendmsg(). This is not expected.");
        ret = false;
        break;
    }

    return ret;
}

/**
 * Poll libfabric for completion queue events.
 *
 * @param completion_queue_ptr Pointer to libfabric completion queue to poll.
 * @param comp_array Pointer to an array of completion queue data entries which will be filled in for any completion or
 *                   error events read for the endpoint.
 * @param packet_ack_count_ptr Pointer to the number of elements in ret_packet_ack_state_ptr_array on input; on output,
 *                             the location is updated with the number of events that were read into the array. This may
 *                             be zero up to the original value passed in or exactly one if an error event was read.
 *
 * @return true if zero or more completion events were read, false if an error event was read.
 */
static bool GetCompletions(struct fid_cq* completion_queue_ptr, struct fi_cq_data_entry* comp_array,
                           int* packet_ack_count_ptr)
{
    bool ret = true;

    int fi_ret = fi_cq_read(completion_queue_ptr, comp_array, *packet_ack_count_ptr);
    // If the returned value is greater than zero, then the value is the number of completion queue messages that were
    // returned in comp_array. Otherwise a negative value represents an  error or -FI_EAGAIN which means no completions
    // were ready.
    if (fi_ret > 0) {
        *packet_ack_count_ptr = fi_ret;
    } else {
        *packet_ack_count_ptr = 0;
        if (fi_ret < 0 && fi_ret != -FI_EAGAIN) {
            // Got an error.
            ret = false;
            if (-FI_EAVAIL == fi_ret) {
                // Get which completion event the error occurred on.
                struct fi_cq_err_entry cq_err = { 0 };
                fi_ret = fi_cq_readerr(completion_queue_ptr, &cq_err, 0);
                if (fi_ret > 0) {
                    // Was able to get the error for a completion event.
                    *packet_ack_count_ptr = fi_ret;
                    comp_array->op_context = cq_err.op_context;
                    comp_array->flags = cq_err.flags;
                    comp_array->len = cq_err.len;
                    comp_array->buf = cq_err.buf;
                    comp_array->data = cq_err.data;
                }
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
    bool status = GetCompletions(efa_endpoint_ptr->completion_queue_ptr, comp_array, &packet_ack_count);

    // Capture whether any useful work was done this time.
    ret = packet_ack_count > 0;

    // Account for the packets acknowleged.
    efa_endpoint_ptr->tx_state.tx_packets_in_process -= packet_ack_count;

    // Process any completions that were received.
    for (int i = 0; i < packet_ack_count; i++) {
        Packet* packet_ptr = comp_array[i].op_context;
        packet_ptr->tx_state.ack_status = status ? kAdapterPacketStatusOk : kAdapterPacketStatusFailed;

        // Send the completion message for the packet.
        (adapter_endpoint_ptr->msg_from_endpoint_func_ptr)(adapter_endpoint_ptr->msg_from_endpoint_param_ptr,
                                                           packet_ptr);

#ifdef DEBUG_PACKET_SEQUENCES
        CdiCDIPacketCommonHeader* common_hdr_ptr =
            (CdiCDIPacketCommonHeader*)packet_ptr->sg_list.sgl_head_ptr->address_ptr;
        CDI_LOG_THREAD(kLogInfo, "CQ T[%d] P[%d] S[%d]%s",
                        common_hdr_ptr->payload_type, common_hdr_ptr->payload_num,
                        common_hdr_ptr->packet_sequence_num,
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
    CdiReturnStatus rs = kCdiStatusOk;

    // Setup additional Tx specific resources.
    rs = EfaAdapterProbeEndpointCreate(endpoint_state_ptr, &endpoint_state_ptr->probe_endpoint_handle);
    if (!ProbeTxControlCreateInterface(endpoint_state_ptr->probe_endpoint_handle, remote_address_str, dest_port)) {
        rs = kCdiStatusAllocationFailed;
    }

    if (kCdiStatusOk != rs) {
        EfaTxEndpointClose(endpoint_state_ptr);
    }

    return rs;
}

CdiReturnStatus EfaTxEndpointPoll(EfaEndpointState* endpoint_state_ptr)
{
    return Poll(endpoint_state_ptr) ? kCdiStatusOk : kCdiStatusInternalIdle;
}

CdiReturnStatus EfaTxEndpointReset(EfaEndpointState* endpoint_state_ptr)
{
    ProbeEndpointReset(endpoint_state_ptr->probe_endpoint_handle);

    return kCdiStatusOk;
}

CdiReturnStatus EfaTxEndpointClose(EfaEndpointState* endpoint_state_ptr)
{
    // Must close the control interface before destroying resources they use, such as control_packet_fifo_handle.
    // Close sockets if they are open (CdiAdapterCloseEndpoint checks if handle is NULL).
    ControlInterfaceDestroy(endpoint_state_ptr->tx_control_handle);
    endpoint_state_ptr->tx_control_handle = NULL;

    ProbeEndpointDestroy(endpoint_state_ptr->probe_endpoint_handle);
    endpoint_state_ptr->probe_endpoint_handle = NULL;

    return kCdiStatusOk;
}

EndpointTransmitQueueLevel EfaGetTransmitQueueLevel(const AdapterEndpointHandle handle)
{
    EfaEndpointState* endpoint_state_ptr = (EfaEndpointState*)handle->type_specific_ptr;
    if (endpoint_state_ptr->tx_state.tx_packets_in_process == 0) {
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
    CdiCDIPacketCommonHeader* common_hdr_ptr = (CdiCDIPacketCommonHeader*)packet_ptr->sg_list.sgl_head_ptr->address_ptr;
    CDI_LOG_THREAD(kLogInfo, "T[%d] P[%3d] S[%3d]", common_hdr_ptr->payload_type, common_hdr_ptr->payload_num,
                   common_hdr_ptr->packet_sequence_num);
#endif

    if (!PostTxData(endpoint_state_ptr, msg_iov_array, iov_count, packet_ptr, flush_packets)) {
        rs = kCdiStatusSendFailed;
    } else {
        // Increment the Tx packets in progress count.
        endpoint_state_ptr->tx_state.tx_packets_in_process++;
    }

    if (kCdiStatusOk != rs) {
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
    endpoint_state_ptr->remote_fi_addr = FI_ADDR_UNSPEC;
    int count = 1;
    uint64_t flags = 0;
    void* context_ptr = NULL;
    int ret = fi_av_insert(endpoint_state_ptr->address_vector_ptr,
                           (void*)endpoint_state_ptr->remote_ipv6_gid_array, count,
                           &endpoint_state_ptr->remote_fi_addr, flags, context_ptr);
    if (count != ret) {
        // This is a fatal error.
        CDI_LOG_THREAD(kLogError, "Failed to start Tx connection. fi_av_insert() failed[%d]", ret);
        rs = kCdiStatusFatal;
    }

    // Reset endpoint state data.
    endpoint_state_ptr->tx_state.tx_packets_in_process = 0;

    return rs;
}

void EfaTxEndpointStop(EfaEndpointState* endpoint_state_ptr)
{
    if (endpoint_state_ptr->address_vector_ptr) {
        int count = 1;
        uint64_t flags = 0;
        // Try to remove the remote address. Ok if it doesn't exist.
        fi_av_remove(endpoint_state_ptr->address_vector_ptr, &endpoint_state_ptr->remote_fi_addr, count, flags);
    }
}
