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

#include <stdlib.h> // For llabs()
#include <string.h>

#include "adapter_api.h"
#include "cdi_logger_api.h"
#include "cdi_pool_api.h"
#include "endpoint_manager.h"
#include "internal.h"
#include "private.h"
#include "private_avm.h"
#include "rx_reorder.h"
#include "statistics.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief A macro for finding the range of values of a variable (i.e. uint8_t is 256).
#define GET_RANGE(var) (1<<(sizeof(var)<<3))

/// @brief The length of time between received payloads is captured using both UTC and monotonic clocks. If the time
/// difference between them exceeds this value in microseconds, then the monotonic value will be used instead of the UTC
/// value.
#define ELAPSED_UTC_TIME_TOLERANCE_US   (100)

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
 * Update payload state when header packet sequence number 0 arrives.
 *
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param common_hdr_ptr Pointer to CDI header that contains data to be added to payload state.
 * @param cdi_header_size_ptr Pointer to memory space updated to size of header + user specified extra_data_size
 */
static void UpdatePayloadStateDataFromCDIPacket0(RxPayloadState* payload_state_ptr,
                                                   CdiCDIPacketCommonHeader* common_hdr_ptr,
                                                   int* cdi_header_size_ptr)
{
    // Got packet #0. Initialize payload state from data in packet sequence number zero's CDI header.
    CdiCDIPacketNum0Header* num0_hdr_ptr = (CdiCDIPacketNum0Header*)common_hdr_ptr;

    payload_state_ptr->payload_num = num0_hdr_ptr->hdr.payload_num;
    payload_state_ptr->expected_payload_data_size = num0_hdr_ptr->total_payload_size;
    payload_state_ptr->work_request_state.max_latency_microsecs = num0_hdr_ptr->max_latency_microsecs;

    // Size of header includes number of extra data bytes, if any.
    *cdi_header_size_ptr = sizeof(CdiCDIPacketNum0Header) + num0_hdr_ptr->extra_data_size;

    // Update work request data.
    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;
    app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp = num0_hdr_ptr->origination_ptp_timestamp;
    app_payload_cb_data_ptr->core_extra_data.payload_user_data = num0_hdr_ptr->payload_user_data;

    // Save the extra data in the work request.
    app_payload_cb_data_ptr->extra_data_size = num0_hdr_ptr->extra_data_size;
    if (num0_hdr_ptr->extra_data_size) {
        // We have extra data, so copy it to our work request.
        memcpy(app_payload_cb_data_ptr->extra_data_array, (uint8_t*)num0_hdr_ptr + sizeof(*num0_hdr_ptr),
               num0_hdr_ptr->extra_data_size);
    }
    payload_state_ptr->payload_state = kPayloadInProgress; // Advance payload state
}

/**
 * Initializes the state data for a payload. Call this when the first packet of a payload is received.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param packet_ptr Pointer to the first packet received for a given payload.
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param common_hdr_ptr Pointer to CDI header that contains data to be added to payload state.
 * @param cdi_header_size_ptr Pointer to memory space updated to size of header + user specified extra_data_size.
 * @param payload_memory_state_ptr Pointer to location to write the address of a pool allocated CdiMemoryState
 *                                 structure.
 *
 * @return true if this function completed successfully, false if a problem was encountered.
 */
