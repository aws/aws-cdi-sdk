// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and implementation for a queue that allows a reader thread to use CdiQueuePop() and a
 * writer thread to use CdiQuenePush(). No resource locks are used, so the functions are not reetrant. Blocking
 * CdiQueuePopWait() and CdiQueuePushWait() queue API functions can be used if enabled using the signal_mode parameter
 * of the CdiQueueCreate() API fucntion. NOTE: The API functions only support a single-producer/single-consumer.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_queue_api.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>

#include "cdi_logger_api.h"
#include "singly_linked_list_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Maximum length of the queue name that is stored internally in queue.c.
#define MAX_QUEUE_NAME_LENGTH               (64)

/**
 * @brief This structure represents a single queue item.
 */
typedef struct {
    CdiSinglyLinkedListEntry list_entry; ///< List entry for this item.
    uint8_t item_data_buffer[];          ///< Pointer to data space for this item.
} QueueItem;

/**
 * @brief Structure used to hold state data for a single queue.
 */
typedef struct {
    char name_str[MAX_QUEUE_NAME_LENGTH];       ///< Name of queue. Used for informational purposes only.

    int queue_item_data_byte_size;              ///< Size of the data portion of each item in bytes.
    int queue_item_byte_size;                   ///< Size of each item in bytes (sizeof(QueueItem) + data portion).
    int queue_item_count;                       ///< Number of items in the queue array.
    int queue_grow_count;                       ///< Number of queue items the queue array may be increased by.
    int queue_cur_grow_count;                   ///< Number of times the current queue has been increased.
    int queue_max_grow_count;                   ///< The maximum number of times the queue can be increased.
    CdiSinglyLinkedList allocated_buffer_list;  ///< Linked list of allocated memory buffers.

    CdiSinglyLinkedListEntry* entry_read_ptr;   ///< Current read entry in the circular queue
    CdiSinglyLinkedListEntry* entry_write_ptr;  ///< Current write entry in the circular queue

    CdiSignalType wake_pop_waiters_signal;      ///< If enabled, signal set whenever item is pushed.
    CdiSignalType wake_push_waiters_signal;     ///< If enabled, signal set whenever item is popped.

    CdiCsID multiple_writer_cs;                 ///< Optional mutex used to make pushing to queue thread safe.

#ifdef DEBUG
    int occupancy;                              ///< The number of entries currently enqueued.
    CdiQueueCallback debug_cb_ptr;              ///< Pointer to user-provided debug callback function
#endif
} QueueState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Get pointer to the item's parent object (QueueItem).
 *
 * @param item_data_ptr Pointer to object being queried.
 *
 * @return Pointer to parent of object.
 */
static inline QueueItem* GetQueueItemFromItemDataPointer(const void* item_data_ptr)
{
    return CONTAINER_OF(item_data_ptr, QueueItem, item_data_buffer);
}

/**
 * @brief Get pointer to the data buffer related to the linked list entry of the item.
 *
 * @param entry_item_ptr Pointer to queue item.
 *
 * @return Pointer to item's data buffer.
 */
static inline uint8_t* GetDataItemFromListEntry(CdiSinglyLinkedListEntry* entry_item_ptr)
{
    QueueItem* queue_item_ptr = (QueueItem*)entry_item_ptr;
    return queue_item_ptr->item_data_buffer;
}

/**
 * @brief Adds queue item array to allocated buffer and insert into the circular item list at the current write pointer
 * location.
 *
 * @param state_ptr Queue state information.
 * @param queue_item_array The array being attached.
 * @param item_count Number of items in array to be attached.
 */
