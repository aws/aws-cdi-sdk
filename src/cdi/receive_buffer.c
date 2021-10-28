// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the implementation of the receive payload delay buffer.
 */

#include "receive_buffer.h"

#include "cdi_os_api.h"
#include "configuration.h"
#include "internal.h"

#include <stdint.h>
#include <stdlib.h> // For llabs()

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * Internal state of a receive buffer "object."
 */
struct ReceiveBufferState {
    uint64_t buffer_delay_microseconds; ///< The configured amount to delay payloads in units of microseconds.
    CdiLogHandle log_handle; ///< Logger handle used for this connection. If NULL, the global logger is used.
    CdiPoolHandle error_message_pool; ///< Pool used to hold error message strings.
    CdiQueueHandle output_queue_handle; ///< Configured handle of where payloads are to be sent after being delayed.
    CdiPoolHandle delay_pool_handle; ///< @brief Pool used to hold payload state data (AppPayloadCallbackData) that is
                                     /// stored in the thread's delay list ordered by send time.
    CdiQueueHandle input_queue_handle; ///< Handle of the input queue to the receive delay buffer.
    CdiThreadID buffer_thread_id; ///< ID of the receive delay buffer thread.
    CdiSignalType shutdown_signal; ///< Signal to set in order to tell the thread to stop running.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// The number of consecutive payloads with time stamps out of the buffering window before offset is reset.
static const int kMaxMissed = 100;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Convenience function to get the TAI based PTP timestamp and convert it to a single integer value representing the
 * number of microseconds from the epoch.
 *
 * @return uint64_t The number of microseconds since the epoch according to the hosts real time clock.
 */
static inline uint64_t TaiNowMicroseconds()
{
    const CdiPtpTimestamp now_ptp = CdiCoreGetPtpTimestamp(NULL);
    return CdiUtilityPtpTimestampToMicroseconds(&now_ptp);
}

/**
 * The main function for the receive delay buffer thread. It takes application callback structures from its input queue
 * and sends them to the configured queue after a configurable delay based on the timestamps associated with each
 * payload.
 *
 * @param ptr Pointer to thread specific data. In this case, a pointer to ReceiveBufferState.
 *
 * @return The return value is not used.
 */
CDI_THREAD ReceiveBufferThread(void* ptr)
{
    ReceiveBufferState* state_ptr = (ReceiveBufferState*)ptr;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(state_ptr->log_handle);

    CdiList delay_list;
    CdiListInit(&delay_list);

    int missed_count = kMaxMissed;  // Cause the first received payload to reset the timestamp offset.
    int64_t t_offset = 0;  // The difference between payload timestamps and the monotonic clock.
    uint32_t timeout_ms = CDI_INFINITE;

    while (!CdiOsSignalGet(state_ptr->shutdown_signal)) {
        // Wait for work to do. If the queue is empty, we will wait for data or the shutdown signal.
        AppPayloadCallbackData app_cb_data;
        if (CdiQueuePopWait(state_ptr->input_queue_handle, timeout_ms, state_ptr->shutdown_signal,
                            (void**)&app_cb_data)) {
            const uint64_t payload_timestamp_us =
                CdiUtilityPtpTimestampToMicroseconds(&app_cb_data.core_extra_data.origination_ptp_timestamp);
            const uint64_t now = TaiNowMicroseconds();

            // Reset t_offset if necessary. This is done before incrementing missed_count so that the offset is set to
            // the extreme end of the window even if the first payload's timestamp is close enough to "now."
            if (missed_count >= kMaxMissed) {
                t_offset = now - payload_timestamp_us;
                missed_count = 0;
           } else if (t_offset + payload_timestamp_us < now - state_ptr->buffer_delay_microseconds ||
                t_offset + payload_timestamp_us > now) {
                missed_count++;
            } else {
                missed_count = 0;
            }

            // Compute the send time.
            const uint64_t send_time = payload_timestamp_us + state_ptr->buffer_delay_microseconds + t_offset;

            // Put the payload into the output queue if it's already late.
            if (send_time <= now) {
                app_cb_data.receive_buffer_send_time = send_time;
                CdiQueuePush(state_ptr->output_queue_handle, &app_cb_data);
            } else {
                // Cap send time to now + delay.
                app_cb_data.receive_buffer_send_time = CDI_MIN(send_time, now + state_ptr->buffer_delay_microseconds);

                // The input and output queues provide copies of the AppPayloadCallbackData structure. Get an item out
                // of the pool to store the data in while it's being delayed.
                AppPayloadCallbackData* pool_item_ptr = NULL;
                if (!CdiPoolGet(state_ptr->delay_pool_handle, (void**)&pool_item_ptr)) {
                    CDI_LOG_THREAD(kLogCritical,
                                   "Failed to get AppPayloadCallbackData from pool. Throwing away payload [%10u.%09u]",
                                   app_cb_data.core_extra_data.origination_ptp_timestamp.seconds,
                                   app_cb_data.core_extra_data.origination_ptp_timestamp.nanoseconds);
                } else {
                    *pool_item_ptr = app_cb_data;  // Copy the callback data into the pool item storage.

                    // Place the payload int the delay line with its position determined by send time.
                    CdiListIterator list_iterator;
                    CdiListIteratorInit(&delay_list, &list_iterator);
                    AppPayloadCallbackData* entry_ptr;
                    while (NULL != (entry_ptr = (AppPayloadCallbackData*)CdiListIteratorGetNext(&list_iterator))) {
                        const uint64_t entry_send_time = entry_ptr->receive_buffer_send_time;
                        if (entry_send_time > app_cb_data.receive_buffer_send_time) {
                            CdiListAddBefore(&delay_list, &pool_item_ptr->list_entry, &entry_ptr->list_entry);
                            break;
                        }
                    }
                    if (NULL == entry_ptr) {
                        CdiListAddTail(&delay_list, &pool_item_ptr->list_entry);
                    }
                }
            }
        }

        // Take items out of the delay line until the first one that needs to remain in it is encountered.
        AppPayloadCallbackData* app_cb_data_ptr = NULL;
        while (NULL != (app_cb_data_ptr = (AppPayloadCallbackData*)CdiListPeek(&delay_list))) {
            // Get "now" and send time of payload at the head of the delay line.
            const uint64_t now = TaiNowMicroseconds();
            const uint64_t send_time = app_cb_data_ptr->receive_buffer_send_time;

            // Place payload into the output queue if its send time has already passed or if send time is too far in the
            // future. This will happen if host clock has been set backwards by more than the delay time after the
            // payload was put in the list.
            if (send_time <= now || send_time > now + state_ptr->buffer_delay_microseconds) {
                if (!CdiQueuePush(state_ptr->output_queue_handle, app_cb_data_ptr)) {
                    PayloadErrorFreeBuffer(state_ptr->error_message_pool, app_cb_data_ptr);
                }
                // Free the pool storage now that its data has been copied into the queue item's storage.
                CdiPoolPut(state_ptr->delay_pool_handle, app_cb_data_ptr);
                // Pop the head entry out of the delay line.
                CdiListPop(&delay_list);
            } else {
                // Since items are ordered by send time, there's no point looking any farther than the first payload
                // whose send time has not yet been reached.
                break;
            }
        }

        // Figure out the maximum wait time for the next queue pop based on payload at the head of the queue.
        if (NULL == app_cb_data_ptr) {
            // The delay line is empty now so wait indefinitely until the next payload arrives in the input queue.
            timeout_ms = CDI_INFINITE;
        } else {
            // Round up to the next millisecond to prevent consuming unproductive CPU cycles. If, for example, the next
            // payload send time is 500 microseconds in the future, rounding down would give a zero millisecond wait
            // time. Assuming no new payloads arrive in the input queue during that time, the loop will run continuously
            // doing no useful work until 500 microseconds have passed. The trade off is that payloads will be delayed
            // up to an extra millisecond.
            const uint64_t now = TaiNowMicroseconds();
            const uint64_t send_time = app_cb_data_ptr->receive_buffer_send_time;
            timeout_ms = (now >= send_time) ? 0 : ((send_time - now + 999) / 1000);
        }
    }

    // Send the entries in the delay line on to callback thread and return items to the intermediate storage pool.
    void* item_ptr = NULL;
    while (NULL != (item_ptr = CdiListPop(&delay_list))) {
        if (!CdiQueuePush(state_ptr->output_queue_handle, item_ptr)) {
            PayloadErrorFreeBuffer(state_ptr->error_message_pool, (AppPayloadCallbackData*)item_ptr);
        }
        CdiPoolPut(state_ptr->delay_pool_handle, item_ptr);
    }

    return 0;  // Return value is not used for anything.
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus RxBufferInit(CdiLogHandle log_handle, CdiPoolHandle error_message_pool, int buffer_delay_ms,
                             int max_rx_payloads, CdiQueueHandle output_queue_handle,
                             ReceiveBufferHandle* receive_buffer_handle_ptr, CdiQueueHandle* input_queue_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    ReceiveBufferState* state_ptr = CdiOsMemAllocZero(sizeof(ReceiveBufferState));
    if (NULL == state_ptr) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        state_ptr->buffer_delay_microseconds = buffer_delay_ms * 1000;
        state_ptr->output_queue_handle = output_queue_handle;
        state_ptr->log_handle = log_handle;
        state_ptr->error_message_pool = error_message_pool;

        // Create the input queue for the receive buffer thread.
        if (!CdiQueueCreate("Receive Buffer Thread Input Queue", MAX_PAYLOADS_PER_CONNECTION,
                            CDI_FIXED_QUEUE_SIZE, CDI_FIXED_QUEUE_SIZE, sizeof(AppPayloadCallbackData),
                            kQueueSignalPopWait, // Queue can block on pops.
                            &state_ptr->input_queue_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }

        if (kCdiStatusOk == rs) {
            int pool_items = (max_rx_payloads * buffer_delay_ms) / CDI_RX_BUFFER_DELAY_BUFFER_MS_DIVISOR;
            if (!CdiPoolCreate("Connection RxOrdered AppPayloadCallbackData Pool", pool_items, NO_GROW_SIZE,
                               NO_GROW_COUNT, sizeof(AppPayloadCallbackData), false, // false= Not thread-safe.
                               &state_ptr->delay_pool_handle)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }

        if (kCdiStatusOk == rs) {
            if (!CdiOsSignalCreate(&state_ptr->shutdown_signal)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }

        if (kCdiStatusOk == rs) {
            // Start the receive buffer thread.
            if (!CdiOsThreadCreate(ReceiveBufferThread, &state_ptr->buffer_thread_id, "ReceiveBuffer", state_ptr,
                                   NULL)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }
    }

    if (kCdiStatusOk == rs) {
        *input_queue_handle_ptr = state_ptr->input_queue_handle;
        *receive_buffer_handle_ptr = state_ptr;
    } else {
        RxBufferDestroy(state_ptr);
    }

    return rs;
}

void RxBufferDestroy(ReceiveBufferHandle receive_buffer_handle)
{
    ReceiveBufferState* state_ptr = receive_buffer_handle;

    if (NULL != state_ptr) {
        if (NULL != state_ptr->shutdown_signal) {
            CdiOsSignalSet(state_ptr->shutdown_signal);

            if (NULL != state_ptr->buffer_thread_id) {
                CdiOsThreadJoin(state_ptr->buffer_thread_id, CDI_INFINITE, NULL);
                state_ptr->buffer_thread_id = NULL;
            }

            CdiOsSignalDelete(state_ptr->shutdown_signal);
            state_ptr->shutdown_signal = NULL;
        }

        if (NULL != state_ptr->delay_pool_handle) {
            CdiPoolDestroy(state_ptr->delay_pool_handle);
            state_ptr->delay_pool_handle = NULL;
        }

        if (NULL != state_ptr->input_queue_handle) {
            CdiQueueDestroy(state_ptr->input_queue_handle);
            state_ptr->input_queue_handle = NULL;
        }

        CdiOsMemFree(state_ptr);
    }
}