static bool InitializePayloadState(CdiEndpointState* endpoint_ptr, const Packet* packet_ptr,
                                   RxPayloadState* payload_state_ptr, CdiCDIPacketCommonHeader* common_hdr_ptr,
                                   int* cdi_header_size_ptr, CdiMemoryState** payload_memory_state_ptr)
{
    bool ret = true;
    uint64_t start_time = CdiOsGetMicroseconds();
    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    // Create state data for a new payload.
    CdiMemoryState* memory_state_ptr = NULL;
    // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
    // them at a time. This function is only called by PoolThread().
    if (!CdiPoolGet(con_state_ptr->rx_state.payload_memory_state_pool_handle, (void**)&memory_state_ptr)) {
        CDI_LOG_THREAD(kLogError, "Failed to get CdiMemoryState from pool. Throwing away this payload[%d]. "
                       "Timestamp[%u:%u]", payload_state_ptr->payload_num,
                       app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                       app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds);
        ret = false;
    } else {
        *payload_memory_state_ptr = memory_state_ptr;

        // Initialize memory state data.
        memory_state_ptr->magic = kMagicMem;
        memory_state_ptr->cdi_endpoint_handle = endpoint_ptr;
        memory_state_ptr->buffer_type = con_state_ptr->rx_state.config_data.rx_buffer_type;

        // Initialize Rx endpoint packet SGL buffer list.
        memset(&memory_state_ptr->endpoint_packet_buffer_sgl, 0, sizeof(memory_state_ptr->endpoint_packet_buffer_sgl));

        // Initialize work request state data.
        app_payload_cb_data_ptr->payload_status_code = kCdiStatusOk;
        app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds = 0;
        app_payload_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds = 0;
        app_payload_cb_data_ptr->core_extra_data.payload_user_data = 0;
        payload_state_ptr->work_request_state.start_time = start_time;
        // Store pointer to memory state in the payload state data.
        payload_state_ptr->work_request_state.payload_memory_state_ptr = memory_state_ptr;

        // Initialize payload state data.
        CdiSgList* payload_sgl_list_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data.payload_sgl;
        memset(payload_sgl_list_ptr, 0, sizeof(*payload_sgl_list_ptr));
        payload_sgl_list_ptr->internal_data_ptr = payload_state_ptr->work_request_state.payload_memory_state_ptr;

        payload_state_ptr->payload_num = 0;
        payload_state_ptr->packet_sequence_num = 0;
        payload_state_ptr->data_bytes_received = 0;
        payload_state_ptr->expected_payload_data_size = 0;
        payload_state_ptr->reorder_list_ptr = NULL;

        if (0 == common_hdr_ptr->packet_sequence_num) {
            UpdatePayloadStateDataFromCDIPacket0(payload_state_ptr, common_hdr_ptr, cdi_header_size_ptr);
        } else {
            payload_state_ptr->payload_state = kPayloadPacketZeroPending; // Advance payload state to expecting 0.
            // Still need to set the payload number since we have logic looking for in-progress payloads that have the
            // wrong payload number at the front end of RxPacketReceive().
            payload_state_ptr->payload_num = common_hdr_ptr->payload_num;
        }

        if (con_state_ptr->rx_state.config_data.rx_buffer_type == kCdiLinearBuffer) {
            if (!CdiPoolGet(con_state_ptr->linear_buffer_pool, (void*)&payload_state_ptr->linear_buffer_ptr)) {
                // Ensure this is NULL if the pool ran dry. This error condition will be reported to the application
                // through the callback made when the payload has been completely received.
                payload_state_ptr->linear_buffer_ptr = NULL;
            }
        } else {
            payload_state_ptr->linear_buffer_ptr = NULL;

            ret = CdiRxPayloadReorderStateInit(con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                               con_state_ptr->rx_state.reorder_entries_pool_handle,
                                               payload_state_ptr, &packet_ptr->sg_list,
                                               *cdi_header_size_ptr, common_hdr_ptr->packet_sequence_num);
        }
        memory_state_ptr->payload_state_ptr = payload_state_ptr;
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
 * @param common_hdr_ptr Pointer to CDI header that contains data to be added to payload state.
 * @param cdi_header_size CDI header size in bytes.
 *
 * @return true if the function completed successfully, false if a problem was encountered.
 */
static bool CopyToLinearBuffer(CdiConnectionState* con_state_ptr, const Packet* packet_ptr,
                               RxPayloadState* const payload_state_ptr, CdiCDIPacketCommonHeader* common_hdr_ptr,
                               int cdi_header_size)
{
    bool ret = true;

    // Using linear memory buffer.
    const int mtu = con_state_ptr->adapter_state_ptr->maximum_payload_bytes;
    int offset = 0;
    // Packet #0 never contains a data offset, since the offset is always zero (its the first packet of a payload).
    if (common_hdr_ptr->packet_sequence_num != 0) {
        if (kPayloadTypeDataOffset == common_hdr_ptr->payload_type) {
            // Using data offset, so get the value from the CDI header.
            CdiCDIPacketDataOffsetHeader *ptr = (CdiCDIPacketDataOffsetHeader *)common_hdr_ptr;
            offset = ptr->payload_data_offset;
        } else {
            // Compute the offset into payload based on the MTU, packet number, and the two possible header types'
            // sizes.
            offset = (mtu - sizeof(CdiCDIPacketNum0Header) +
                     (mtu - sizeof(CdiCDIPacketCommonHeader)) * (common_hdr_ptr->packet_sequence_num - 1));
        }
    }

    const int byte_count = packet_ptr->sg_list.total_data_size - cdi_header_size;

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
        const int bytes_gathered = CdiCoreGather(&packet_ptr->sg_list, cdi_header_size,
                                                 payload_state_ptr->linear_buffer_ptr + offset, byte_count);
        assert(bytes_gathered <= byte_count);
        payload_state_ptr->data_bytes_received += bytes_gathered;
    }

    return ret;
}

/**
 * Free resources specific to a payload. Adapter packet resources are freed separately.
 *
 * @param sgl_ptr Pointer to payload scatter-gather list.
 */
static void FreePayloadBuffer(CdiSgList* sgl_ptr)
{
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)sgl_ptr->internal_data_ptr;

    // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
    // them at a time. This function is only called by PoolThread().
    CdiConnectionState* con_state_ptr = memory_state_ptr->cdi_endpoint_handle->connection_state_ptr;

    if (memory_state_ptr->buffer_type == kCdiLinearBuffer) {
        // Return the linear buffer to its pool; its address is in the singular SGL entry.
        if (sgl_ptr->sgl_head_ptr && sgl_ptr->sgl_head_ptr->address_ptr) {
            CdiPoolPut(con_state_ptr->linear_buffer_pool, sgl_ptr->sgl_head_ptr->address_ptr);
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

#ifdef DEBUG_RX_PAYLOAD_SGL_ENTRY_FREE_COUNT
    int post_free_count = CdiPoolGetFreeItemCount(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
    SDK_LOG_GLOBAL(kLogDebug, "Rx Payload Free. Pre[%d] Post[%d]", pre_free_count, post_free_count);
#endif

    // Return memory state to pool.
    CdiPoolPut(con_state_ptr->rx_state.payload_memory_state_pool_handle, memory_state_ptr);
    sgl_ptr->internal_data_ptr = NULL; // Pointer is no longer valid, so clear it to prevent future accidental use.
}

/**
 * Free payload resources.
 *
 * @param endpoint_ptr Pointer to connection state structure.
 * @param payload_state_ptr Pointer to payload structure being updated.
 */
static void FreePayloadResources(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr)
{
    AppPayloadCallbackData* app_payload_cb_data_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    CdiSgList* payload_sgl_ptr = &payload_state_ptr->work_request_state.app_payload_cb_data.payload_sgl;

    // Free adapter Rx packet buffer resources.
    CdiMemoryState* memory_state_ptr = (CdiMemoryState*)payload_sgl_ptr->internal_data_ptr;
    CdiAdapterFreeBuffer(endpoint_ptr->adapter_endpoint_ptr, &memory_state_ptr->endpoint_packet_buffer_sgl);

    // Now safe to free payload resources and memory_state_ptr. NOTE: payload_sgl_ptr->internal_data_ptr will be
    // cleared.
    FreePayloadBuffer(payload_sgl_ptr);
    memory_state_ptr = NULL; // Pointer is no longer valid, so clear it to prevent future accidental use.

    // Free Rx-reorder lists.
    CdiRxReorderFreeLists(payload_state_ptr->reorder_list_ptr,
                          con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                          con_state_ptr->rx_state.reorder_entries_pool_handle);

    // Clear SGL sent to application's Rx callback.
    memset(&app_payload_cb_data_ptr->payload_sgl, 0, sizeof(CdiSgList));
}

/**
 * Reset payload state data.
 *
 * @param payload_state_ptr Pointer to payload state data to reset.
 */
static void ResetPayloadState(RxPayloadState* payload_state_ptr)
{
    // Reset payload state.
    payload_state_ptr->payload_state = kPayloadIdle; // Reset back to idle state
    payload_state_ptr->packet_sequence_num = 0;
    payload_state_ptr->data_bytes_received = 0;
    payload_state_ptr->suspend_warnings = false;
}

/**
 * Finalizes the payload state. Call this once all data expected for the payload has been received.
 *
 * @param con_state_ptr Pointer to connection state structure.
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param common_hdr_ptr Pointer to CDI header that contains data to be added to payload state.
 *
 * @return Returns true if payload successfully received without any packet reorder issues, otherwise false is
 *         returned.
 */
static bool FinalizePayload(CdiConnectionState* con_state_ptr, RxPayloadState* payload_state_ptr,
                            CdiCDIPacketCommonHeader* common_hdr_ptr)
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
        if (NULL != payload_state_ptr->reorder_list_ptr->next_ptr &&
                NULL != payload_state_ptr->reorder_list_ptr->prev_ptr) {
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
            CdiRxReorderFreeLists(payload_state_ptr->reorder_list_ptr,
                                  con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                  con_state_ptr->rx_state.reorder_entries_pool_handle);
            ret = false;
        }

        // Update SGL's total data size and pointers
        CdiSgList* sgl_ptr = &app_payload_cb_data_ptr->payload_sgl;
        sgl_ptr->total_data_size = payload_state_ptr->data_bytes_received;
        sgl_ptr->sgl_head_ptr = payload_state_ptr->reorder_list_ptr->sglist.sgl_head_ptr;
        sgl_ptr->sgl_tail_ptr = payload_state_ptr->reorder_list_ptr->sglist.sgl_tail_ptr;

        // Free the reorder list memory entry.
        CdiPoolPut(con_state_ptr->rx_state.reorder_entries_pool_handle, payload_state_ptr->reorder_list_ptr);
        payload_state_ptr->reorder_list_ptr = NULL;
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
#ifdef DEBUG_INTERNAL_SGL_ENTRIES
                entry_ptr->packet_sequence_num = common_hdr_ptr->packet_sequence_num;
                entry_ptr->payload_num = common_hdr_ptr->payload_num;
#endif  // DEBUG_INTERNAL_SGL_ENTRIES
                app_payload_cb_data_ptr->payload_sgl.total_data_size = entry_ptr->size_in_bytes;
                app_payload_cb_data_ptr->payload_sgl.sgl_head_ptr = entry_ptr;
                app_payload_cb_data_ptr->payload_sgl.sgl_tail_ptr = entry_ptr;
            }
        }
    }

    return ret;
}