static inline void AddEntriesToBuffers(QueueState* state_ptr, uint8_t* queue_item_array, int item_count)
{
    // Store this entry into the allocated buffer list.
    CdiSinglyLinkedListEntry* queue_item_array_list_ptr = (CdiSinglyLinkedListEntry*)queue_item_array;
    CdiSinglyLinkedListPushHead(&state_ptr->allocated_buffer_list, queue_item_array_list_ptr);

    // Since we must insert the new entries into the existing list at the write pointer location and not the head, a new
    // list will be created to hold all the new items. Then, the new list can be inserted.
    CdiSinglyLinkedList new_list;
    CdiSinglyLinkedListInit(&new_list);

    // Offset from the link list entry for this array.
    uint8_t* queue_item_array_offset = queue_item_array + sizeof(CdiSinglyLinkedListEntry);
    for (int i = 0; i < item_count; i++) {
        QueueItem* queue_item_ptr = (QueueItem*)(queue_item_array_offset + (uint64_t)state_ptr->queue_item_byte_size * i);
        CdiSinglyLinkedListPushTail(&new_list, &queue_item_ptr->list_entry);
    }

    // Check if the existing list is empty.
    if (NULL == state_ptr->entry_write_ptr) {
        // The existing list is empty. Make the list circular, by pointing the tail back at the head of the list.
        new_list.tail_ptr->next_ptr = new_list.head_ptr;
        // Now, set the write pointer to the start of the list.
        state_ptr->entry_write_ptr = new_list.head_ptr;
    } else {
        // The existing list is not empty, so insert the new list into the existing one. Set the tail of the new list to
        // point at the write entry's next_ptr.
        new_list.tail_ptr->next_ptr = state_ptr->entry_write_ptr->next_ptr;
        // Now, update the write entry's next_ptr to point to the head of the new list.
        state_ptr->entry_write_ptr->next_ptr = new_list.head_ptr;
    }
}

/**
 * @brief Increases the size of a queue, if it is growable.
 *
 * @param handle_ptr Pointer to queue handle.
 *
 * @return If successful true is returned, otherwise false is returned.
 */
static bool QueueIncrease(CdiQueueHandle handle_ptr)
{
    bool ret = true;
    void* queue_item_array = NULL;
    QueueState* state_ptr = (QueueState*)handle_ptr;

    if (0 == state_ptr->queue_grow_count) {
        // Queue is not growable. Don't assert() here or generate a log message, since it is ok to be full.
        ret = false;
    } else {
        // NOTE: Queue is growable, so attempt to increase its size. Some growth is allowed at runtime to prevent a
        // fatal error from occurring. However, this is not a normal condition, logged as a warning and the reason why
        // the queue is growing should be understood and corrected. In most cases, this means simply increasing the
        // initial size of the queue.

        // First check to see if this queue hasn't already exceeded its growth count.
        if (state_ptr->queue_cur_grow_count < state_ptr->queue_max_grow_count) {
            uint32_t size_needed = sizeof(CdiSinglyLinkedListEntry) + (state_ptr->queue_grow_count * (sizeof(QueueItem) +
                                state_ptr->queue_item_byte_size));
            queue_item_array = CdiOsMemAllocZero(size_needed);
            if (NULL == queue_item_array) {
                CDI_LOG_THREAD(kLogError, "Not enough memory to increase allocation to queue[%s] by size[%d] items.",
                               state_ptr->name_str, state_ptr->queue_cur_grow_count);
                assert(false); // Catch this as an error and halt execution in a debug build.
                ret = false;
            } else {
                state_ptr->queue_item_count += state_ptr->queue_grow_count;
                state_ptr->queue_cur_grow_count++;
            }
        } else {
            // User tried to grow this queue too many times.
            CDI_LOG_THREAD(kLogError, "Too many size increases for queue[%s]", state_ptr->name_str);
            assert(false); // Catch this as an error and halt execution in a debug build.
            ret = false;
        }
    }

    if (ret) {
        CDI_LOG_THREAD(kLogWarning, "Queue[%s] increased by[%d] to items count[%d]",
                       state_ptr->name_str, state_ptr->queue_grow_count, state_ptr->queue_item_count);
        AddEntriesToBuffers(state_ptr, (uint8_t*)queue_item_array, (int)state_ptr->queue_grow_count);
    }
    return ret;
}

