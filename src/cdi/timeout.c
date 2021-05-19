// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/*
 * @file
 * @brief
 * This file contains definitions and implementation for setting and managing timeouts.
 */

#include "timeout.h"

#include "cdi_logger_api.h"
#include "internal.h"
#include "internal_log.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure for reading and writing FIFO entries which contains callback data and callback function pointer
 */
typedef struct TimeoutCbFifoData {
    CdiTimeoutCbData cb_data; ///< Return data for timeout callback.
    CdiTimeoutCallback cb_ptr; ///< Pointer to timeout callback function.
} TimeoutCbFifoData;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 *  This is an optimized version of CdiTimeoutRemove for dealing with expired timers.
 *  If a timer expiration occurs the active timer is always the one removed, the stop
 *  signal does not need to be set, and memory is not freed until after the callback
 *  function executes.
 *
 *  @param handle_instance is the handle for TimeoutInstanceState state info
 */
static void ExpiredTimeoutRemove(CdiTimeoutInstanceHandle instance_handle)
{
    CdiOsCritSectionReserve(instance_handle->critical_section);
    if (instance_handle != NULL) {
        CdiListPop(&instance_handle->timeout_list);
        if (CdiListIsEmpty(&instance_handle->timeout_list)) {
            CdiOsSignalClear(instance_handle->go_signal);
        }
    }
    CdiOsCritSectionRelease(instance_handle->critical_section);
}

/**
 *  This function packages up the data for the callback FIFO to use and then removes the expired timeout from
 *  the list of timeouts.
 *
 *  @param instance_handle is the TimeoutInstanceState handle with timeout process information
 *  @param expired_handle is the TimeoutDataState handle for the timeout that has expired
 *
 *  @return true if successful, false if failed to write to callback fifo
 */
static bool ServiceExpiredTimeout(CdiTimeoutInstanceHandle instance_handle, TimeoutHandle expired_handle) {
    bool ret = true;
    TimeoutCbFifoData fifo_data;
    // Package data for sending into the callback fifo
    fifo_data.cb_ptr = expired_handle->cb_ptr;
    fifo_data.cb_data.handle = expired_handle;
    fifo_data.cb_data.user_data_ptr = expired_handle->user_data_ptr;
    // Remove the expired timeout at head of timeout list.
    ExpiredTimeoutRemove(instance_handle);
    // Send the expired timeout data to the callback thread for servicing
    if (!CdiFifoWrite(instance_handle->cb_fifo_handle, 1, NULL, &fifo_data)) {
        ret = false;
        CDI_LOG_THREAD(kLogError, "Timeout callback FIFO write failed");
    }

    return ret;
}

/* @brief This thread waits for data to be sent to the callback FIFO or for a shutdown signal.
 * When callback FIFO data is received the callback pointer is pulled from the structure and
 * exectues the callback pointer with the callback data from the structure used as the sole
 * parameter for the callback function. Callbacks occur after a timeout has expired so the expired
 * timeout TimeoutDataState structure is not sent back to the memory pool until after the callback
 * function has completed.
 */
static THREAD TimeoutCbThread(void* ptr)
{
    TimeoutInstanceState* state_ptr = (TimeoutInstanceState*)ptr;

    // Set this thread to use the desired log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(state_ptr->log_handle);

    CDI_LOG_THREAD(kLogInfo, "Timeout Callback Thread established");

    // loop until shutdown signal received
    while (!CdiOsSignalGet(state_ptr->shutdown_signal)) {
        // wait on read data or shutdown signal
        TimeoutCbFifoData fifo_data;
        if (CdiFifoRead(state_ptr->cb_fifo_handle, CDI_INFINITE, state_ptr->shutdown_signal, &fifo_data)) {
            CDI_LOG_THREAD(kLogDebug, "Timeout expired, executing callback function");
            (fifo_data.cb_ptr)(&fifo_data.cb_data); // execute callback function
            CdiPoolPut(state_ptr->mem_pool_handle, fifo_data.cb_data.handle);
        }
    }

    CDI_LOG_THREAD(kLogInfo, "Timeout Callback Thread exiting");

    return 0;
}

/* @brief This thread checks for timer signals Go, Stop, and Shutdown and sets new timers when timers are available.
 * If there are active timers this thread sleeps until the first timer to expire goes off,
 * is cleared, or shutdown is received. If the timer is expired it is sent to a separate FIFO thread to execute the
 * user callback function. This separates the execution time of the callback function from the time of managing the
 * timers themselves.
 */
