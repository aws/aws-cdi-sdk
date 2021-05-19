// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used with the SDK that is not part of the API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.
#include "internal_rx.h"

#include <string.h>

#include "adapter_api.h"
#include "cdi_logger_api.h"
#include "cdi_pool_api.h"
#include "endpoint_manager.h"
#include "internal.h"
#include "private.h"
#include "private_avm.h"
#include "receive_buffer.h"
#include "rx_reorder_packets.h"
#include "rx_reorder_payloads.h"
#include "statistics.h"

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
 * Function to set status code and optional error message string in the core callback and frees any receive buffer
 * resources.
 *
 * @param rs Return status code.
 * @param error_msg_str Pointer to optional error message string.
 * @param core_cb_data_ptr Pointer to the core callback data structure.
 * @param payload_sgl_ptr Pointer to the payload SGL list to free.
 */
static void SetCbErrorAndFreeResources(CdiReturnStatus rs, const char *error_msg_str, CdiCoreCbData* core_cb_data_ptr,
                                       CdiSgList* payload_sgl_ptr)
{
    // If another error has already occurred don't overwrite it, just log this error.
    if (kCdiStatusOk == core_cb_data_ptr->status_code) {
        core_cb_data_ptr->status_code = rs;
        core_cb_data_ptr->err_msg_str = error_msg_str;
    }
    CDI_LOG_THREAD(kLogError, "%s", error_msg_str);

    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)payload_sgl_ptr->internal_data_ptr;
    // Post message to free the SGL resources.
    if (!CdiQueuePush(memory_state_ptr->cdi_endpoint_handle->rx_state.free_buffer_queue_handle, payload_sgl_ptr)) {
        CDI_LOG_THREAD(kLogInfo, "Rx free buffer queue[%s] full.",
                       CdiQueueGetName(memory_state_ptr->cdi_endpoint_handle->rx_state.free_buffer_queue_handle));
    }

    // Clear the SGL sent to the application callback by using our empty SGL entry. This allows other logic within CDI
    // to determine if an empty SGL was generated within CDI. In this case, CDI can ignore use of the list,
    // otherwise generate an error.
    SglSetEmptyForExternalUse(payload_sgl_ptr);
}

/**
 * Update payload statistics data whenever a payload has either been successfully received or an error occurred while
 * being received.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param work_request_ptr Pointer to work request data.
 */
static void UpdatePayloadStats(CdiEndpointState* endpoint_ptr, RxPayloadWorkRequestState* work_request_ptr)
{
    AppPayloadCallbackData* app_payload_cb_data_ptr = &work_request_ptr->app_payload_cb_data;

    if (CdiLogComponentIsEnabled(endpoint_ptr->connection_state_ptr, kLogComponentPayloadConfig)) {
        DumpPayloadConfiguration(&app_payload_cb_data_ptr->core_extra_data, app_payload_cb_data_ptr->extra_data_size,
                                 app_payload_cb_data_ptr->extra_data_array,
                                 endpoint_ptr->connection_state_ptr->protocol_type);
    }

    // Update these stats whenever we receive a payload or have a payload error.
    StatsGatherPayloadStatsFromConnection(endpoint_ptr, kCdiStatusOk == app_payload_cb_data_ptr->payload_status_code,
                                          work_request_ptr->start_time, work_request_ptr->max_latency_microsecs,
                                          app_payload_cb_data_ptr->payload_sgl.total_data_size);
}

/**
 * Call the Raw payload user-registered callback function.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param app_cb_data_ptr Pointer to application callback data.
 * @param core_cb_data_ptr Pointer to core callback data.
 */
static void InvokeRawPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr,
                                     CdiCoreCbData* core_cb_data_ptr)
{
    if (app_cb_data_ptr->extra_data_size) {
        // Raw connection should never have any extra data. Set error for callback.
        SetCbErrorAndFreeResources(kCdiStatusRxWrongProtocolType,
                                   "Got an AVM payload, but this is a RAW connection.", core_cb_data_ptr,
                                   &app_cb_data_ptr->payload_sgl);
    }

    CdiRawRxCbData cb_data;
    cb_data.core_cb_data = *core_cb_data_ptr;
    cb_data.sgl = app_cb_data_ptr->payload_sgl;

    CdiRawRxCallback rx_raw_cb_ptr = (CdiRawRxCallback)con_state_ptr->rx_state.cb_ptr;
    (rx_raw_cb_ptr)(&cb_data); // Call the user-registered Rx RAW callback function.
}

/**
 * Call the AVM payload user-registered callback function.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param app_cb_data_ptr Pointer to application callback data.
 * @param core_cb_data_ptr Pointer to core callback data.
 */
static void InvokeAvmPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr,
                                     CdiCoreCbData* core_cb_data_ptr)
{
    CdiAvmRxCbData cb_data;
    memset(&cb_data, 0, sizeof(cb_data));

    int extra_data_size = app_cb_data_ptr->extra_data_size;

    if (0 == extra_data_size) {
        // AVM connection should always have extra data. Set error for callback.
        SetCbErrorAndFreeResources(kCdiStatusRxWrongProtocolType,
            "Got a RAW payload, but this is an AVM connection. Use CdiAvmTxRawPayload() to send a RAW payload.",
            core_cb_data_ptr, &app_cb_data_ptr->payload_sgl);
    } else {
        // Parse the extra data.
        CDIPacketAvmUnion* avm_union_ptr = (CDIPacketAvmUnion*)app_cb_data_ptr->extra_data_array;

        // Copy the extra data to the callback data structure.
        cb_data.avm_extra_data = avm_union_ptr->common_header.avm_extra_data;

        // Determine whether an CdiAvmConfig structure came along with the payload by looking at the header size.
        if (sizeof(avm_union_ptr->with_config) == extra_data_size) {
            cb_data.config_ptr = &avm_union_ptr->with_config.config;
        } else {
            cb_data.config_ptr = NULL;
        }
    }
    cb_data.core_cb_data = *core_cb_data_ptr;
    cb_data.sgl = app_cb_data_ptr->payload_sgl;

    CdiAvmRxCallback rx_avm_cb_ptr = (CdiAvmRxCallback)con_state_ptr->rx_state.cb_ptr;
    (rx_avm_cb_ptr)(&cb_data); // Call the user-registered Rx AVM callback function.
}

/**
 * Update application callback data with header packet sequence number 0.
 *
 * @param app_payload_cb_data_ptr Address where to write the application callback data.
 * @param num0_info_ptr Pointer to CDI header for packet sequence number 0.
 */