/**
 * Wait on either an empty or full queue. This is done by using the specified signal to wait for one of the queue
 * read/write pointers to change. The wait can be aborted if any of the signals in the specified signal array get set.
 *
 * @param entry_change_ptr Pointer to either the read or write pointer that is going to be changed outside the scope of
 *                         this function. The contents of this pointer is made a volatile pointer to prevent the
 *                         compiler from caching it in a register. It must be re-read from memory each time it is used
 *                         in this function.
 * @param entry_static_ptr Pointer to the read or write pointer that is not being changed.
 * @param wait_signal Signal that is set when the address pointed to by entry_change_ptr changes.
 * @param timeout_ms Maximume time to wait, in milliseconds.
 * @param cancel_wait_signal_array Array of wait cancel signals.
 * @param num_signals Number of signals in the signal array.
 * @param ret_signal_index_ptr Address where to write the returned index value of the signal that was set.
 *
 * @return Returns true if the wait_signal was set and the contents of the address pointed to by entry_change_ptr
 *         changed.
 */
static inline bool WaitForSignals(CdiSinglyLinkedListEntry* volatile* entry_change_ptr,
                                  CdiSinglyLinkedListEntry* entry_static_ptr, CdiSignalType wait_signal, int timeout_ms,
                                  CdiSignalType* cancel_wait_signal_array, int num_signals,
                                  uint32_t* ret_signal_index_ptr)
{
    bool ret = true;
    uint32_t signal_index = 0;

    // allow one extra signal for the "wait_signal".
    int num_actual_signals = num_signals + 1; // Account for "wait_signal".

    if (num_actual_signals > CDI_MAX_WAIT_MULTIPLE) {
        CDI_LOG_THREAD(kLogError, "Maximum number[%d] of wait signals exceed[%d].", CDI_MAX_WAIT_MULTIPLE,
                       num_actual_signals);
        ret = false;
    } else {
        // Since this logic is only used if we need to wait for a queue item, ok to execute this loop. In most
        // cases, the number of signals is only going to be 1 or 2.
        CdiSignalType signal_ptr[CDI_MAX_WAIT_MULTIPLE];
        signal_ptr[0] = wait_signal;
        for (int i = 0; i < num_signals; i++) {
            signal_ptr[i+1] = cancel_wait_signal_array[i];
        }

        // Exit loop if queue pointer changes, get a signal or timeout.
        while (*entry_change_ptr == entry_static_ptr) {
            CdiOsSignalsWait(signal_ptr, num_actual_signals, false, timeout_ms, &signal_index);
            if (0 != signal_index) {
                // Wait was aborted (not set by "wait_signal") or timed-out (signal_index=CDI_OS_SIG_TIMEOUT).
                if (CDI_OS_SIG_TIMEOUT != signal_index) {
                    // Was not a timeout. Decrement signal index so the index matches the signal_array parameter.
                    signal_index--;
                }
                ret = false;
                break;
            }
        }
    }

    if (ret_signal_index_ptr) {
        *ret_signal_index_ptr = signal_index;
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool CdiQueueCreate(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                    uint32_t item_byte_size, CdiQueueSignalMode signal_mode, CdiQueueHandle* ret_handle)
{
    bool ret = true;

    if (1 > item_count) {
        CDI_LOG_THREAD(kLogError, "Queue[%s] cannot be created with fewer than 1 item, count [%"PRIu32"]", name_str,
                       item_count);
        return false;
    }

    // The implementation does not allow item_count items to occupy the queue. It is deemed to be "full" when it has
    // item_count - 1 items in it. Adjust item_count so that the true size requested is available.
    item_count += 1;

    uint32_t size_needed = sizeof(CdiSinglyLinkedListEntry) + (item_count * (sizeof(QueueItem) + item_byte_size));
    void* queue_item_array = CdiOsMemAllocZero(size_needed);
    if (NULL == queue_item_array) {
        CDI_LOG_THREAD(kLogError, "Not enough memory to allocate queue[%s] with size [%"PRIu32"]", name_str, size_needed);
        return false;
    }

    QueueState* state_ptr = (QueueState*)CdiOsMemAllocZero(sizeof(QueueState));
    if (NULL == state_ptr) {
        CdiOsMemFree(queue_item_array);
        queue_item_array = NULL;
        return false;
    }

    if (ret) {
        CdiOsStrCpy(state_ptr->name_str, sizeof(state_ptr->name_str), name_str);
        state_ptr->queue_grow_count = grow_count;
        state_ptr->queue_max_grow_count = max_grow_count;
        state_ptr->queue_item_data_byte_size = item_byte_size;
        state_ptr->queue_item_byte_size = sizeof(QueueItem) + item_byte_size;
        state_ptr->queue_item_count = item_count;

        // Initialize the allocated buffers.
        CdiSinglyLinkedListInit(&state_ptr->allocated_buffer_list);

        AddEntriesToBuffers(state_ptr, (uint8_t*)queue_item_array, (int)item_count);
        // Set read pointer to match write pointer, so the queue starts empty.
        state_ptr->entry_read_ptr = state_ptr->entry_write_ptr;
    }

    // Mask off option bits leaving only the mode selection.
    const CdiQueueSignalMode signal_mode_masked = signal_mode & kQueueSignalModeMask;

    if (ret && (kQueueSignalPopWait == signal_mode_masked || kQueueSignalPopPushWait == signal_mode_masked)) {
        // Create signal used for blockable CdiQueuePopWait().
        ret = CdiOsSignalCreate(&state_ptr->wake_pop_waiters_signal);
    }

    if (ret && (kQueueSignalPushWait == signal_mode_masked || kQueueSignalPopPushWait == signal_mode_masked)) {
        // Create signal used for blockable CdiQueuePushWait().
        ret = CdiOsSignalCreate(&state_ptr->wake_push_waiters_signal);
    }

    if (ret && ((kQueueMultipleWritersFlag & signal_mode) != 0)) {
        ret = CdiOsCritSectionCreate(&state_ptr->multiple_writer_cs);
    }

    if (!ret) {
        CdiQueueDestroy((CdiQueueHandle)state_ptr);
        state_ptr = NULL;
    }

    *ret_handle = (CdiQueueHandle)state_ptr;

    return ret;
}

bool CdiQueuePop(CdiQueueHandle handle, void* item_dest_ptr)
{
    QueueState* state_ptr = (QueueState*)handle;

    if (NULL != state_ptr->wake_pop_waiters_signal) {
        // Clear signal then use the read/write pointers, in case another thread is using one of the Push functions.
        CdiOsSignalClear(state_ptr->wake_pop_waiters_signal);
    }

    // Use atomic operations to ensure latest memory is being read from.
    CdiSinglyLinkedListEntry* entry_read_ptr = (CdiSinglyLinkedListEntry*)CdiOsAtomicLoadPointer(&state_ptr->entry_read_ptr);
    CdiSinglyLinkedListEntry* entry_write_ptr = (CdiSinglyLinkedListEntry*)CdiOsAtomicLoadPointer(&state_ptr->entry_write_ptr);

    // Check if queue is empty.
    if (entry_read_ptr == entry_write_ptr) {
        return false;
    }

    if (item_dest_ptr) {
        // Copy the data from the queue buffer to the memory pointed to by item_dest_ptr.
        uint8_t* item_data_ptr = GetDataItemFromListEntry(entry_read_ptr);
        memcpy(item_dest_ptr, item_data_ptr, state_ptr->queue_item_data_byte_size);
    }

#ifdef DEBUG
    const int current_occupancy = CdiOsAtomicDec32(&state_ptr->occupancy);

    if (state_ptr->debug_cb_ptr) {
        CdiQueueCbData cb_data = {
            .is_pop = true,
            .read_ptr = entry_read_ptr,
            .write_ptr = entry_write_ptr,
            .item_data_ptr = item_dest_ptr,
            .occupancy = current_occupancy,
        };
        (state_ptr->debug_cb_ptr)(&cb_data);
    }
#endif

    // Advance the read pointer. Use an atomic operation to ensure the memcpy above has copied all the data to memory
    // before this variable gets changed.
    CdiOsAtomicStorePointer(&state_ptr->entry_read_ptr, CdiSinglyLinkedListNextEntry(entry_read_ptr));

    // If blockable push was enabled upon creation, set the signal to wake-up any waiting threads.
    if (state_ptr->wake_push_waiters_signal) {
        CdiOsSignalSet(state_ptr->wake_push_waiters_signal);
    }

    return true;
}

bool CdiQueuePopWait(CdiQueueHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, void* item_dest_ptr)
{
    return CdiQueuePopWaitMultiple(handle, timeout_ms, &abort_wait_signal, 1, NULL, item_dest_ptr);
}

bool CdiQueuePopWaitMultiple(CdiQueueHandle handle, int timeout_ms, CdiSignalType* abort_wait_signal_array,
                             int num_signals, uint32_t* ret_signal_index_ptr, void* item_dest_ptr)
{
    bool ret = true;
    QueueState* state_ptr = (QueueState*)handle;

    if (NULL == state_ptr->wake_pop_waiters_signal) {
        CDI_LOG_THREAD(kLogError,
                       "Queue[%s] not configured for PopWait signal. See CdiQueueCreate().", state_ptr->name_str);
        return false;
    }

    // Wait here until an entry has been popped, get an abort signal or a timeout.
    while (ret && !CdiQueuePop(handle, item_dest_ptr)) {
        // Queue is empty, so setup to wait for an item to be pushed to it.
        ret = WaitForSignals(&state_ptr->entry_write_ptr, state_ptr->entry_read_ptr,
                             state_ptr->wake_pop_waiters_signal, timeout_ms, abort_wait_signal_array, num_signals,
                             ret_signal_index_ptr);
    }

    return ret;
}

bool CdiQueuePush(CdiQueueHandle handle, const void* data_ptr)
{
    bool ret = true;
    QueueState* state_ptr = (QueueState*)handle;

    if (state_ptr->multiple_writer_cs) {
        CdiOsCritSectionReserve(state_ptr->multiple_writer_cs);
    }

    // Use atomic operations to ensure latest memory is being read from.
    CdiSinglyLinkedListEntry* entry_read_ptr = (CdiSinglyLinkedListEntry*)CdiOsAtomicLoadPointer(&state_ptr->entry_read_ptr);
    CdiSinglyLinkedListEntry* entry_write_ptr = (CdiSinglyLinkedListEntry*)CdiOsAtomicLoadPointer(&state_ptr->entry_write_ptr);

    CdiSinglyLinkedListEntry* new_write_ptr = CdiSinglyLinkedListNextEntry(entry_write_ptr);

    // Check if queue is full.
    if (new_write_ptr == entry_read_ptr) {
        // Queue is full. Try to grow it.
        ret = QueueIncrease(handle);
        if (ret) {
            // Successfully grew the queue, so get new write pointer since growing it changes the value. Use atomic
            // operation to ensure latest memory is being read from.
            entry_write_ptr = (CdiSinglyLinkedListEntry*)CdiOsAtomicLoadPointer(&state_ptr->entry_write_ptr);
            new_write_ptr = CdiSinglyLinkedListNextEntry(entry_write_ptr);
        }
    }

    if (ret) {
        // Copy the data to the queue buffer before updating the write pointer, so the read operation always has valid
        // data.
        uint8_t* item_dest_ptr = GetDataItemFromListEntry(entry_write_ptr);
        memcpy(item_dest_ptr, data_ptr, state_ptr->queue_item_data_byte_size);

#ifdef DEBUG
        const int current_occupancy = CdiOsAtomicInc32(&state_ptr->occupancy);

        if (state_ptr->debug_cb_ptr) {
            CdiQueueCbData cb_data = {
                .is_pop = false,
                .read_ptr = entry_read_ptr,
                .write_ptr = entry_write_ptr,
                .item_data_ptr = item_dest_ptr,
                .occupancy = current_occupancy,
            };
            (state_ptr->debug_cb_ptr)(&cb_data);
        }
#endif

        // Update the write pointer. Use an atomic operation to ensure the data written above by the memcpy has been
        // completely written to memory before this variable gets changed.
        CdiOsAtomicStorePointer(&state_ptr->entry_write_ptr, new_write_ptr);

        // If blockable pop was enabled upon creation, set the signal to wake-up any waiting threads.
        if (state_ptr->wake_pop_waiters_signal) {
            CdiOsSignalSet(state_ptr->wake_pop_waiters_signal);
        }
    }

    if (state_ptr->multiple_writer_cs) {
        CdiOsCritSectionRelease(state_ptr->multiple_writer_cs);
    }

    return ret;
}

bool CdiQueuePushWait(CdiQueueHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, const void* item_ptr)
{
    return CdiQueuePushWaitMultiple(handle, timeout_ms, &abort_wait_signal, 1, NULL, item_ptr);
}

bool CdiQueuePushWaitMultiple(CdiQueueHandle handle, int timeout_ms, CdiSignalType* signal_array, int num_signals,
                              uint32_t* ret_signal_index_ptr, const void* item_ptr)
{
    bool ret = true;
    QueueState* state_ptr = (QueueState*)handle;

    if (NULL == state_ptr->wake_push_waiters_signal) {
        CDI_LOG_THREAD(kLogError,
                       "Queue[%s] not configured for PushWait signal. See CdiQueueCreate().", state_ptr->name_str);
        return false;
    }

    // Clear signal and then use the entry write/read pointers, in case another thread is using one of the Pop API
    // functions.
    CdiOsSignalClear(state_ptr->wake_push_waiters_signal);

    CdiSinglyLinkedListEntry* new_write_ptr = CdiSinglyLinkedListNextEntry(state_ptr->entry_write_ptr);

    // Wait here until the entry is pushed, get an abort signal or a timeout.
    while (ret && !CdiQueuePush(handle, item_ptr)) {
        // Queue is full, so setup to wait for an item to be popped from it.
        ret = WaitForSignals(&state_ptr->entry_read_ptr, new_write_ptr, state_ptr->wake_push_waiters_signal,
                             timeout_ms, signal_array, num_signals, ret_signal_index_ptr);
    }

    return ret;
}

void CdiQueueFlush(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;
    CdiOsAtomicStorePointer(&state_ptr->entry_read_ptr, CdiOsAtomicLoadPointer(&state_ptr->entry_write_ptr));
}

bool CdiQueueIsEmpty(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;
    // Use atomic operations to ensure latest memory is being read from.
    return (CdiOsAtomicLoadPointer(&state_ptr->entry_read_ptr) == CdiOsAtomicLoadPointer(&state_ptr->entry_write_ptr));
}

CdiSignalType CdiQueueGetPushWaitSignal(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;

    if (state_ptr) {
        assert(NULL != state_ptr->wake_push_waiters_signal);
        return state_ptr->wake_push_waiters_signal; // Signal that is used to wait in CdiQueuePushWait().
    }

    return NULL;
}

CdiSignalType CdiQueueGetPopWaitSignal(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;

    if (state_ptr) {
        assert(NULL != state_ptr->wake_pop_waiters_signal);
        return state_ptr->wake_pop_waiters_signal; // Signal that is used to wait in CdiQueuePopWait().
    }

    return NULL;
}

const char* CdiQueueGetName(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;

    if (state_ptr) {
        return state_ptr->name_str;
    }

    return NULL;
}

#ifdef DEBUG
void CdiQueueDebugEnable(CdiQueueHandle handle, CdiQueueCallback cb_ptr)
{
    QueueState* state_ptr = (QueueState*)handle;

    state_ptr->debug_cb_ptr = cb_ptr;
}

void CdiQueueDebugDisable(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;

    state_ptr->debug_cb_ptr = NULL;
}
#endif //DEBUG

void CdiQueueDestroy(CdiQueueHandle handle)
{
    QueueState* state_ptr = (QueueState*)handle;

    if (state_ptr) {
        // Ensure that the queue is empty.
        assert(state_ptr->entry_read_ptr == state_ptr->entry_write_ptr);

        CdiSinglyLinkedListEntry* allocated_buffer_ptr = state_ptr->allocated_buffer_list.head_ptr;

        // Free up each of the allocated buffers.
        while (allocated_buffer_ptr) {
            CdiSinglyLinkedListEntry* next_allocated_buffer_ptr = CdiSinglyLinkedListNextEntry(allocated_buffer_ptr);
            CdiOsMemFree(allocated_buffer_ptr);
            allocated_buffer_ptr = next_allocated_buffer_ptr;
        }

        if (state_ptr->multiple_writer_cs) {
            CdiOsCritSectionDelete(state_ptr->multiple_writer_cs);
            state_ptr->multiple_writer_cs = NULL;
        }

        CdiOsSignalDelete(state_ptr->wake_push_waiters_signal);
        state_ptr->wake_push_waiters_signal = NULL;

        CdiOsSignalDelete(state_ptr->wake_pop_waiters_signal);
        state_ptr->wake_pop_waiters_signal = NULL;

        CdiOsMemFree(state_ptr);
    }
}
