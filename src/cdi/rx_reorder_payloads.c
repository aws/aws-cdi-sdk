// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * When payloads are received from the transmitter, they can arrive in any order. The routines here will put the
 * payloads in order.
 */

#include <stdbool.h>

#include "rx_reorder_payloads.h"

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_pool_api.h"
#include "cdi_raw_api.h"
#include "internal_rx.h"
#include "private.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Ensure define is a power of 2.
CDI_STATIC_ASSERT((0 == (CDI_MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER % 2)), "The define must be a power of 2.");

/// @brief Ensure the packet out of order window is less than or equal to the payload out of order buffer.
CDI_STATIC_ASSERT((CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW <= CDI_MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER), "...WINDOW must be <= ...BUFFER.");

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Advance the specified state array index value by 1. If a maximum limit is reached, the value wraps to zero.
 *
 * @param payload_num_max Maximum value for payload number (protocol dependent).
 * @param index Current index value.
 *
 * @return New index value.
 */
static inline int AdvanceStateArrayIndex(int payload_num_max, int index)
{
    int max_value = CDI_MIN(payload_num_max, CDI_ARRAY_ELEMENT_COUNT(((RxEndpointState*)0)->payload_state_array_ptr)-1);
    if (++index > max_value) {
        index = 0;
    }
    return index;
}

/**
 * @brief Set the specified payload to the ignore state.
 *
 * @param payload_state_ptr Pointer to payload state data.
 */
static void SetIgnoreState(RxPayloadState* payload_state_ptr)
{
    payload_state_ptr->payload_state = kPayloadIgnore;
    payload_state_ptr->packet_count = 0;
    payload_state_ptr->ignore_packet_count = 0;
}

/**
 * @brief Decrease packet window count by the specified value.
 *
 * @param endpoint_state_ptr Pointer to endpoint state data.
 * @param packet_count Number of packets to decrease window by.
 */
static void DecreasePacketWindowCount(RxEndpointState* endpoint_state_ptr, int packet_count)
{
    if (endpoint_state_ptr->rxreorder_buffered_packet_count >= packet_count) {
        endpoint_state_ptr->rxreorder_buffered_packet_count -= packet_count;
    } else {
        endpoint_state_ptr->rxreorder_buffered_packet_count = 0; // Don't let value go negative.
    }
}

/**
 * @brief Free payload state by removing it from the payload Rx reorder list and returning it to the pool.
 *
 * @param endpoint_ptr Pointer to endpoint data.
 * @param index Index of payload state pointer in payload_state_array_ptr.
 */
static void FreePayloadState(CdiEndpointState* endpoint_ptr, int index)
{
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[index];

    // Return it to the pool.
    CdiPoolPut(con_state_ptr->rx_state.rx_payload_state_pool_handle, payload_state_ptr);
    endpoint_ptr->rx_state.payload_state_array_ptr[index] = NULL; // Pointer is no longer valid, so clear it.
}

/**
 * @brief Send the payload if it is ready.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param index Index of payload state pointer in payload_state_array_ptr.
 *
 * @return true if payload was sent, otherwise false.
 */
static bool SendPayloadIfCompleteOrError(CdiEndpointState* endpoint_ptr, int index)
{
    bool sent = false;
    RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[index];

    if (kPayloadComplete == payload_state_ptr->payload_state) {
        // Send the payload down stream, remove it from the Rx reorder list and free payload_state_ptr.
        DecreasePacketWindowCount(&endpoint_ptr->rx_state, payload_state_ptr->packet_count);
        RxSendPayload(endpoint_ptr, payload_state_ptr);
        FreePayloadState(endpoint_ptr, index);
        sent = true;
    } else if (kPayloadError == payload_state_ptr->payload_state) {
        // Send the payload down stream and change the payload state to ignore.
        DecreasePacketWindowCount(&endpoint_ptr->rx_state, payload_state_ptr->packet_count);
        RxSendPayload(endpoint_ptr, payload_state_ptr);
        SetIgnoreState(payload_state_ptr);
        sent = true;
    }

    if (sent) {
        int payload_num_max = endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max;
        // Set current index to next value, taking into account maximum limits.
        endpoint_ptr->rx_state.rxreorder_current_index = AdvanceStateArrayIndex(payload_num_max, index);
    }

    return sent;
}

/**
 * Starting at the window start index, flush partial payloads or erred payloads freeing up enough Rx packet reorder
 * resources to get below the packet limit of MAX_RX_PACKET_OUT_OF_ORDER_WINDOW packets.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 */