static void UpdateApplicationCallbackDataFromCdiPacket0(AppPayloadCallbackData* app_payload_cb_data_ptr,
                                                        const CdiDecodedPacketNum0Info* num0_info_ptr)
{
    // Update application callback data.
    app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp = num0_info_ptr->origination_ptp_timestamp;
    app_payload_cb_data_ptr->core_extra_data.payload_user_data = num0_info_ptr->payload_user_data;
    app_payload_cb_data_ptr->tx_start_time_microseconds = num0_info_ptr->tx_start_time_microseconds;

    // Save the extra data in the work request.
    app_payload_cb_data_ptr->extra_data_size = num0_info_ptr->extra_data_size;
    if (app_payload_cb_data_ptr->extra_data_size) {
        // We have extra data, so copy it to our work request.
        memcpy(app_payload_cb_data_ptr->extra_data_array, num0_info_ptr->extra_data_ptr,
               app_payload_cb_data_ptr->extra_data_size);
    }
}

/**
 * Update payload state when header packet sequence number 0 arrives.
 *
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param header_ptr Pointer to CDI header that contains data to be added to payload state.
 */
static void UpdatePayloadStateDataFromCDIPacket0(RxPayloadState* payload_state_ptr,
                                                 CdiDecodedPacketHeader *header_ptr)
{
    // Got packet #0. Initialize payload state from data in packet sequence number zero's CDI header.
    payload_state_ptr->payload_num = header_ptr->payload_num;
    payload_state_ptr->expected_payload_data_size = header_ptr->num0_info.total_payload_size;
    payload_state_ptr->work_request_state.max_latency_microsecs = header_ptr->num0_info.max_latency_microsecs;

    // Update application callback data.
    UpdateApplicationCallbackDataFromCdiPacket0(&payload_state_ptr->work_request_state.app_payload_cb_data,
                                                &header_ptr->num0_info);

    payload_state_ptr->payload_state = kPayloadInProgress; // Advance payload state
}

/**
 * Initializes the state data for a payload. Call this when the first packet of a payload is received.
 *
 * @param protocol_handle Handle to protocol being used.
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param packet_ptr Pointer to the first packet received for a given payload.
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param header_ptr Pointer to decoded packet header.
 * @param payload_memory_state_ptr Pointer to location to write the address of a pool allocated CdiMemoryState
 *                                 structure.
 *
 * @return true if this function completed successfully, false if a problem was encountered.
 */
static bool InitializePayloadState(CdiProtocolHandle protocol_handle, CdiEndpointState* endpoint_ptr,
                                   const Packet* packet_ptr, RxPayloadState* payload_state_ptr,
                                   CdiDecodedPacketHeader* header_ptr, CdiMemoryState** payload_memory_state_ptr)
{
    bool ret = true;
    uint64_t start_time = CdiOsGetMicroseconds();
    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    int packet_sequence_num = header_ptr->packet_sequence_num;

    // Create state data for a new payload.
    CdiMemoryState* memory_state_ptr = NULL;
    // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
    // them at a time. This function is only called by PollThread().
    if (!CdiPoolGet(con_state_ptr->rx_state.payload_memory_state_pool_handle, (void**)&memory_state_ptr)) {
        BACK_PRESSURE_ERROR(con_state_ptr->back_pressure_state, kLogError,
                            "Failed to get CdiMemoryState from pool. Throwing away this payload[%d]. Timestamp[%u:%u]",
                            payload_state_ptr->payload_num,
                            app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                            app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds);
        ret = false;
    } else {
        *payload_memory_state_ptr = memory_state_ptr;

        // Initialize memory state data.
        memory_state_ptr->magic = kMagicMemory;
        memory_state_ptr->cdi_endpoint_handle = endpoint_ptr;
        memory_state_ptr->buffer_type = con_state_ptr->rx_state.config_data.rx_buffer_type;

        // Initialize Rx endpoint packet SGL buffer list.
        memset(&memory_state_ptr->endpoint_packet_buffer_sgl, 0, sizeof(memory_state_ptr->endpoint_packet_buffer_sgl));

        // Initialize work request state data.
        app_payload_cb_data_ptr->payload_status_code = kCdiStatusOk;
        app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds = 0;
        app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds = 0;
        app_payload_cb_data_ptr->core_extra_data.payload_user_data = 0;
        app_payload_cb_data_ptr->tx_start_time_microseconds = 0;
        payload_state_ptr->work_request_state.start_time = start_time;
        // Store pointer to memory state in the payload state data.
        payload_state_ptr->work_request_state.payload_memory_state_ptr = memory_state_ptr;

        // Initialize payload state data.
        CdiSgList* payload_sgl_list_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data.payload_sgl;
        memset(payload_sgl_list_ptr, 0, sizeof(*payload_sgl_list_ptr));
        payload_sgl_list_ptr->internal_data_ptr = payload_state_ptr->work_request_state.payload_memory_state_ptr;

        payload_state_ptr->payload_num = 0;
        payload_state_ptr->data_bytes_received = 0;
        payload_state_ptr->expected_payload_data_size = 0;
        payload_state_ptr->reorder_list_ptr = NULL;

        if (0 == packet_sequence_num) {
            UpdatePayloadStateDataFromCDIPacket0(payload_state_ptr, header_ptr);
        } else {
            payload_state_ptr->payload_state = kPayloadPacketZeroPending; // Advance payload state to expecting 0.
            // Still need to set the payload number since we have logic looking for in-progress payloads that have the
            // wrong payload number at the front end of RxPacketReceive().
            payload_state_ptr->payload_num = header_ptr->payload_num;
        }

        if (con_state_ptr->rx_state.config_data.rx_buffer_type == kCdiLinearBuffer) {
            if (!CdiPoolGet(con_state_ptr->linear_buffer_pool, (void*)&payload_state_ptr->linear_buffer_ptr)) {
                // Ensure this is NULL if the pool ran dry. This error condition will be reported to the application
                // through the callback made when the payload has been completely received.
                payload_state_ptr->linear_buffer_ptr = NULL;
            }
        } else {
            payload_state_ptr->linear_buffer_ptr = NULL;

            ret = RxReorderPacketPayloadStateInit(protocol_handle,
                                                  con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                                  con_state_ptr->rx_state.reorder_entries_pool_handle,
                                                  payload_state_ptr, &packet_ptr->sg_list,
                                                  header_ptr->encoded_header_size, packet_sequence_num);
        }
    }

    return ret;
}

/**
 * Copy the packet payloads's contents to its proper location within the current linear receive payload buffer. It takes
 * into account the case of packets with a data offset in the case where a packet's size somewhere in the payload was
 * reduced to limit the number of SGL entries required.
 *
 * @param con_state_ptr Pointer to connection state structure.
 * @param packet_ptr Pointer to packet whose contents are to be copied.
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param header_ptr Pointer to CDI header that contains data to be added to payload state.
 *
 * @return true if the function completed successfully, false if a problem was encountered.
 */
