// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in queue.c.
 */

#ifndef CDI_QUEUE_API_H__
#define CDI_QUEUE_API_H__

#include <stdbool.h>
#include <stdint.h>

#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Queue cannot dynamically grow.
#define FIXED_QUEUE_SIZE        (0)

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a queue. Each handle represents a instance of a
 * queue.
 */
typedef struct CdiQueueState* CdiQueueHandle;

/**
 * @brief This enumeration is used in the internal queue structure to indicate the type of OS signals, if any, to use in
 * the queue.
 */
typedef enum {
    kQueueSignalNone, ///< In this mode signals are not used.

    /// @brief In this mode, use CdiQueuePopWait() to block and wait on an empty queue. Use CdiQueueGetPopWaitSignal()
    /// to directly get the signal.
    kQueueSignalPopWait,

    /// @brief In this mode, use CdiQueuePushWait() to block and wait on a full queue. Use CdiQueueGetPushWaitSignal()
    /// to directly get the signal.
    kQueueSignalPushWait,

    kQueueSignalPopPushWait, ///< In this mode, signals are enabled for both push and pop operations (see above).
} CdiQueueSignalMode;

/// Forward declaration of CdiSinglyLinkedListEntry defined in singly_linked_list_api.h
typedef struct CdiSinglyLinkedListEntry CdiSinglyLinkedListEntry;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a queue. Memory is allocated by this function.
 *
 * @param name_str Name of queue.
 * @param item_count Number of items in the queue.
 * @param grow_count Number of items that a queue may be increased by if the initial size requested is inadequate.
 * @param max_grow_count Maximum number of times a queue may be increased before an error occurs.
 * @param item_byte_size Size of each item in bytes.
 * @param signal_mode Sets type of signals, if any, to use.
 * @param ret_handle_ptr Pointer to returned handle of the new queue.
 *
 * @return true if successful, otherwise false is returned.
 */
bool CdiQueueCreate(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                    uint32_t item_byte_size, CdiQueueSignalMode signal_mode, CdiQueueHandle* ret_handle_ptr);

/**
 * Pop an item from the queue buffer and copy to item_dest_ptr. If the queue is empty, false is returned.
 *
 * @param handle Queue handle.
 * @param item_dest_ptr Pointer to buffer where to copy the item to. Size of buffer must be large enough to hold the
 *                      data. Data size was set when the queue was created (see item_byte_size). This is an optional
 *                      parameter, you can pass NULL if you don't care.
 *
 * @return true if successful, otherwise false (queue is empty).
 */
bool CdiQueuePop(CdiQueueHandle handle, void* item_dest_ptr);

/**
 * Pop an item from the queue buffer and copy to the address item_dest_ptr. If the queue is empty, wait until the
 * specified timeout expires or the optional signal gets set.
 *
 * @param handle Queue handle.
 * @param timeout_ms Timeout in mSec can be CDI_INFINITE to wait indefinitely
 * @param abort_wait_signal Signal used to abort waiting.
 * @param item_dest_ptr Pointer to buffer where to copy the item to. Size of buffer must be large enough to hold the
 *                      data. Data size was set when the queue was created (see item_byte_size). This is an optional
 *                      parameter. Pass NULL if you don't care.
 *
 * @return true if successful, otherwise false (queue is empty and timeout expired or signal got set).
 */
bool CdiQueuePopWait(CdiQueueHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, void* item_dest_ptr);

/**
 * Pop an item from the queue buffer and copy to the address item_dest_ptr. If the queue is empty, wait until the
 * specified timeout expires or one of the signals in the signal array gets set.
 *
 * @param handle Queue handle.
 * @param timeout_ms Timeout in mSec can be CDI_INFINITE to wait indefinitely.
 * @param abort_wait_signal_array Array of signals used to abort waiting.
 * @param num_signals Number of signals in the array.
 * @param  ret_signal_index_ptr Pointer to the returned signal index that caused the wait to abort. If a timeout
 *                              occurred, OS_SIG_TIMEOUT is returned. This is an optional parameter. Pass NULL if you
 *                              don't care.
 * @param item_dest_ptr Pointer to buffer where to copy the item to. Size of buffer must be large enough to hold the
 *                      data. Data size was set when the queue was created (see item_byte_size). This is an optional
 *                      parameter. Pass NULL if you don't care.
 *
 * @return true if successful, otherwise false (queue is empty and timeout expired or signal got set).
 */
bool CdiQueuePopWaitMultiple(CdiQueueHandle handle, int timeout_ms, CdiSignalType* abort_wait_signal_array,
                             int num_signals, uint32_t* ret_signal_index_ptr, void* item_dest_ptr);

/**
 * Push an item on the queue. If the queue is full, false is returned.
 *
 * @param handle Queue handle.
 * @param item_ptr Pointer where to copy the item from.
 *
 * @return true if successful, otherwise false (queue is full).
 */
