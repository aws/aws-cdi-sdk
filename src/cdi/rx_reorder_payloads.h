// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions of types needed for the reordering of payloads.
 */

#ifndef RX_REORDER_PAYLOADS_H__
#define RX_REORDER_PAYLOADS_H__

#include <stdint.h>

#include "internal.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Get pointer to Rx payload state structure for the specified payload sequence number. If one does not already exist
 * then a new one is created.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param rx_payload_state_pool_handle Handle of Rx payload state pool to use when allocating new payload state
 *                                     structures.
 * @param sequence_num Payload sequence number.
 *
 * @return Pointer to returned Rx payload state structure. If an error occurred, NULL is returned.
 */
RxPayloadState* RxReorderPayloadStateGet(CdiEndpointState* endpoint_ptr, CdiPoolHandle rx_payload_state_pool_handle,
                                         int sequence_num);

/**
 * Reset payload state data.
 *
 * @param payload_state_ptr Pointer to payload state data to reset.
 * @param payload_num Payload number to set.
 */
void RxReorderPayloadResetState(RxPayloadState* payload_state_ptr, int payload_num);


/**
 * @brief Set payload in an error state and free associated payload resources (but not payload state).
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param payload_state_ptr Pointer to the payload state.
 */
void RxReorderPayloadError(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr);

/**
 * @brief Determine if a payload has not received any packets within the packet out of order window. See
 * MAX_RX_PACKET_OUT_OF_ORDER_WINDOW.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param payload_state_ptr Pointer to the payload state.
 *
 * @return true if payload is stale, otherwise false.
 */
bool RxReorderPayloadIsStale(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr);

/**
 * Send the payload on to the next stage because it is complete or determined to be in error.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param send_payload_state_ptr Pointer to the payload state for the completed payload.
 */
void RxReorderPayloadSendPayload(CdiEndpointState* endpoint_ptr, RxPayloadState* send_payload_state_ptr);

/**
 * Starting at the beginning of the payload state list, sends any payloads that are complete or in an error state.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 */
void RxReorderPayloadSendReadyPayloads(CdiEndpointState* endpoint_ptr);

/**
 * Advance the current Rx reorder window index to the first entry that contains a payload.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 */
void RxReorderPayloadSeekFirstPayload(CdiEndpointState* endpoint_ptr);

#endif  // RX_REORDER_PAYLOADS_H__