/**
 * Send the payload on to the next stage because it is complete or determined to be in error.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param send_payload_state_ptr Pointer to the payload state for the completed payload.
 */
static void RxSendPayload(CdiEndpointState* endpoint_ptr, RxPayloadState* send_payload_state_ptr)
{
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    // Update payload statistics data.
    UpdatePayloadStats(endpoint_ptr, &send_payload_state_ptr->work_request_state);

    // Add the Rx payload SGL message to the AppCallbackPayloadThread() queue.
    if (!CdiQueuePush(con_state_ptr->app_payload_message_queue_handle,
                      (void*)&send_payload_state_ptr->work_request_state.app_payload_cb_data)) {
        CDI_LOG_THREAD(kLogError, "Queue[%s] full, push failed.",
                       CdiQueueGetName(con_state_ptr->app_payload_message_queue_handle));

        // If payload is in state kPayloadComplete, its resources need to be freed above. If in one of the other states,
        // the payload's resources have already been freed or no resources have been allocated.
        if (send_payload_state_ptr->payload_state == kPayloadComplete) {
            FreePayloadResources(endpoint_ptr, send_payload_state_ptr);
        }
        PayloadErrorFreeBuffer(con_state_ptr, &send_payload_state_ptr->work_request_state.app_payload_cb_data);
    } else {
        // Queue passes a copy of app_payload_cb_data to AppCallbackPayloadThread(), which frees the buffer. So
        // set the pointer to NULL here, so it doesn't get re-used.
        send_payload_state_ptr->work_request_state.app_payload_cb_data.error_message_str = NULL;
    }
}

/**
 * Flush the entire payload state array. NULL entries are ignored as are those in an ignore state. Payloads that have
 * received a packet, are complete, or are marked as erred are sent on to the application. Payloads found in the idle
 * state also result in an error callback to the application.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 */