bool CdiQueuePush(CdiQueueHandle handle, const void* item_ptr);

/**
 * Push an item on the queue. If the queue is full, wait until the specified timeout expires or the optional signal gets
 * set.
 *
 * @param handle Queue handle.
 * @param timeout_ms Timeout in mSec can be CDI_INFINITE to wait indefinitely.
 * @param abort_wait_signal Signal used to abort waiting.
 * @param item_ptr Address where to copy the item from.
 *
 * @return true if successful, otherwise false (queue is full and timeout expired or signal got set).
 */
bool CdiQueuePushWait(CdiQueueHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, const void* item_ptr);

/**
 * Push an item on the queue. If the queue is full, wait until the specified timeout expires or one of the signals in
 * the signal array gets set.
 *
 * @param handle Queue handle.
 * @param timeout_ms Timeout in mSec can be CDI_INFINITE to wait indefinitely.
 * @param abort_wait_signal_array Array of signals used to abort waiting.
 * @param num_signals Number of signals in the array.
 * @param ret_signal_index_ptr Pointer to the returned signal index that caused the wait to abort. If a timeout
 *                             occurred, OS_SIG_TIMEOUT is returned. This is an optional parameter. Pass NULL if you
 *                             don't care.
 * @param item_ptr Address where to copy the item from.
 *
 * @return true if successful, otherwise false (queue is empty and timeout expired or signal got set).
 */
bool CdiQueuePushWaitMultiple(CdiQueueHandle handle, int timeout_ms, CdiSignalType* abort_wait_signal_array,
                              int num_signals, uint32_t* ret_signal_index_ptr, const void* item_ptr);

/**
 * Check if queue is empty.
 *
 * @param handle Queue handle.
 *
 * @return Returns true if the queue is empty, otherwise false is returned.
 */
bool CdiQueueIsEmpty(CdiQueueHandle handle);

/**
 * Drain all items in the queue. NOTE: The caller must ensure that other threads cannot use either of the CdiQueuePop()
 * or CdiQueuePush() API functions while using this API function.
 *
 * @param handle Queue handle.
 */
void CdiQueueFlush(CdiQueueHandle handle);

/**
 * If kQueueSignalPopWait or kQueueSignalBoth was specified when the queue was created, this function returns the signal
 * that got set whenever an item is pushed on the queue. It is used to wait in CdiQueuePopWait(). Otherwise, NULL is
 * returned.
 *
 * @param handle Queue handle.
 */
CdiSignalType CdiQueueGetPopWaitSignal(CdiQueueHandle handle);

/**
 * If kQueueSignalPushWait or kQueueSignalBoth was specified when the queue was created, this function returns the
 * signal that got set whenever an item is popped off the queue. It is used to wait in CdiQueuePushWait(). Otherwise,
 * NULL is returned.
 *
 * @param handle Queue handle.
 */
CdiSignalType CdiQueueGetPushWaitSignal(CdiQueueHandle handle);

/**
 * Get name of the queue that was defined when it was created.
 *
 * @param handle Queue handle.
 */
const char* CdiQueueGetName(CdiQueueHandle handle);

/**
 * @brief A structure of this type is passed as the parameter to CdiQueueCallback(). It contains the state of a
 * single queue read or write operation.
 */
typedef struct {
    bool is_pop;        ///< True if read triggered the callback, otherwise a write triggered it.
    CdiSinglyLinkedListEntry* read_ptr;  ///< Current read pointer in the queue.
    CdiSinglyLinkedListEntry* write_ptr; ///< Current write pointer in the queue.
    void* item_data_ptr; ///< Pointer to item data.
} CdiQueueCbData;

/**
 * Prototype of the queue debug callback function.
 *
 * This callback function is invoked whenever an item is written to or read from the queue.
 *
 * @param data_ptr A pointer to an CdiQueueCbData structure.
 */
typedef void (*CdiQueueCallback)(const CdiQueueCbData* data_ptr);

#ifdef DEBUG
/**
 * Enable triggering of a user provided callback function whenever CdiQueuePop() or CdiQueuePush() are
 * used. This is typically used to provide debug information to the caller.
 *
 * @param handle Queue handle.
 * @param cb_ptr Pointer to callback function.
 */
void CdiQueueDebugEnable(CdiQueueHandle handle, CdiQueueCallback cb_ptr);

/**
 * Disable a previously enabled queue debug callback.
 *
 * @param handle Queue handle.
 */
void CdiQueueDebugDisable(CdiQueueHandle handle);
#endif //DEBUG

/**
 * Destroy a queue.
 *
 * @param handle Queue handle.
 */
void CdiQueueDestroy(CdiQueueHandle handle);

#ifdef __cplusplus
}
#endif

#endif // CDI_QUEUE_API_H__