static bool CopyToLinearBuffer(CdiConnectionState* con_state_ptr, const Packet* packet_ptr,
                               RxPayloadState* const payload_state_ptr, const CdiDecodedPacketHeader* header_ptr)
{
    bool ret = true;

    // Using linear memory buffer.
    int offset = header_ptr->data_offset_info.payload_data_offset;
    const int byte_count = packet_ptr->sg_list.total_data_size - header_ptr->encoded_header_size;

    // Ensure that the gather will end up fully within the linear buffer.
    uint64_t linear_buffer_size = con_state_ptr->rx_state.config_data.linear_buffer_size;
    if (offset + byte_count < 0 || (uint64_t)(offset + byte_count) > linear_buffer_size) {
        PAYLOAD_ERROR(con_state_ptr, &payload_state_ptr->work_request_state.app_payload_cb_data,
                      kCdiStatusBufferOverflow, "Payload data size[%d] exceeds linear buffer size[%d]. Copy failed.",
                      offset + byte_count, linear_buffer_size);
        ret = false;
    } else {
        // Copy the data from the packet(s) into the desired buffer at the payload's offset, skipping the header
        // portion.
        const int bytes_gathered = CdiCoreGather(&packet_ptr->sg_list, header_ptr->encoded_header_size,
                                                 payload_state_ptr->linear_buffer_ptr + offset, byte_count);
        assert(bytes_gathered <= byte_count);
        payload_state_ptr->data_bytes_received += bytes_gathered;
    }

    return ret;
}

/**
 * Free payload memory state.
 *
 * @param sgl_ptr Pointer to payload scatter-gather list.
 */
static void FreeMemoryState(CdiSgList* sgl_ptr)
{
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)sgl_ptr->internal_data_ptr;

    // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
    // them at a time. This function is only called by PollThread().
    CdiConnectionState* con_state_ptr = memory_state_ptr->cdi_endpoint_handle->connection_state_ptr;

    // Return memory state to pool.
    CdiPoolPut(con_state_ptr->rx_state.payload_memory_state_pool_handle, memory_state_ptr);
    // Pointers are no longer valid, so clear them to prevent future accidental use.
    memory_state_ptr = NULL;
    sgl_ptr->internal_data_ptr = NULL;
}

/**
 * Free resources specific to a payload. Adapter packet resources are freed separately.
 *
 * @param sgl_ptr Pointer to payload scatter-gather list.
 */