static THREAD TimeoutMainThread(void* ptr)
{
    TimeoutInstanceState* state_ptr = (TimeoutInstanceState*)ptr;

    // Set this thread to use the desired log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(state_ptr->log_handle);

    bool thread_exit = false;
    CdiSignalType outer_signal_array[2];
    CdiSignalType inner_signal_array[2];
    outer_signal_array[0] = state_ptr->shutdown_signal;
    outer_signal_array[1] = state_ptr->go_signal;
    inner_signal_array[0] = state_ptr->shutdown_signal;
    inner_signal_array[1] = state_ptr->stop_signal;

    // Loop to check whether there are active timers exit on shutdown.
    while (!thread_exit) {
        unsigned int signal_index;
        // have thread go to sleep until shutdown_signal or go_signal is received
        CdiOsSignalsWait(outer_signal_array, 2, false, CDI_INFINITE, &signal_index);
        CdiOsCritSectionReserve(state_ptr->critical_section);
        if (signal_index == 0) {
            // Shutdown received.
            CdiOsCritSectionRelease(state_ptr->critical_section);
            thread_exit = true;
            CDI_LOG_THREAD(kLogInfo, "Timeout thread shutdown received");

        // Do a NULL check in case last timer got cleared after the go signal was seen.
        } else if (!CdiListIsEmpty(&state_ptr->timeout_list)) {
            //
            // Timers are available to set, so get the current time to calculate when the head timer will expire so
            // the next timer can be set.
            //
            uint64_t current_time = CdiOsGetMicroseconds();
            TimeoutDataState* timeout_head_ptr = CONTAINER_OF(CdiListPeek(&state_ptr->timeout_list), TimeoutDataState, list_entry);
            if (timeout_head_ptr->deadline_us > current_time) {

                // Get time difference in ms
                const unsigned int new_timeout = (timeout_head_ptr->deadline_us - current_time + 500) / 1000;

                CdiOsCritSectionRelease(state_ptr->critical_section);

                // Set a wait for the length of timeout_head remaining deadline time in ms break from wait if
                // stop_signal or shutdown_signal is received.
                CdiOsSignalsWait(inner_signal_array, 2, false, new_timeout, &signal_index);
                if (0 == signal_index) {
                    // Shutdown signal sent.
                    thread_exit = true;
                    CDI_LOG_THREAD(kLogInfo, "Cancelled timer without logging. Shutdown received");
                } else if (CDI_OS_SIG_TIMEOUT == signal_index) {
                    // Timeout occurred.
                    if (!ServiceExpiredTimeout(state_ptr, timeout_head_ptr)) {
                        CDI_LOG_THREAD(kLogError, "Failed to service expired timeout");
                    }
                } else {
                    // Stop_signal received so restart loop and grab next timeout_head if available.
                    CdiOsSignalClear(state_ptr->stop_signal);
                }
            } else {
                 // Timeout has occurred before wait could be set.
                if (!ServiceExpiredTimeout(state_ptr, timeout_head_ptr)) {
                    CDI_LOG_THREAD(kLogError, "Failed to service expired timeout");
                }
            }
        }
        // Timeout_list is empty.
    }

    CDI_LOG_THREAD(kLogInfo, "Timeout main thread exiting");
    return 0;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiTimeoutCreate(CdiLogHandle log_handle, CdiTimeoutInstanceHandle* ret_handle_ptr)
{
    CdiReturnStatus ret = kCdiStatusOk;

    TimeoutInstanceState* state_ptr = (TimeoutInstanceState*)CdiOsMemAllocZero(sizeof(TimeoutInstanceState));
    if (state_ptr == NULL) {
        // NOTE: Must use CDI_LOG_HANDLE() to direct the log message to the desired log.
        CDI_LOG_HANDLE(log_handle, kLogError, "Insufficient memory for TimeoutInstanceState allocation");
        ret = kCdiStatusNotEnoughMemory;
    }

    state_ptr->log_handle = log_handle;

    if (ret == kCdiStatusOk) {
        if (!CdiPoolCreate("Timeout TimeoutDataState Pool", MAX_TIMERS, MAX_TIMERS_GROW, MAX_POOL_GROW_COUNT,
                            sizeof(TimeoutDataState), true, // true= Make thread-safe
                            &state_ptr->mem_pool_handle)) {
                            CDI_LOG_HANDLE(log_handle, kLogError, "ERROR: Failed to create memory pool");
                            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
       if (!CdiOsCritSectionCreate(&state_ptr->critical_section)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Failed to create critical section for Timeout Instance State");
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&state_ptr->shutdown_signal)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Failed to create signal for Timeout Shutdown");
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&state_ptr->stop_signal)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Failed to create signal for Timeout Timer Stop");
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&state_ptr->go_signal)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Failed to create signal for Timeout Go");
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiOsThreadCreate(TimeoutMainThread, &state_ptr->main_thread_id, "TimeoutMain", (void*)state_ptr, NULL)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Timeout main thread creation failed");
            ret = kCdiStatusFatal;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiFifoCreate("Timeout CB FIFO", MAX_TIMERS, sizeof(TimeoutCbFifoData), NULL, NULL,
                           &state_ptr->cb_fifo_handle)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Callback FIFO creation failed");
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (ret == kCdiStatusOk) {
        if (!CdiOsThreadCreate(TimeoutCbThread, &state_ptr->cb_thread_id, "TimeoutCb", (void*)state_ptr, NULL)) {
            CDI_LOG_HANDLE(log_handle, kLogError, "Timeout callback thread creation failed");
            ret = kCdiStatusFatal;
        }
    }

    if (ret == kCdiStatusOk) {
        CdiListInit(&state_ptr->timeout_list);
    }

    // If the timeout creation process fails a NULL handle is returned and the partially created timeout is destroyed,
    if (ret == kCdiStatusOk) {
        *ret_handle_ptr = (CdiTimeoutInstanceHandle)state_ptr;
    } else {
        *ret_handle_ptr = NULL;
        CdiTimeoutDestroy(state_ptr);
    }

    return ret;
}

void CdiTimeoutDestroy(CdiTimeoutInstanceHandle handle)
{
    // Check for valid handle before doing anything
    if (NULL != handle) {
        // Clean-up thread resources. We will wait for it to exit using thread join.
        SdkThreadJoin(handle->main_thread_id, handle->shutdown_signal);
        handle->main_thread_id = NULL;
        SdkThreadJoin(handle->cb_thread_id, handle->shutdown_signal);
        handle->cb_thread_id = NULL;

        // Not setting any of these to NULL, since the memory is freed directly below.
        CdiFifoDestroy(handle->cb_fifo_handle);
        CdiOsSignalDelete(handle->shutdown_signal);
        CdiOsSignalDelete(handle->stop_signal);
        CdiOsSignalDelete(handle->go_signal);
        CdiOsCritSectionDelete(handle->critical_section);
        CdiPoolDestroy(handle->mem_pool_handle);

        CdiOsMemFree(handle);
    }
}

bool CdiTimeoutAdd(CdiTimeoutInstanceHandle instance_handle, CdiTimeoutCallback cb_ptr, int timeout_us, void* user_data_ptr, TimeoutHandle* ret_handle_ptr)
{
    bool ret = true;

    TimeoutDataState* new_timeout_ptr = NULL;

    ret = CdiPoolGet(instance_handle->mem_pool_handle, (void**)&new_timeout_ptr);

    // Initialize newly allocated timeout that will be added to timeout list.
    if (ret) {
        new_timeout_ptr->cb_ptr = cb_ptr;
        new_timeout_ptr->user_data_ptr = user_data_ptr;
        new_timeout_ptr->deadline_us = CdiOsGetMicroseconds() + timeout_us;
    }

    if (ret) {
        CdiOsCritSectionReserve(instance_handle->critical_section);
        if(CdiListIsEmpty(&instance_handle->timeout_list)) { // no active timeouts so setting the new one
            CdiListAddHead(&instance_handle->timeout_list, &new_timeout_ptr->list_entry);
            CdiOsCritSectionRelease(instance_handle->critical_section);
            if (!CdiOsSignalSet(instance_handle->go_signal)) {
                CDI_LOG_THREAD(kLogError,"Unable to set timer GO signal");
                ret = false;
            }
        } else {
            // Find where the new timeout belongs within the list.
            TimeoutDataState* compare_ptr = CONTAINER_OF(CdiListPeek(&instance_handle->timeout_list), TimeoutDataState, list_entry);
            while ((compare_ptr->deadline_us <= new_timeout_ptr->deadline_us)
                    && (CONTAINER_OF(CdiListPeekTail(&instance_handle->timeout_list), TimeoutDataState, list_entry)  != compare_ptr)) {
                compare_ptr = CONTAINER_OF(compare_ptr->list_entry.next_ptr, TimeoutDataState, list_entry);
            }

            // if smaller insert new entry in list before compare_ptr else insert at end of list
            if (compare_ptr->deadline_us >= new_timeout_ptr->deadline_us) {
                // check if inserting new head
                if (compare_ptr ==
                        CONTAINER_OF(CdiListPeek(&instance_handle->timeout_list), TimeoutDataState, list_entry)) {
                    CdiListAddHead(&instance_handle->timeout_list, &new_timeout_ptr->list_entry);
                    if (!CdiOsSignalSet(instance_handle->stop_signal)) {
                        CDI_LOG_THREAD(kLogError, "Unable to set stop on setting new head timer");
                        ret = false;
                    }
                } else {
                    // New timeout is somewhere in the middle of the list.
                    CdiListAddAfter(&instance_handle->timeout_list, &new_timeout_ptr->list_entry, compare_ptr->list_entry.prev_ptr);
                }
            } else {
                // New timeout is the new tail of the list.
                CdiListAddTail(&instance_handle->timeout_list, &new_timeout_ptr->list_entry);
            }
            CdiOsCritSectionRelease(instance_handle->critical_section);
        }
    }

    *ret_handle_ptr = new_timeout_ptr;

    return ret;
}

bool CdiTimeoutRemove(TimeoutHandle handle, CdiTimeoutInstanceHandle instance_handle)
{
    bool ret = handle != NULL;

    if (ret) {
        CdiOsCritSectionReserve(instance_handle->critical_section);
        if (&handle->list_entry == CdiListPeek(&instance_handle->timeout_list)) {
            CdiOsSignalSet(instance_handle->stop_signal);
        }
        if (instance_handle->timeout_list.count == 1) {
            CdiOsSignalClear(instance_handle->go_signal);
        }
        CdiListRemove(&instance_handle->timeout_list, &handle->list_entry);

        CdiOsCritSectionRelease(instance_handle->critical_section);
        CdiPoolPut(instance_handle->mem_pool_handle, handle);
    }

    return ret;
}