static void RxSendAllPayloads(CdiEndpointState* endpoint_ptr)
{
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    // Visit every slot in the window ignoring any entries in the payload state pointer array along the way.
    for (int i = 0; i < MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER; i++) {
        const uint8_t idx = endpoint_ptr->rx_state.payload_num_window_min + i;
        RxPayloadState* send_payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[idx];
        if (NULL != send_payload_state_ptr) {
            // If this payload is in progress, clean-up resources and notify the application that there was a Rx payload
            // error. Set the payload state to kPayloadError so any more packets that arrive as part of this payload
            // will not be processed.
            if ((send_payload_state_ptr->payload_state == kPayloadInProgress) ||
                (send_payload_state_ptr->payload_state == kPayloadPacketZeroPending)) {
                // An error occurred so clean-up resources and set the payload state to kPayloadError.
                FreePayloadResources(endpoint_ptr, send_payload_state_ptr);

                // Ensure an error message and status have been set.
                PAYLOAD_ERROR(con_state_ptr, &send_payload_state_ptr->work_request_state.app_payload_cb_data,
                              kCdiStatusRxPayloadError, "Connection[%s] Rx packet error occurred.",
                              con_state_ptr->saved_connection_name_str);
                send_payload_state_ptr->payload_state = kPayloadError;
            }

            // If we are flushing the array, it is because we received an out-of-range payload, so we assume the current
            // array location's payload number has been lost and we report that condition and set this entry up as an
            // error condition which gets reported back to the application.  This is a "best effort" approach to giving
            // the application some information about what may have gone wrong under this unexpected circumstance.
            if (send_payload_state_ptr->payload_state == kPayloadIdle) {
                PAYLOAD_ERROR(con_state_ptr, &send_payload_state_ptr->work_request_state.app_payload_cb_data,
                              kCdiStatusRxPayloadError, "Connection[%s] No packets were received for payload[%d].",
                              con_state_ptr->saved_connection_name_str, endpoint_ptr->rx_state.payload_num_window_min);
                send_payload_state_ptr->work_request_state.app_payload_cb_data.payload_status_code =
                               kCdiStatusRxPayloadMissing;
                send_payload_state_ptr->payload_state = kPayloadError;
            }

            if (send_payload_state_ptr->payload_state == kPayloadComplete ||
                send_payload_state_ptr->payload_state == kPayloadError) {
                RxSendPayload(endpoint_ptr, send_payload_state_ptr);
                endpoint_ptr->rx_state.payload_state_array_ptr[idx] = NULL;
            } else {
                send_payload_state_ptr->payload_state = kPayloadIgnore;
            }
        }
    }
}

/**
 * Starting at the beginning of the payload state array, sends any payloads that are complete or in an error state. Stop
 * when either a NULL or not ready payload is encountered or the end of the array is reached.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 */
static void RxSendReadyPayloads(CdiEndpointState* endpoint_ptr)
{
    // Start at window minimum and go until either the maximum has been reached or the payload at the minimum is not
    // ready to send.
    uint8_t idx = endpoint_ptr->rx_state.payload_num_window_min;
    const uint8_t end_idx = idx + MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER;  // 1 beyond the end of the window
    RxPayloadState** payload_state_array = endpoint_ptr->rx_state.payload_state_array_ptr;  // alias
    while (idx != end_idx && NULL != payload_state_array[idx] &&
           (kPayloadError == payload_state_array[idx]->payload_state ||
            kPayloadComplete == payload_state_array[idx]->payload_state)) {
        RxSendPayload(endpoint_ptr, payload_state_array[idx]);
        payload_state_array[idx++] = NULL;
    }
    endpoint_ptr->rx_state.payload_num_window_min = idx;
}

/**
 * Reset the payload base time values and sleep for the connection's buffer delay period. This delay allows received
 * payloads to be buffered as they are added to the app payload message queue.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param current_monotonic_time_us Current monotonic time in microseconds. Must be obtained using
 *                                  CdiOsGetMicroseconds().
 * @param current_utc_us Current UTC time in microseconds. Must be obtained using CdiCoreGetUtcTimeMicroseconds().
 * @param payload_timestamp_us Current payload's timestamp converted into microseconds.
 * @param buffer_time_us Amount of time to buffer in microseconds.
 */