static void FreePayloadBuffer(CdiSgList* sgl_ptr)
{
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)sgl_ptr->internal_data_ptr;

    if (memory_state_ptr) {
        // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
        // them at a time. This function is only called by PollThread().
        CdiConnectionState* con_state_ptr = memory_state_ptr->cdi_endpoint_handle->connection_state_ptr;

        if (memory_state_ptr->buffer_type == kCdiLinearBuffer) {
            // Return the linear buffer to its pool; its address is in the singular SGL entry.
            if (sgl_ptr->sgl_head_ptr && sgl_ptr->sgl_head_ptr->address_ptr) {
                CdiPoolPut(con_state_ptr->linear_buffer_pool, sgl_ptr->sgl_head_ptr->address_ptr);
                sgl_ptr->sgl_head_ptr->address_ptr = NULL; // Pointer is no longer valid, so clear it.
            }
        }

#ifdef DEBUG_RX_DUMP_RAW_SGL_ENTRIES
        {
            CdiSglEntry* sgl_entry_ptr = sgl_ptr->sgl_head_ptr;
            while (sgl_entry_ptr) {
                SDK_LOG_GLOBAL(kLogDebug, "PuttingEntry[%p]", sgl_entry_ptr);
                sgl_entry_ptr = sgl_entry_ptr->next_ptr;
            }
        }
#endif

#ifdef DEBUG_RX_PAYLOAD_SGL_ENTRY_FREE_COUNT
        int pre_free_count = CdiPoolGetFreeItemCount(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
#endif

        // Free the user facing payload SGL entries returning them back to the pool.
        FreeSglEntries(con_state_ptr->rx_state.payload_sgl_entry_pool_handle, sgl_ptr->sgl_head_ptr);
        sgl_ptr->total_data_size = 0;

#ifdef DEBUG_RX_PAYLOAD_SGL_ENTRY_FREE_COUNT
        int post_free_count = CdiPoolGetFreeItemCount(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
        SDK_LOG_GLOBAL(kLogDebug, "Rx Payload Free. Pre[%d] Post[%d]", pre_free_count, post_free_count);
#endif
    }
}

/**
 * Finalizes the payload state. Call this once all data expected for the payload has been received.
 *
 * @param con_state_ptr Pointer to connection state structure.
 * @param payload_state_ptr Pointer to payload structure being updated.
 *
 * @return Returns true if payload successfully received without any packet reorder issues, otherwise false is
 *         returned.
 */
static bool FinalizePayload(CdiConnectionState* con_state_ptr, RxPayloadState* payload_state_ptr)
{
    bool ret = true;

    if (payload_state_ptr->data_bytes_received != payload_state_ptr->expected_payload_data_size) {
        CDI_LOG_THREAD(kLogError, "Expected payload size[%d]. Received[%d].",
                       payload_state_ptr->expected_payload_data_size, payload_state_ptr->data_bytes_received);
        ret = false;
    }

    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;

    // If the above logic fails, we still want to execute this logic to provide possible additional error information
    // and to free resources used.
    if (kCdiSgl == con_state_ptr->rx_state.config_data.rx_buffer_type) {
        // If all data received, then there can only be one list and the next and prev pointers must be NULL.
        if (payload_state_ptr->reorder_list_ptr->next_ptr || payload_state_ptr->reorder_list_ptr->prev_ptr) {
            CDI_LOG_THREAD(kLogError, "All payload data received but there are unattached lists present.");
            CDI_LOG_THREAD(kLogError, "Throwing away this payload[%d]. Timestamp[%u:%u] Expected Size[%d] Received[%d]",
                           payload_state_ptr->payload_num,
                           app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                           app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds,
                           payload_state_ptr->expected_payload_data_size, payload_state_ptr->data_bytes_received);
#ifdef DEBUG_RX_REORDER_ERROR
             CdiReorderList* reorder_list_ptr = payload_state_ptr->reorder_list_ptr;
             while (reorder_list_ptr) {
                 CDI_LOG_THREAD(kLogError, "Unattached list [%d-%d].", reorder_list_ptr->top_sequence_num,
                                reorder_list_ptr->bot_sequence_num);
                 reorder_list_ptr = reorder_list_ptr->next_ptr;
             }
#endif
            // Return the memory space back to the respective pools.
            RxReorderPacketFreeLists(payload_state_ptr->reorder_list_ptr,
                                     con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                     con_state_ptr->rx_state.reorder_entries_pool_handle);
            ret = false;
        } else {
            // Update SGL's total data size and pointers.
            CdiSgList* sgl_ptr = &app_payload_cb_data_ptr->payload_sgl;
            sgl_ptr->total_data_size = payload_state_ptr->data_bytes_received;
            sgl_ptr->sgl_head_ptr = payload_state_ptr->reorder_list_ptr->sglist.sgl_head_ptr;
            sgl_ptr->sgl_tail_ptr = payload_state_ptr->reorder_list_ptr->sglist.sgl_tail_ptr;

            // Free the reorder list memory entry.
            CdiPoolPut(con_state_ptr->rx_state.reorder_entries_pool_handle, payload_state_ptr->reorder_list_ptr);
        }
        payload_state_ptr->reorder_list_ptr = NULL; // List freed by both cases above and no longer valid, so clear it.
    } else {
        // If the linear buffer pointer is NULL, the packets for this payload were dropped into the bit bucket.
        // Send this condition on through the pipeline.
        if (NULL != payload_state_ptr->linear_buffer_ptr) {
            // Update the specifics of the memory state structure.
            MemoryLinearState* ptr =
                &payload_state_ptr->work_request_state.payload_memory_state_ptr->linear_state;
            ptr->byte_size = payload_state_ptr->data_bytes_received;
            ptr->virtual_address = payload_state_ptr->linear_buffer_ptr;

            // The physical address will probably have to be set once the NIC can write directly to the receive
            // buffer. Until then, force it to not be some random value just in case.
            ptr->physical_address = 0LL;

            // Allocate a single SGL entry to point to the linear buffer and add it to the SGL.
            CdiSglEntry* entry_ptr = NULL;
            if (!CdiPoolGet(con_state_ptr->rx_state.payload_sgl_entry_pool_handle, (void*)&entry_ptr)) {
                ret = false;
            } else {
                entry_ptr->internal_data_ptr = NULL;
                entry_ptr->address_ptr = payload_state_ptr->linear_buffer_ptr;
                entry_ptr->next_ptr = NULL;
                entry_ptr->size_in_bytes = payload_state_ptr->data_bytes_received;
                app_payload_cb_data_ptr->payload_sgl.total_data_size = entry_ptr->size_in_bytes;
                app_payload_cb_data_ptr->payload_sgl.sgl_head_ptr = entry_ptr;
                app_payload_cb_data_ptr->payload_sgl.sgl_tail_ptr = entry_ptr;
            }
        }
    }

    return ret;
}

/**
 * Queue back pressure payload to application.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param endpoint_ptr Pointer to endpoint data.
 * @param decoded_header_ptr Pointer to decoded packet header.
 */
static void QueueBackPressurePayloadToApp(CdiConnectionState* con_state_ptr, CdiEndpointState* endpoint_ptr,
                                          const CdiDecodedPacketHeader* decoded_header_ptr)
{
    AppPayloadCallbackData cb_data = {
        .payload_status_code = kCdiStatusRxPayloadBackPressure
    };
    if (0 == decoded_header_ptr->packet_sequence_num) {
        UpdateApplicationCallbackDataFromCdiPacket0(&cb_data, &decoded_header_ptr->num0_info);
    }

    // If the protocol is AVM and no extra data exist we must at least provide an entry for a stream identifer,
    // otherwise downstream logic will generate an error (since AVM protocol must contain extra data).
    if (kProtocolTypeAvm == con_state_ptr->protocol_type && 0 == cb_data.extra_data_size) {
        cb_data.extra_data_size = sizeof(CDIPacketAvmUnion);
        CDIPacketAvmUnion* avm_union_ptr = (CDIPacketAvmUnion*)cb_data.extra_data_array;
        avm_union_ptr->common_header.avm_extra_data.stream_identifier = -1; // Unknown stream ID
    }

    // Increment the dropped payload count. This value is also incremented in TxPayloadThread(), so use atomic
    // operation here.
    CdiOsAtomicInc32(&endpoint_ptr->transfer_stats.payload_counter_stats.num_payloads_dropped);

    // Place the callback data in the queue to be sent to the application.
    if (!CdiQueuePush(con_state_ptr->rx_state.active_payload_complete_queue_handle, (void*)&cb_data)) {
        CDI_LOG_THREAD(kLogError, "Queue[%s] full, push failed.",
                       CdiQueueGetName(con_state_ptr->rx_state.active_payload_complete_queue_handle));
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus RxCreateInternal(ConnectionProtocolType protocol_type, CdiRxConfigData* config_data_ptr,
                                 CdiCallback rx_cb_ptr, CdiConnectionHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)CdiOsMemAllocZero(sizeof *con_state_ptr);
    if (con_state_ptr == NULL) {
        return kCdiStatusNotEnoughMemory;
    }
    int max_rx_payloads = config_data_ptr->max_simultaneous_rx_payloads_per_connection;
    if (max_rx_payloads == 0) {
        max_rx_payloads = MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION;
    }

    con_state_ptr->adapter_state_ptr = config_data_ptr->adapter_handle;
    con_state_ptr->handle_type = kHandleTypeRx;
    con_state_ptr->protocol_type = protocol_type;
    con_state_ptr->magic = kMagicConnection;
    memcpy(&con_state_ptr->rx_state.config_data, config_data_ptr, sizeof *config_data_ptr);
    con_state_ptr->rx_state.cb_ptr = rx_cb_ptr;
    // Now that we have a connection logger, we can use the CDI_LOG_HANDLE() macro to add log messages to it. Since this
    // thread is from the application, we cannot use the CDI_LOG_THEAD() macro.

    if (-1 == con_state_ptr->rx_state.config_data.buffer_delay_ms) {
        con_state_ptr->rx_state.config_data.buffer_delay_ms = ENABLED_RX_BUFFER_DELAY_DEFAULT_MS;
    } else {
        if (config_data_ptr->buffer_delay_ms > MAXIMUM_RX_BUFFER_DELAY_MS) {
            CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogError,
                           "Buffer delay specified[%d]ms exceeds maximum allowable value[%d]ms.",
                           config_data_ptr->buffer_delay_ms, MAXIMUM_RX_BUFFER_DELAY_MS);
            rs = kCdiStatusInvalidParameter;
        } else if (config_data_ptr->buffer_delay_ms < -1) {
            CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogError,
                           "Buffer delay specified[%d]ms is a negative value.",
                           config_data_ptr->buffer_delay_ms);
            rs = kCdiStatusInvalidParameter;
        }
    }

    // This log will be used by all the threads created for this connection.
    if (kCdiStatusOk == rs) {
        if (kLogMethodFile == config_data_ptr->connection_log_method_data_ptr->log_method) {
            CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogInfo, "Setting log file[%s] for SDK Rx logging.",
                           config_data_ptr->connection_log_method_data_ptr->log_filename_str);
        }
        if (!CdiLoggerCreateLog(cdi_global_context.logger_handle, con_state_ptr,
                                config_data_ptr->connection_log_method_data_ptr, &con_state_ptr->log_handle)) {
            rs = kCdiStatusCreateLogFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo, "Creating Rx connection. Protocol[%s] Destination Port[%d] Name[%s]",
                       CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, protocol_type),
                       con_state_ptr->rx_state.config_data.dest_port,
                       CdiGetEmptyStringIfNull(con_state_ptr->rx_state.config_data.connection_name_str));
        if (con_state_ptr->rx_state.config_data.buffer_delay_ms) {
            CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo, "Using Rx buffer delay[%d]ms.",
                           con_state_ptr->rx_state.config_data.buffer_delay_ms);
        }
    }

    // Copy the name for the connection from the config data or generate one. NOTE: Do this here, since other logic
    // below uses the saved name.
    if ((NULL == config_data_ptr->connection_name_str) || (0 == strlen(config_data_ptr->connection_name_str))) {
        snprintf(con_state_ptr->saved_connection_name_str, sizeof(con_state_ptr->saved_connection_name_str), "dest%d",
                 config_data_ptr->dest_port);

        config_data_ptr->connection_name_str = con_state_ptr->saved_connection_name_str;

        CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo, "Rx connection is unnamed. Created name[%s]",
                       con_state_ptr->saved_connection_name_str);
    } else {
        CdiOsStrCpy(con_state_ptr->saved_connection_name_str, sizeof(con_state_ptr->saved_connection_name_str),
                    con_state_ptr->rx_state.config_data.connection_name_str);
    }

    // Update copy of config data to use the saved connection string.
    con_state_ptr->rx_state.config_data.connection_name_str = con_state_ptr->saved_connection_name_str;

    if (kCdiStatusOk == rs) {
        rs = ConnectionCommonResourcesCreate(con_state_ptr, config_data_ptr->stats_cb_ptr,
                                             config_data_ptr->stats_user_cb_param, &config_data_ptr->stats_config);
    }

    int reserve_packet_buffers = MAX_RX_PACKETS_PER_CONNECTION;
    if (con_state_ptr->rx_state.config_data.buffer_delay_ms) {
        // Rx buffer delay is enabled, so we need to allocate additional Rx buffers.
        reserve_packet_buffers += (MAX_RX_PACKETS_PER_CONNECTION * con_state_ptr->rx_state.config_data.buffer_delay_ms)
                                  / RX_BUFFER_DELAY_BUFFER_MS_DIVISOR;
    }

    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Connection Rx CdiSglEntry Pool", reserve_packet_buffers,
                           MAX_RX_PACKETS_PER_CONNECTION_GROW, MAX_POOL_GROW_COUNT,
                           sizeof(CdiSglEntry), true, // true= Make thread-safe
                           &con_state_ptr->rx_state.payload_sgl_entry_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Rx CdiReorderList Out of Order Pool", MAX_RX_OUT_OF_ORDER,
                           MAX_RX_OUT_OF_ORDER_GROW, MAX_POOL_GROW_COUNT,
                           sizeof(CdiReorderList), true, // true= Make thread-safe
                           &con_state_ptr->rx_state.reorder_entries_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs && config_data_ptr->rx_buffer_type == kCdiLinearBuffer) {
        // Allocate an extra couple of buffers for payloads being reassembled.
        if (!CdiPoolCreate("Rx Linear Buffer Pool", RX_LINEAR_BUFFER_COUNT + 2, NO_GROW_SIZE, NO_GROW_COUNT,
                           config_data_ptr->linear_buffer_size, true, &con_state_ptr->linear_buffer_pool)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Set up receive buffer handling if enabled; either way, set payload complete queue to point to the right one.
        if (con_state_ptr->rx_state.config_data.buffer_delay_ms != 0) {
            rs = RxBufferInit(con_state_ptr->log_handle, con_state_ptr->error_message_pool,
                              con_state_ptr->rx_state.config_data.buffer_delay_ms, max_rx_payloads,
                              con_state_ptr->app_payload_message_queue_handle,
                              &con_state_ptr->rx_state.receive_buffer_handle,
                              &con_state_ptr->rx_state.active_payload_complete_queue_handle);
        } else {
            // No receive buffer so send payloads directly to application callback thread's input queue.
            con_state_ptr->rx_state.active_payload_complete_queue_handle =
                con_state_ptr->app_payload_message_queue_handle;
        }
    }

    // NOTE: The pools at rx_state.rx_payload_state_pool_handle and rx_state.payload_memory_state_pool_handle are
    // created dynamically in RxEndpointCreateDynamicPools() based on the protocol version being used.

    if (kCdiStatusOk == rs) {
        // Create a packet message thread that is used by both Tx and Rx connections.
        rs = ConnectionCommonPacketMessageThreadCreate(con_state_ptr);
    }

    if (kCdiStatusOk == rs) {
        // Open an connection to receive packets from a remote host.
        CdiAdapterConnectionConfigData config_data = {
            .cdi_adapter_handle = con_state_ptr->adapter_state_ptr,
            .cdi_connection_handle = con_state_ptr,
            .endpoint_manager_handle = con_state_ptr->endpoint_manager_handle,

            .connection_cb_ptr = config_data_ptr->connection_cb_ptr,
            .connection_user_cb_param = config_data_ptr->connection_user_cb_param,

            .log_handle = con_state_ptr->log_handle,
            .port_number = config_data_ptr->dest_port,
            .thread_core_num = config_data_ptr->thread_core_num,

            .direction = kEndpointDirectionReceive,
            .rx_state.reserve_packet_buffers = reserve_packet_buffers,

            // This endpoint is used for normal data transmission (not used for control). This means that the Endpoint
            // Manager is used for managing threads related to the connection.
            .data_type = kEndpointTypeData,
        };
        if (kCdiStatusOk != CdiAdapterCreateConnection(&config_data, &con_state_ptr->adapter_connection_ptr)) {
            rs = kCdiStatusFatal;
        }
    }

    // Socket adapter does not dynamically create Rx endpoints, so create it here.
    if (kCdiStatusOk == rs && kCdiAdapterTypeSocket == config_data_ptr->adapter_handle->adapter_data.adapter_type) {
        rs = EndpointManagerRxCreateEndpoint(con_state_ptr->endpoint_manager_handle, config_data_ptr->dest_port, NULL);
    }

    if (kCdiStatusOk == rs) {
        CdiOsSignalSet(con_state_ptr->start_signal); // Start connection threads.
        CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo, "Successfully created Rx connection. Name[%s]",
                       con_state_ptr->saved_connection_name_str);
    }

    if (kCdiStatusOk != rs) {
        ConnectionDestroyInternal((CdiConnectionHandle)con_state_ptr);
        con_state_ptr = NULL;
    }

    *ret_handle_ptr = (CdiConnectionHandle)con_state_ptr;

    return rs;
}

