// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the external definitions for the receive payload delay buffer.
 */

#ifndef RECEIVE_BUFFER_H__
#define RECEIVE_BUFFER_H__

#include "endpoint_manager.h"
#include "private.h"

typedef struct ReceiveBufferState* ReceiveBufferHandle; ///< Forward declaration of a receive buffer handle type.

/**
 * Creates a receive delay buffer of the specified length, allocating all of the associated resources.
 *
 * @param log_handle Handle to the logger to be used for any messages from this module.
 * @param error_message_pool Handle to the pool to which error messages are to be freed in the event that an internal
 *                           error prevents an input payload from being sent to the next stage.
 * @param buffer_delay_ms The number of milliseconds to delay each payload, more or less, depending on each payload's
 *                        timestamp value.
 * @param max_rx_payloads The number of objects to allocate for holding payloads in the delay buffer.
 * @param output_queue_handle Handle to which the receive delay buffer is to send payloads after they've been delayed.
 * @param receive_buffer_handle_ptr Address of where to write the receive delay buffer's handle if successfully created.
 * @param input_queue_handle_ptr Address to write the handle for the receive delay buffer's input queue if creation was
 *                               successful.
 *
 * @return CdiReturnStatus kCdiStatusOk if the recieve delay buffer was successfully created or
 *         kCdiStatusNotEnoughMemory if memory was insufficient to allocate all of the required resources.
 */
CdiReturnStatus RxBufferInit(CdiLogHandle log_handle, CdiPoolHandle error_message_pool, int buffer_delay_ms,
                             int max_rx_payloads, CdiQueueHandle output_queue_handle,
                             ReceiveBufferHandle* receive_buffer_handle_ptr, CdiQueueHandle* input_queue_handle_ptr);

/**
 * Destroys the receive delay buffer specified by the handle. Payloads currently in the delay line are pushed to the
 * output queue then the associate thread is shut down and joined and all resources allocated are freed.
 *
 * @param receive_buffer_handle Handle for the receive delay buffer to destroy.
 */
void RxBufferDestroy(ReceiveBufferHandle receive_buffer_handle);

#endif  // RECEIVE_BUFFER_H__