static void RestartBufferedDelay(CdiConnectionState* con_state_ptr, uint64_t current_monotonic_time_us,
                                 uint64_t current_utc_us, uint64_t payload_timestamp_us, uint64_t buffer_time_us)
{

    con_state_ptr->rx_state.payload_base_monotonic_time_us = current_monotonic_time_us + buffer_time_us;
    con_state_ptr->rx_state.payload_base_utc_us = current_utc_us + buffer_time_us;
    con_state_ptr->rx_state.payload_base_timestamp_us = payload_timestamp_us;
    CdiOsSleepMicroseconds(buffer_time_us);
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
    con_state_ptr->magic = kMagicCon;
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

    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Connection Rx CdiMemoryState Pool", max_rx_payloads,
                           MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION_GROW, MAX_POOL_GROW_COUNT,
                           sizeof(CdiMemoryState), true, // true= Make thread-safe
                           &con_state_ptr->rx_state.payload_memory_state_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    int reserve_packet_buffers = MAX_RX_PACKETS_PER_CONNECTION;
    if (con_state_ptr->rx_state.config_data.buffer_delay_ms) {
        // Rx buffer delay is enabled, so we need to allocate additional Rx buffers.
        reserve_packet_buffers += (MAX_RX_PACKETS_PER_CONNECTION * con_state_ptr->rx_state.config_data.buffer_delay_ms) /
                                  RX_BUFFER_DELAY_BUFFER_MS_DIVISOR;
        CdiListInit(&con_state_ptr->rx_state.ptp_ordered_payload_list);
        if (kCdiStatusOk == rs) {
            int pool_items = (max_rx_payloads *
                             con_state_ptr->rx_state.config_data.buffer_delay_ms) / RX_BUFFER_DELAY_BUFFER_MS_DIVISOR;
            if (!CdiPoolCreate("Connection RxOrdered AppPayloadCallbackData Pool", pool_items, NO_GROW_SIZE,
                               NO_GROW_COUNT, sizeof(AppPayloadCallbackData), false, // false= Not thread-safe.
                            &con_state_ptr->rx_state.ordered_payload_pool_handle)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }
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
        // Payload state pool.
        if (!CdiPoolCreate("Rx Payload State Pool", max_rx_payloads, NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(RxPayloadState), true,
                           &con_state_ptr->rx_state.rx_payload_state_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

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

void RxEndpointFlushResources(CdiEndpointState* endpoint_ptr)
{
    if (endpoint_ptr) {
        // Walk through the list of payload state data and see if any payloads were in the process of being received. If
        // so, set an error.
        for (int i = 0; i < (int)CDI_ARRAY_ELEMENT_COUNT(endpoint_ptr->rx_state.payload_state_array_ptr); i++) {
            RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[i];
            if (NULL != payload_state_ptr ) {
                if (kPayloadIdle != payload_state_ptr->payload_state &&
                    kPayloadIgnore != payload_state_ptr->payload_state &&
                    kPayloadError != payload_state_ptr->payload_state) {
                    // Free payload resources. Also frees entry for linear_buffer_pool (if used).
                    FreePayloadResources(endpoint_ptr, payload_state_ptr);
                }
                CdiPoolPut(endpoint_ptr->connection_state_ptr->rx_state.rx_payload_state_pool_handle, payload_state_ptr);
                endpoint_ptr->rx_state.payload_state_array_ptr[i] = NULL;
            }
        }

        // Clear payload reordering tracking.
        endpoint_ptr->rx_state.payload_num_window_min = 0;

        // NOTE: This pool only contains pool buffers (so nothing else needs to be freed).
        CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
        CdiPoolPutAll(con_state_ptr->rx_state.reorder_entries_pool_handle);

        // NOTE: The queue free_buffer_queue_handle is flushed using RxPollFreeBuffer(), called from the adapter.

        // NOTE: This pool only contains pool buffers (so nothing else needs to be freed).
        CdiPoolPutAll(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);

        // NOTE: This pool may contain SGL that point to adapter Rx buffers. However, the adapter directly frees the Rx
        // buffers (so nothing else needs to be freed).
        CdiPoolPutAll(con_state_ptr->rx_state.payload_memory_state_pool_handle);

        // NOTE: This pool only contains pool buffers (so nothing else needs to be freed).
        CdiPoolPutAll(con_state_ptr->rx_state.ordered_payload_pool_handle);
        CdiListInit(&con_state_ptr->rx_state.ptp_ordered_payload_list);
    }
}

void RxConnectionDestroyInternal(CdiConnectionHandle con_handle)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)con_handle;

    if (con_state_ptr) {
        // Now that the connection and adapter threads have stopped, it is safe to clean up the remaining resources in
        // the opposite order of their creation.
        CdiPoolDestroy(con_state_ptr->rx_state.rx_payload_state_pool_handle);
        con_state_ptr->rx_state.rx_payload_state_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->linear_buffer_pool);
        con_state_ptr->linear_buffer_pool = NULL;

        CdiPoolDestroy(con_state_ptr->rx_state.reorder_entries_pool_handle);
        con_state_ptr->rx_state.reorder_entries_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
        con_state_ptr->rx_state.payload_sgl_entry_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->rx_state.ordered_payload_pool_handle);
        con_state_ptr->rx_state.ordered_payload_pool_handle = NULL;

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

void RxPacketReceive(void* param_ptr, Packet* packet_ptr)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)param_ptr;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    CdiCDIPacketCommonHeader* common_hdr_ptr = CdiPayloadParseCDIPacket(&packet_ptr->sg_list);
    assert(NULL != common_hdr_ptr);
    bool still_ok = true;

#ifdef DEBUG_PACKET_SEQUENCES
    CDI_LOG_THREAD(kLogInfo, "T[%d] P[%3d] S[%3d] A[%p]", common_hdr_ptr->payload_type, common_hdr_ptr->payload_num,
                   common_hdr_ptr->packet_sequence_num, packet_ptr->sg_list.sgl_head_ptr->address_ptr);
#endif

    int current_payload_index = common_hdr_ptr->payload_num;

    RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[current_payload_index];
    if (NULL == payload_state_ptr) {
        if (!CdiPoolGet(con_state_ptr->rx_state.rx_payload_state_pool_handle, (void**)&payload_state_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to get Rx Payload State entry from pool.");
            still_ok = false;
        } else {
            endpoint_ptr->rx_state.payload_state_array_ptr[current_payload_index] = payload_state_ptr;
            ResetPayloadState(payload_state_ptr);
        }
    }
    int cdi_header_size = sizeof(CdiCDIPacketCommonHeader);
    if (still_ok) {
        // If we received a packet for a new payload that is outside the range of what our payload state array can
        // handle to keep payloads in order, then we will flush the array of all currently outstanding payloads and'
        // reset the pointer to the new payload that we are receiving. This is a heavy hammer, but since this condition
        // should never happen, and if it does we don't know what to expect, we need to release resources for old
        // payloads and pick a new window.
        // In order to figure out what range to expect, we need to compare the incoming payload against our oldest
        // pending payload. Since payload numbers can wrap, we need to look for a condition where we are comparing near
        // a wrap point and deal with that condition if we find it.
        uint8_t payload_min = endpoint_ptr->rx_state.payload_num_window_min;
        uint8_t payload_max = payload_min + MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER - 1;

        // Test that we are within the buffer window. If not, issue an error message, flush the state array, and reset
        // the expected payload number to the current payload's number.
        bool in_range = true;
        if (payload_max < payload_min) { // wrap
            if (common_hdr_ptr->payload_num > payload_max && common_hdr_ptr->payload_num < payload_min) {
                in_range = false;
            }
        } else {
            if (common_hdr_ptr->payload_num < payload_min || common_hdr_ptr->payload_num > payload_max) {
                in_range = false;
            }
        }

        if (!in_range) {
            if (con_state_ptr->rx_state.received_first_payload) {
                CDI_LOG_THREAD(kLogError,
                               "Connection[%s] Received payload[%d] which is outside of the valid buffer range[%d] to"
                               " [%u]. Realigning buffer.", con_state_ptr->saved_connection_name_str,
                               common_hdr_ptr->payload_num, payload_min, payload_max);
            } else {
                CDI_LOG_THREAD(kLogDebug,
                               "Connection[%s] Received first payload and payload number is not zero. The first payload"
                               " number is [%u]. Realigning buffer range to first payload number.",
                               con_state_ptr->saved_connection_name_str, common_hdr_ptr->payload_num);

            }
            RxSendAllPayloads(endpoint_ptr);
            // Reset the expected payload number at this index to be the new payload number.
            endpoint_ptr->rx_state.payload_num_window_min = current_payload_index;
        }

        // No need to check if this is already set. If this code is being reached a first payload has been received.
        con_state_ptr->rx_state.received_first_payload = true;

        // If we get a packet for a completed payload, issue a warning, and then set the suspend_warnings flag so that we
        // don't keep issuing warnings if we get more packets for this same payload before it is sent to the application.
        if (!payload_state_ptr->suspend_warnings && (kPayloadComplete == payload_state_ptr->payload_state)) {
            CDI_LOG_THREAD(kLogWarning, "Connection[%s] Received packet for completed payload[%d].  Additional packets "
                                        "for this payload will be dropped.",
                                        con_state_ptr->saved_connection_name_str, common_hdr_ptr->payload_num);
            payload_state_ptr->suspend_warnings = true;
        }

        // If we have received a packet for a new payload that wants to use a payload state array location that is
        // marked Ignore, we can just take over this slot and set the state to Idle. We were either in the Ignore
        // state because we just got set to Ignore in the block above, or we got set to ignore previously to allow
        // us to pass over packets from a payload that was previously terminated because of error.
        if (kPayloadIgnore == payload_state_ptr->payload_state) {
            if (payload_state_ptr->payload_num != common_hdr_ptr->payload_num) {
                ResetPayloadState(payload_state_ptr);
            }
        }

        // Get size of CDI header. If packet #0, then logic below will calculate the header size.
        if (kPayloadTypeDataOffset == common_hdr_ptr->payload_type) {
            cdi_header_size = sizeof(CdiCDIPacketDataOffsetHeader);
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
            still_ok = InitializePayloadState(endpoint_ptr, packet_ptr, payload_state_ptr, common_hdr_ptr,
                                              &cdi_header_size, &payload_memory_state_ptr);
        } else {
            if (kPayloadPacketZeroPending == payload_state_ptr->payload_state &&
                0 == common_hdr_ptr->packet_sequence_num) {
                UpdatePayloadStateDataFromCDIPacket0(payload_state_ptr, common_hdr_ptr, &cdi_header_size);
            }
            // Using state data for an existing in progress payload.
            payload_memory_state_ptr = payload_state_ptr->work_request_state.payload_memory_state_ptr;

            if (kCdiSgl == con_state_ptr->rx_state.config_data.rx_buffer_type) {
                // Send the Rx packet SGL to the packet re-orderer. It will determine if the entry was used or cached.
                // The packet reordering logic does not need to be invoked if the connection was configured for a linear
                // receive buffer.
                still_ok = CdiRxReorder(con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                        con_state_ptr->rx_state.reorder_entries_pool_handle, payload_state_ptr,
                                        &packet_ptr->sg_list, cdi_header_size,
                                        common_hdr_ptr->packet_sequence_num);
            }
        }
    }

    if (still_ok && kCdiLinearBuffer == con_state_ptr->rx_state.config_data.rx_buffer_type &&
             NULL != payload_state_ptr->linear_buffer_ptr) {
        // Gather this packet into the linear receive buffer.
        still_ok = CopyToLinearBuffer(con_state_ptr, packet_ptr, payload_state_ptr, common_hdr_ptr, cdi_header_size);
    }

    if (!still_ok && (NULL != payload_state_ptr) &&
                     ((kPayloadInProgress        == payload_state_ptr->payload_state) ||
                      (kPayloadPacketZeroPending == payload_state_ptr->payload_state))) {
            // An error occurred so clean-up resources, notify the application that there was a Rx payload error. Enter
            // the payload ignore state so any more packets that arrive as part of this payload will not be processed.
            FreePayloadResources(endpoint_ptr, payload_state_ptr);

            // Ensure an error message and status have been set.
            PAYLOAD_ERROR(con_state_ptr, &payload_state_ptr->work_request_state.app_payload_cb_data,
                          kCdiStatusRxPayloadError, "Connection[%s] Rx packet error occurred.",
                          con_state_ptr->saved_connection_name_str);
            payload_state_ptr->payload_state = kPayloadError;
    }

    if (still_ok && kPayloadInProgress == payload_state_ptr->payload_state &&
        payload_state_ptr->data_bytes_received >= payload_state_ptr->expected_payload_data_size) {
        // The entire payload has been received, so finalize it and add it to the payload reordering list in the correct
        // order.
        still_ok = FinalizePayload(con_state_ptr, payload_state_ptr, common_hdr_ptr);
        payload_state_ptr->payload_state = kPayloadComplete;
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

    // Check to see if the current expected payload is ready to send. If it is, send it and then check the next one.
    // If we had gotten any payloads early, they will be waiting in the array too, so we can keep doing this until
    // we get to a payload index that has not yet been received, at which time we exit.
    RxSendReadyPayloads(endpoint_ptr);
}

void RxInvokeAppPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr)
{
    // Setup core callback data.
    CdiCoreCbData core_cb_data = {
        .status_code = app_cb_data_ptr->payload_status_code,
        .err_msg_str = app_cb_data_ptr->error_message_str,
        .connection_handle = (CdiConnectionHandle)con_state_ptr,
        .user_cb_param = con_state_ptr->rx_state.config_data.user_cb_param,
        .core_extra_data = app_cb_data_ptr->core_extra_data
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
            CdiPoolPut(handle->connection_state_ptr->rx_state.rx_payload_state_pool_handle,
                       memory_state_ptr->payload_state_ptr);
            // Pointer is no longer valid, so clear it to prevent future accidental use.
            memory_state_ptr->payload_state_ptr = NULL;

            // Now safe to free payload resources and memory_state_ptr. NOTE: sgl_payload.internal_data_ptr will be
            // cleared.
            FreePayloadBuffer(&sgl_payload);
            memory_state_ptr = NULL; // Pointer is no longer valid, so clear it to prevent future accidental use.
        }

        if (ret) {
            // Copy the packet buffer SGL to the address specified.
            *ret_packet_buffer_sgl_ptr = sgl_packets;
        }
    }

    return ret;
}

void RxBufferedPayloadAdd(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr)
{
    CdiList* list_ptr = &con_state_ptr->rx_state.ptp_ordered_payload_list;
    CdiListIterator list_iterator;
    CdiListIteratorInit(list_ptr, &list_iterator);
    CdiPtpTimestamp* new_ptp_ptr = &app_cb_data_ptr->core_extra_data.origination_ptp_timestamp;

    // Since we are holding the payload data in a list, make a copy of it and store in a pool buffer.
    AppPayloadCallbackData* pool_item_ptr = NULL;
    if (!CdiPoolGet(con_state_ptr->rx_state.ordered_payload_pool_handle, (void**)&pool_item_ptr)) {
        CDI_LOG_THREAD(kLogCritical,
            "Failed to get AppPayloadCallbackData from pool. Throwing away payload [%u:%u]",
            app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
            app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds);
    } else {
        *pool_item_ptr = *app_cb_data_ptr; // Make a copy of the item in the pool buffer.
        // Walk through the list looking for first entry with higher PTP value. If found, then insert the new entry
        // before it. Otherwise, the new entry is added to the end of the list.
        bool added = false;
        AppPayloadCallbackData* entry_ptr;
        while (NULL != (entry_ptr = (AppPayloadCallbackData*)CdiListIteratorGetNext(&list_iterator))) {
            CdiPtpTimestamp* entry_ptp_ptr = &entry_ptr->core_extra_data.origination_ptp_timestamp;
            if (new_ptp_ptr->seconds < entry_ptp_ptr->seconds ||
                ((new_ptp_ptr->seconds == entry_ptp_ptr->seconds) &&
                  new_ptp_ptr->nanoseconds < entry_ptp_ptr->nanoseconds)) {
                CdiListAddBefore(list_ptr, &pool_item_ptr->list_entry, &entry_ptr->list_entry);
                added = true;
                break;
            }
        }
        if (!added) {
            CdiListAddTail(list_ptr, &pool_item_ptr->list_entry);
        }
    }
}

bool RxBufferedPayloadGet(CdiConnectionState* con_state_ptr, uint32_t* ret_timeout_ms_ptr,
                          AppPayloadCallbackData* ret_app_cb_data_ptr)
{
    bool ret = false;
    uint64_t current_utc_us = CdiCoreGetUtcTimeMicroseconds(); // Get time used for PTP (UTC using realtime clock).
    uint64_t current_monotonic_time_us = CdiOsGetMicroseconds();
    CdiList* list_ptr = &con_state_ptr->rx_state.ptp_ordered_payload_list;

    AppPayloadCallbackData* app_cb_data_ptr = (AppPayloadCallbackData*)CdiListPeek(list_ptr);
    if (NULL == app_cb_data_ptr) {
        // Nothing in the list, so setup to wait until the next payload arrives in the caller's queue.
        *ret_timeout_ms_ptr = CDI_INFINITE;
        return false; // false= No payloads to process.
    }

    uint64_t payload_timestamp_us =
        CdiUtilityPtpTimestampToMicroseconds(&app_cb_data_ptr->core_extra_data.origination_ptp_timestamp);

    uint64_t buffer_delay_us = (uint64_t)con_state_ptr->rx_state.config_data.buffer_delay_ms * 1000;
    uint64_t elapsed_time = 0;
    bool restart = false;

#ifdef DEBUG_RX_BUFFER
    CDI_LOG_THREAD(kLogInfo, "RxTimestamp[%d.%d]", app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                   app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds);
#endif

    if (0 == con_state_ptr->rx_state.payload_base_utc_us) {
        restart = true;
    } else {
        int64_t elapsed_monotonic_time = current_monotonic_time_us -
                                         con_state_ptr->rx_state.payload_base_monotonic_time_us;
        assert(elapsed_monotonic_time > 0);
        int64_t elapsed_utc_time = current_utc_us - con_state_ptr->rx_state.payload_base_utc_us;
        assert(elapsed_utc_time > 0);

#ifdef DEBUG_RX_BUFFER
        int64_t diff = elapsed_utc_time - elapsed_monotonic_time;
        if (0 != diff) {
            CDI_LOG_THREAD(kLogInfo, "Mono Vs RT diff[%lld]", llabs(elapsed_utc_time - elapsed_monotonic_time));
        }
#endif
        // In order to prevent clock drift between our system clocks and the PTP timestamps, use the system clock based
        // on CLOCK_REALTIME (UTC time). We want to ensure the elapsed time has not jumped past our tolerance. If it
        // has, then use the monotonic elapsed time value for this interval.
        //
        // NOTE: UTC time is based on CLOCK_REALTIME which can jump forwards and backwards as the system time-of-day
        // clock is changed, including by NTP. CLOCK_MONOTONIC represents the absolute elapsed wall-clock time since
        // some arbitrary, fixed point in the past. It isn't affected by changes in the system time-of-day clock.
        if (llabs(elapsed_utc_time - elapsed_monotonic_time) > ELAPSED_UTC_TIME_TOLERANCE_US) {
            if (0 == con_state_ptr->rx_state.realtime_monotonic_clock_mismatch_count++) {
                CDI_LOG_THREAD(kLogWarning, "Elapsed real-time clock[%lld]us exceeded elapsed monotonic clock[%lld]us "
                               "by [%lld]us. Using monotonic clock.", elapsed_utc_time, elapsed_monotonic_time,
                               llabs(elapsed_utc_time - elapsed_monotonic_time));
            } else {
                CDI_LOG_THREAD(kLogDebug, "Elapsed real-time clock[%lld]us exceeded elapsed monotonic clock[%lld]us "
                               "by [%lld]us. Using monotonic clock. This has occurred [%u] times.", elapsed_utc_time,
                               elapsed_monotonic_time, llabs(elapsed_utc_time - elapsed_monotonic_time),
                               con_state_ptr->rx_state.realtime_monotonic_clock_mismatch_count);

            }
            elapsed_time = elapsed_monotonic_time;
        } else {
            elapsed_time = elapsed_utc_time;
        }

        uint64_t timestamp_diff = 0;
        if (payload_timestamp_us > con_state_ptr->rx_state.payload_base_timestamp_us) {
            timestamp_diff = payload_timestamp_us - con_state_ptr->rx_state.payload_base_timestamp_us;
        }
        if (elapsed_time < timestamp_diff) {
            uint64_t sleep_time = timestamp_diff - elapsed_time;
#ifdef DEBUG_RX_BUFFER
            CDI_LOG_THREAD(kLogInfo, "Elapsed[%llu] TsDiff[%llu] Sleep[%llu]", elapsed_time, timestamp_diff, sleep_time);
#endif
            if (sleep_time > buffer_delay_us) {
                // Timestamp is in the future that is beyond our buffer delay period. Need to set new timestamp base.
                CDI_LOG_THREAD(kLogWarning,
                               "Payload timestamp[%dsec,%dns] is in future[%llu]us.",
                               app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                               app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds,
                               sleep_time - buffer_delay_us);
                restart = true;
            } else {
                // Setup returned timeout.
                *ret_timeout_ms_ptr = (uint32_t)(sleep_time / 1000L); // Convert uS to mS.
            }
        } else {
            // Payload arrived past the elapsed time.
            uint64_t past_time_amount = elapsed_time - timestamp_diff;
#ifdef DEBUG_RX_BUFFER
            CDI_LOG_THREAD(kLogInfo, "Elapsed[%llu] TsDiff[%llu] Past[%llu]", elapsed_time, timestamp_diff, past_time_amount);
#endif
            if (past_time_amount > buffer_delay_us) {
                // We are past our buffer delay time (the payload is late). Need to reset base times and adjust Rx
                // buffer delay to ensure the total delay matches buffer_delay_us.
                CDI_LOG_THREAD(kLogError, "Payload timestamp[%dsec,%dns] is late[%llu]us.",
                                app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.seconds,
                                app_cb_data_ptr->core_extra_data.origination_ptp_timestamp.nanoseconds,
                                past_time_amount);
                restart = true;
            } else {
                // Setup to return a payload to send to application. Return a timeout of zero so caller checks payload
                // queue and immediately returns to this function.
                *ret_timeout_ms_ptr = 0;
                *ret_app_cb_data_ptr = *app_cb_data_ptr; // Make a copy of the data.
                // Put the item back in the pool and remove it from the list.
                CdiPoolPut(con_state_ptr->rx_state.ordered_payload_pool_handle, app_cb_data_ptr);
                CdiListPop(list_ptr);
                ret = true; // true= Have a payload to send to application.
            }
        }
    }

    if (restart) {
        // Need to restart the buffer delay. If the amount of time that has elapsed is less than the full buffer
        // delay, then decrease the full buffer delay by that amount, otherwise use the full buffer delay.
        if (elapsed_time < buffer_delay_us) {
            buffer_delay_us -= elapsed_time;
        }
        RestartBufferedDelay(con_state_ptr, current_monotonic_time_us, current_utc_us, payload_timestamp_us,
                             buffer_delay_us);
        *ret_timeout_ms_ptr = 0; // Setup so caller checks payload queue and immediately returns to this function.
    }

    return ret;
}