CdiReturnStatus RxEndpointCreateDynamicPools(CdiEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    // Add one to the maximum value so we get the actual number of entries required.
    int required_size = endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max + 1;

    // Payload state pool.
    if (con_state_ptr->rx_state.rx_payload_state_pool_handle) {
        int current_size = CdiPoolGetTotalItemCount(con_state_ptr->rx_state.rx_payload_state_pool_handle);
        if (current_size < required_size) {
            CdiPoolDestroy(con_state_ptr->rx_state.rx_payload_state_pool_handle);
            con_state_ptr->rx_state.rx_payload_state_pool_handle = NULL;
        }
    }
    if (NULL == con_state_ptr->rx_state.rx_payload_state_pool_handle) {
        if (!CdiPoolCreate("Rx Payload State Pool", required_size, NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(RxPayloadState), true,
                           &con_state_ptr->rx_state.rx_payload_state_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    // Memory state pool.
    if (kCdiStatusOk == rs) {
        if (con_state_ptr->rx_state.payload_memory_state_pool_handle) {
            int current_size = CdiPoolGetTotalItemCount(con_state_ptr->rx_state.payload_memory_state_pool_handle);
            if (current_size < required_size) {
                CdiPoolDestroy(con_state_ptr->rx_state.payload_memory_state_pool_handle);
                con_state_ptr->rx_state.payload_memory_state_pool_handle = NULL;
            }
        }
        if (NULL == con_state_ptr->rx_state.payload_memory_state_pool_handle) {
            if (!CdiPoolCreate("Connection Rx CdiMemoryState Pool", required_size, NO_GROW_SIZE, NO_GROW_COUNT,
                            sizeof(CdiMemoryState), true, // true= Make thread-safe
                            &con_state_ptr->rx_state.payload_memory_state_pool_handle)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }
    }

    return rs;
}

void RxEndpointFlushResources(CdiEndpointState* endpoint_ptr)
{
    if (endpoint_ptr) {
        // Walk through the list of payload state data and see if any payloads were in the process of being received. If
        // so, set an error.
        for (int i = 0; i < CDI_ARRAY_ELEMENT_COUNT(endpoint_ptr->rx_state.payload_state_array_ptr); i++) {
            RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[i];
            if (payload_state_ptr) {
                if (kPayloadIdle != payload_state_ptr->payload_state &&
                    kPayloadIgnore != payload_state_ptr->payload_state &&
                    kPayloadError != payload_state_ptr->payload_state) {
                    // Free payload resources. Also frees entry for linear_buffer_pool (if used).
                    RxFreePayloadResources(endpoint_ptr, payload_state_ptr, true);
                }
                CdiPoolPut(endpoint_ptr->connection_state_ptr->rx_state.rx_payload_state_pool_handle,
                           payload_state_ptr);
                endpoint_ptr->rx_state.payload_state_array_ptr[i] = NULL; // Pointer is no longer valid, so clear it.
            }
        }
        endpoint_ptr->rx_state.rxreorder_buffered_packet_count = 0; // Reset packet count window.

        // Entries used by the connection pools below are not freed here. They are either freed in the logic above or
        // by the application:
        //   rx_state.reorder_entries_pool_handle
        //   rx_state.payload_sgl_entry_pool_handle
        //   rx_state.payload_memory_state_pool_handle

        CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
        con_state_ptr->back_pressure_state = kCdiBackPressureNone; // Reset back pressure state.
    }
}

void RxConnectionDestroyInternal(CdiConnectionHandle con_handle)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)con_handle;

    if (con_state_ptr) {
        // Now that the connection and adapter threads have stopped, it is safe to clean up the remaining resources in
        // the opposite order of their creation.

        // Destroying the connection, so ensure all pool entries are freed.
        CdiPoolPutAll(con_state_ptr->rx_state.rx_payload_state_pool_handle);
        CdiPoolDestroy(con_state_ptr->rx_state.rx_payload_state_pool_handle);
        con_state_ptr->rx_state.rx_payload_state_pool_handle = NULL;

        // Set this to NULL before calling RxBufferDestroy() because the queue that it points to will be destroyed there
        // if the receive buffer was enabled.
        con_state_ptr->rx_state.active_payload_complete_queue_handle = NULL;

        RxBufferDestroy(con_state_ptr->rx_state.receive_buffer_handle);
        con_state_ptr->rx_state.receive_buffer_handle = NULL;

        // Destroying the connection, so ensure all pool entries are freed.
        CdiPoolPutAll(con_state_ptr->linear_buffer_pool);
        CdiPoolDestroy(con_state_ptr->linear_buffer_pool);
        con_state_ptr->linear_buffer_pool = NULL;

        // Destroying the connection, so ensure all pool entries are freed.
        CdiPoolPutAll(con_state_ptr->rx_state.reorder_entries_pool_handle);
        CdiPoolDestroy(con_state_ptr->rx_state.reorder_entries_pool_handle);
        con_state_ptr->rx_state.reorder_entries_pool_handle = NULL;

        // Destroying the connection, so ensure all pool entries are freed.
        CdiPoolPutAll(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
        CdiPoolDestroy(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
        con_state_ptr->rx_state.payload_sgl_entry_pool_handle = NULL;

        // Destroying the connection, so ensure all pool entries are freed.
        CdiPoolPutAll(con_state_ptr->rx_state.payload_memory_state_pool_handle);
        CdiPoolDestroy(con_state_ptr->rx_state.payload_memory_state_pool_handle);
        con_state_ptr->rx_state.payload_memory_state_pool_handle = NULL;

        // NOTE: con_state_ptr is freed by the caller.
    }
}

void RxEndpointDestroy(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    if (endpoint_ptr) {
        CdiQueueDestroy(endpoint_ptr->rx_state.free_buffer_queue_handle);
        endpoint_ptr->rx_state.free_buffer_queue_handle = NULL;
    }
}

void RxPacketReceive(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type)
{
    assert(kEndpointMessageTypePacketReceived == message_type);
    (void)message_type;

    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)param_ptr;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    bool still_ok = true;

    CdiProtocolHandle protocol_handle = endpoint_ptr->adapter_endpoint_ptr->protocol_handle;
    if (NULL == protocol_handle) {
        CDI_LOG_THREAD(kLogError, "Connection[%s] Received packet but no protocol defined to decode it. ",
                       con_state_ptr->saved_connection_name_str);
        // Free the buffer and return. No need to flow through all the logic below.
        CdiAdapterFreeBuffer(endpoint_ptr->adapter_endpoint_ptr, &packet_ptr->sg_list);
        return;
    }

    CdiDecodedPacketHeader decoded_header = { 0 };
    ProtocolPayloadHeaderDecode(protocol_handle, packet_ptr->sg_list.sgl_head_ptr->address_ptr,
                                packet_ptr->sg_list.sgl_head_ptr->size_in_bytes, &decoded_header);
    int payload_num = decoded_header.payload_num;
    int packet_sequence_num = decoded_header.packet_sequence_num;
    int cdi_header_size = decoded_header.encoded_header_size;

#ifdef DEBUG_PACKET_SEQUENCES
    CdiPayloadType payload_type = decoded_header.payload_type;
    CDI_LOG_THREAD(kLogInfo, "T[%d] P[%3d] S[%3d] A[%p]", payload_type, payload_num, packet_sequence_num,
                   packet_ptr->sg_list.sgl_head_ptr->address_ptr);
#endif

    RxPayloadState* payload_state_ptr = RxReorderPayloadStateGet(endpoint_ptr,
                                                                 con_state_ptr->rx_state.rx_payload_state_pool_handle,
                                                                 payload_num);
    if (NULL == payload_state_ptr) {
        still_ok = false;
    } else {
        // Should never be here in the error state. The error state is only set in the logic below and then changed to
        // ignore before this function exits.
        assert(kPayloadError != payload_state_ptr->payload_state);

        // No need to check if this is already set. If this code is being reached a first payload has been received.
        con_state_ptr->rx_state.received_first_payload = true;

        // If we get a packet for a completed payload, issue a warning, and then set the suspend_warnings flag so that
        // we don't keep issuing warnings if we get more packets for this same payload before it is sent to the
        // application.
        if (!payload_state_ptr->suspend_warnings && (kPayloadComplete == payload_state_ptr->payload_state)) {
            CDI_LOG_THREAD(kLogWarning, "Connection[%s] Received packet for completed payload[%d]. Additional packets "
                           "for this payload will be dropped.",
                           con_state_ptr->saved_connection_name_str, payload_num);
            payload_state_ptr->suspend_warnings = true;
        }

        // If we have received a packet for a payload that is marked ignore, we will ignore incoming packets for it
        // until we have received MAX_RX_PACKET_OUT_OF_ORDER_WINDOW packets since the payload was set to ignore.
        if (kPayloadIgnore == payload_state_ptr->payload_state &&
            RxReorderPayloadIsStale(endpoint_ptr, payload_state_ptr)) {
            // Payload state data is stale, so ok to re-use it now.
            RxReorderPayloadResetState(payload_state_ptr, payload_num);
        }

        // This will be true while processing of the packet proceeds normally. The packet ignore and error states are
        // considered abnormal in the respect that the packet does not undergo the normal processing. Any allocated
        // resources coming into the function and allocated along the way must be passed on or freed at the end.
        still_ok = (kPayloadIdle == payload_state_ptr->payload_state ||
                    kPayloadInProgress == payload_state_ptr->payload_state ||
                    kPayloadPacketZeroPending == payload_state_ptr->payload_state);
    }

    // Check if we are receiving a new payload.
    CdiMemoryState* payload_memory_state_ptr = NULL;
    if (still_ok) {
        if (kPayloadIdle == payload_state_ptr->payload_state) {
            // Create state data for a new payload.
            still_ok = InitializePayloadState(protocol_handle, endpoint_ptr, packet_ptr, payload_state_ptr,
                                              &decoded_header, &payload_memory_state_ptr);
        } else {
            if (kPayloadPacketZeroPending == payload_state_ptr->payload_state &&
                0 == packet_sequence_num) {
                UpdatePayloadStateDataFromCDIPacket0(payload_state_ptr, &decoded_header);
            }
            // Using state data for an existing in progress payload.
            payload_memory_state_ptr = payload_state_ptr->work_request_state.payload_memory_state_ptr;

            if (kCdiSgl == con_state_ptr->rx_state.config_data.rx_buffer_type) {
                // Send the Rx packet SGL to the packet re-orderer. It will determine if the entry was used or cached.
                // The packet reordering logic does not need to be invoked if the connection was configured for a linear
                // receive buffer.
                still_ok = RxReorderPacket(protocol_handle, con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                           con_state_ptr->rx_state.reorder_entries_pool_handle, payload_state_ptr,
                                           &packet_ptr->sg_list, cdi_header_size, packet_sequence_num);
            }
        }
    }

    if (still_ok && kCdiLinearBuffer == con_state_ptr->rx_state.config_data.rx_buffer_type &&
             NULL != payload_state_ptr->linear_buffer_ptr) {
        // Gather this packet into the linear receive buffer.
        still_ok = CopyToLinearBuffer(con_state_ptr, packet_ptr, payload_state_ptr, &decoded_header);
    }

    if (!still_ok && payload_state_ptr &&
                     ((kPayloadInProgress        == payload_state_ptr->payload_state) ||
                      (kPayloadPacketZeroPending == payload_state_ptr->payload_state))) {
        // An error occurred so set payload error.
        RxReorderPayloadError(endpoint_ptr, payload_state_ptr);
    }

    if (still_ok && kPayloadInProgress == payload_state_ptr->payload_state &&
        payload_state_ptr->data_bytes_received >= payload_state_ptr->expected_payload_data_size) {
        // The entire payload has been received, so finalize it and add it to the payload reordering list in the correct
        // order.
        still_ok = FinalizePayload(con_state_ptr, payload_state_ptr);
        payload_state_ptr->payload_state = kPayloadComplete;
        if (still_ok) {
            if (kCdiBackPressureNone != con_state_ptr->back_pressure_state) {
                // Successfully received a payload and had back pressure. In order to prevent Rx payload reorder logic
                // from waiting for a payload that may have been thrown away, advance the current window index to the
                // first payload.
                RxReorderPayloadSeekFirstPayload(endpoint_ptr);
                con_state_ptr->back_pressure_state = kCdiBackPressureNone; // Reset back pressure state.
            }
        }
    }

    // Decide what to do with the incoming packet's SGL.
    if (still_ok && kCdiSgl == con_state_ptr->rx_state.config_data.rx_buffer_type) {
        // In SGL mode (SGL packet buffer is being directly used). Append the head of Rx packet SGL list to the tail
        // of the endpoint buffer SGL list. This will append the entire list to the buffer SGL. This list is used
        // later to free the buffers in the adapter via the application's call to CdiCoreRxFreeBuffer(), which uses
        // CdiAdapterFreeBuffer().
        // NOTE: The size of the endpoint SGL list is updated in SglMoveEntries().
        SglMoveEntries(&payload_memory_state_ptr->endpoint_packet_buffer_sgl, &packet_ptr->sg_list);
    } else {
        // The SGL passed in to the function was not consumed. Send it back to the adapter now.
        CdiAdapterFreeBuffer(endpoint_ptr->adapter_endpoint_ptr, &packet_ptr->sg_list);
    }

    if (still_ok) {
        payload_state_ptr->last_total_packet_count = endpoint_ptr->rx_state.total_packet_count;
        payload_state_ptr->packet_count++;

        // Packet is ok (no errors), so increment Rx reorder buffered packet counter.
        endpoint_ptr->rx_state.rxreorder_buffered_packet_count++;
    } else if (kCdiBackPressureActive == con_state_ptr->back_pressure_state) {
        QueueBackPressurePayloadToApp(con_state_ptr, endpoint_ptr, &decoded_header);
    }

    // Always increment total Rx packet counter (packet was actually received) and check if any payloads are ready to
    // send.
    endpoint_ptr->rx_state.total_packet_count++;
    RxReorderPayloadSendReadyPayloads(endpoint_ptr);
}

void RxSendPayload(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr)
{
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    // Update payload statistics data.
    UpdatePayloadStats(endpoint_ptr, &payload_state_ptr->work_request_state);

    // Add the Rx payload SGL message to the AppCallbackPayloadThread() queue.
    if (!CdiQueuePush(con_state_ptr->rx_state.active_payload_complete_queue_handle,
                      (void*)&payload_state_ptr->work_request_state.app_payload_cb_data)) {
        CDI_LOG_THREAD(kLogError, "Queue[%s] full, push failed.",
                       CdiQueueGetName(con_state_ptr->rx_state.active_payload_complete_queue_handle));

        // If payload is in state kPayloadComplete, its resources need to be freed. If in one of the other states, the
        // payload's resources have already been freed or no resources have been allocated.
        if (payload_state_ptr->payload_state == kPayloadComplete) {
            RxFreePayloadResources(endpoint_ptr, payload_state_ptr, true);
        }
        PayloadErrorFreeBuffer(con_state_ptr->error_message_pool,
                               &payload_state_ptr->work_request_state.app_payload_cb_data);
    } else {
        // Queue passes a copy of app_payload_cb_data to AppCallbackPayloadThread(), which frees the buffer. So
        // set the pointer to NULL here, so it doesn't get re-used.
        payload_state_ptr->work_request_state.app_payload_cb_data.error_message_str = NULL;
    }
}

void RxFreePayloadResources(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr, bool free_memory_state)
{
    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    CdiSgList* payload_sgl_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data.payload_sgl;

    // Free adapter Rx packet buffer resources.
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)payload_sgl_ptr->internal_data_ptr;
    if (memory_state_ptr) {
        CdiAdapterFreeBuffer(endpoint_ptr->adapter_endpoint_ptr, &memory_state_ptr->endpoint_packet_buffer_sgl);
        memset(&memory_state_ptr->endpoint_packet_buffer_sgl, 0, sizeof(memory_state_ptr->endpoint_packet_buffer_sgl));
    }

    // Now safe to free payload resources.
    FreePayloadBuffer(payload_sgl_ptr);

    if (free_memory_state && memory_state_ptr) {
        // Free payload memory_state_ptr. NOTE: payload_sgl_ptr->internal_data_ptr will be cleared.
        FreeMemoryState(payload_sgl_ptr);
        memory_state_ptr = NULL;
    }

    // Free Rx-reorder lists.
    RxReorderPacketFreeLists(payload_state_ptr->reorder_list_ptr,
                             con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                             con_state_ptr->rx_state.reorder_entries_pool_handle);
    payload_state_ptr->reorder_list_ptr = NULL; // List freed and no longer valid, so clear it.

    // Clear SGL sent to application's Rx callback. Don't clear internal_data_ptr here (see logic above).
    app_payload_cb_data_ptr->payload_sgl.sgl_head_ptr = NULL;
    app_payload_cb_data_ptr->payload_sgl.sgl_tail_ptr = NULL;
    app_payload_cb_data_ptr->payload_sgl.total_data_size = 0;
}

void RxInvokeAppPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr)
{
    // Setup core callback data.
    CdiCoreCbData core_cb_data = {
        .status_code = app_cb_data_ptr->payload_status_code,
        .err_msg_str = app_cb_data_ptr->error_message_str,
        .connection_handle = (CdiConnectionHandle)con_state_ptr,
        .user_cb_param = con_state_ptr->rx_state.config_data.user_cb_param,
        .core_extra_data = app_cb_data_ptr->core_extra_data,
    };

    // Check which protocol the connection is using.
    if (kProtocolTypeRaw == con_state_ptr->protocol_type) {
        // Raw connection.
        InvokeRawPayloadCallback(con_state_ptr, app_cb_data_ptr, &core_cb_data);
    } else {
        // AVM connection.
        InvokeAvmPayloadCallback(con_state_ptr, app_cb_data_ptr, &core_cb_data);
    }
}

CdiReturnStatus RxEnqueueFreeBuffer(const CdiSgList* sgl_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.

    CdiReturnStatus rs = kCdiStatusOk;
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)sgl_ptr->internal_data_ptr;
    CdiEndpointState* endpoint_ptr = memory_state_ptr->cdi_endpoint_handle;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    if (kHandleTypeRx != endpoint_ptr->connection_state_ptr->handle_type) {
        return kCdiStatusWrongDirection;
    }
    if (kCdiConnectionStatusConnected != endpoint_ptr->adapter_endpoint_ptr->connection_status_code) {
        // Currently not connected, so no need to free pending resources. All resources have already been freed
        // internally when the connection was disconnected.
        return kCdiStatusOk;
    }

    // Add the free buffer message into the Rx free buffer queue processing by PollThread().
    if (!CdiQueuePush(endpoint_ptr->rx_state.free_buffer_queue_handle, sgl_ptr)) {
        rs = kCdiStatusQueueFull;
    }

    // If adapter endpoint does not support polling, then signal the poll thread to do work so it can process freeing
    // payload and adapter packet buffers.
    bool is_poll = (NULL != con_state_ptr->adapter_state_ptr->functions_ptr->Poll);
    if (!is_poll) {
        CdiOsSignalSet(endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->poll_do_work_signal);
    }

    return rs;
}