static void FlushPartialPayload(CdiEndpointState* endpoint_ptr)
{
    int payload_num_max = endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max;

    int idx = endpoint_ptr->rx_state.rxreorder_current_index;
    int starting_idx = idx;
    while (endpoint_ptr->rx_state.rxreorder_buffered_packet_count >= CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW) {
        RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[idx];
        if (payload_state_ptr) {
            // If this payload is in progress, change it to the error state.
            if ((payload_state_ptr->payload_state == kPayloadInProgress) ||
                (payload_state_ptr->payload_state == kPayloadPacketZeroPending)) {
                RxReorderPayloadError(endpoint_ptr, payload_state_ptr);
            }
            // Send payload if state is complete or error, which reduces rxreorder_buffered_packet_count.
            SendPayloadIfCompleteOrError(endpoint_ptr, idx);
        }

        // Advance the index, taking into account maximum limits.
        idx = AdvanceStateArrayIndex(payload_num_max, idx);

        if (idx == starting_idx) {
            // Wrapped.
            CDI_LOG_THREAD(kLogError, "Failed to reduce Rx packet count[%d] below limit[%d]",
                           endpoint_ptr->rx_state.rxreorder_buffered_packet_count, CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW);
            assert(false); // Should never occur.
            break;
        }
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void RxReorderPayloadResetState(RxPayloadState* payload_state_ptr, int payload_num)
{
    // Reset payload state.
    payload_state_ptr->payload_state = kPayloadIdle;
    payload_state_ptr->payload_num = payload_num;
    payload_state_ptr->data_bytes_received = 0;
    payload_state_ptr->packet_count = 0;
    payload_state_ptr->last_total_packet_count = 0;
    payload_state_ptr->suspend_warnings = false;
}

void RxReorderPayloadError(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr)
{
    // We don't want to free the payload memory state here, since RxSendPayload() will be used to send the
    // payload data downstream where it will be later freed.
    RxFreePayloadResources(endpoint_ptr, payload_state_ptr, false); // false= Don't free memory state

    // Ensure an error message and error status have been set.
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
    PAYLOAD_ERROR(endpoint_ptr->connection_state_ptr,
                    &payload_state_ptr->work_request_state.app_payload_cb_data,
                    kCdiStatusRxPayloadError,
                    "Connection[%s] Rx packet error occurred. Payload[%d] Got [%d] packets.",
                    con_state_ptr->saved_connection_name_str, payload_state_ptr->payload_num,
                    payload_state_ptr->packet_count);
    payload_state_ptr->payload_state = kPayloadError;
}

bool RxReorderPayloadIsStale(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr)
{
    RxEndpointState* endpoint_state_ptr = &endpoint_ptr->rx_state;
    uint32_t diff = 0;

    if (endpoint_state_ptr->total_packet_count >= payload_state_ptr->last_total_packet_count) {
        diff = endpoint_state_ptr->total_packet_count - payload_state_ptr->last_total_packet_count;
    } else {
        diff = UINT32_MAX - payload_state_ptr->last_total_packet_count + endpoint_state_ptr->total_packet_count;
    }

    return (diff > CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW);
}

RxPayloadState* RxReorderPayloadStateGet(CdiEndpointState* endpoint_ptr, CdiPoolHandle rx_payload_state_pool_handle,
                                         int payload_num)
{
    // Get masked version of payload number (only use LSBs).
    int current_payload_index = payload_num & (CDI_MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER-1);

    RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[current_payload_index];

    if (NULL == payload_state_ptr) {
        // Get a new entry from the pool.
        if (!CdiPoolGet(rx_payload_state_pool_handle, (void**)&payload_state_ptr)) {
            BACK_PRESSURE_ERROR(endpoint_ptr->connection_state_ptr->back_pressure_state, kLogError,
                                "Failed to get Rx Payload State entry from pool.");
        } else {
            // Initialize the new payload state entry.
            RxReorderPayloadResetState(payload_state_ptr, payload_num);
            endpoint_ptr->rx_state.payload_state_array_ptr[current_payload_index] = payload_state_ptr;
        }
    }

    return payload_state_ptr;
}

void RxReorderPayloadSendReadyPayloads(CdiEndpointState* endpoint_ptr)
{
    int payload_num_max = endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max;

    // Start index at window start.
    int idx = endpoint_ptr->rx_state.rxreorder_current_index;

    // Send payloads while they are in the completed or error state. Stop on all other conditions.
    while (NULL != endpoint_ptr->rx_state.payload_state_array_ptr[idx] &&
        SendPayloadIfCompleteOrError(endpoint_ptr, idx)) {
        // Advance the index, taking into account maximum limits.
        idx = AdvanceStateArrayIndex(payload_num_max, idx);
    }

    // Now, check if we are at or above the maximum number of buffered packets used to reorder payloads.
    if (endpoint_ptr->rx_state.rxreorder_buffered_packet_count >= CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW) {
        // At the limit, so walk the payload state array and flush payload(s) until we get back below the limit.
        CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;
        CdiLogLevel log_level = con_state_ptr->rx_state.received_first_payload ? kLogError : kLogDebug;
        CDI_LOG_THREAD(log_level,
                       "Connection[%s] Exceeded rx-reorder packet cache window size[%d]. Flushing payload(s).",
                       con_state_ptr->saved_connection_name_str, CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW);
        FlushPartialPayload(endpoint_ptr);
    }
}

void RxReorderPayloadSeekFirstPayload(CdiEndpointState* endpoint_ptr) {
    int payload_num_max = endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max;

    // Start index at window start.
    int idx = endpoint_ptr->rx_state.rxreorder_current_index;

    while (NULL == endpoint_ptr->rx_state.payload_state_array_ptr[idx]) {
        // Advance the index, taking into account maximum limits.
        idx = AdvanceStateArrayIndex(payload_num_max, idx);
        if (idx == endpoint_ptr->rx_state.rxreorder_current_index) {
            break;
        }
    }
    endpoint_ptr->rx_state.rxreorder_current_index = idx;
}