bool RxPollFreeBuffer(void* param_ptr, CdiSgList* ret_packet_buffer_sgl_ptr)
{
    bool ret = false; // Default to false, nothing in the queue.
    CdiEndpointHandle handle = (CdiEndpointHandle)param_ptr;
    if (handle) {
        CdiSgList sgl_packets = { 0 };

        // Walk through all the entries in the queue, so we can free them all at once.
        CdiSgList sgl_payload;
        while (CdiQueuePop(handle->rx_state.free_buffer_queue_handle, (void*)&sgl_payload)) {
            CdiMemoryState* memory_state_ptr = (CdiMemoryState*)sgl_payload.internal_data_ptr;

            if (memory_state_ptr->endpoint_packet_buffer_sgl.sgl_head_ptr) {
                // Append the endpoint packet SGL to the list that will be returned.
                SglMoveEntries(&sgl_packets, &memory_state_ptr->endpoint_packet_buffer_sgl);
                ret = true;
            }
            // Now safe to free payload resources and memory_state_ptr. NOTE: sgl_payload.internal_data_ptr will be
            // cleared.
            FreePayloadBuffer(&sgl_payload);
            FreeMemoryState(&sgl_payload);
            memory_state_ptr = NULL; // Pointer is no longer valid, so clear it to prevent future accidental use.
        }

        if (ret) {
            // Copy the packet buffer SGL to the address specified.
            *ret_packet_buffer_sgl_ptr = sgl_packets;
        }
    }

    return ret;
}
