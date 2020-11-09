// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and implementation for a memory pool.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_pool_api.h"

#include <assert.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "internal_log.h"
#include "singly_linked_list_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This structure represents a single pool item.
 */
typedef struct {
    CdiSinglyLinkedListEntry list_entry; ///< List entry for this pool item.
    CdiListEntry in_use_list_entry;      ///< Doubly linked list entry for the in_use list.
    uint8_t item_data_buffer[];          ///< Pointer to data space for this pool item.
} CdiPoolItem;

/**
 * @brief This structure represents the current state of a memory pool.
 */
typedef struct {
    char name_str[MAX_POOL_NAME_LENGTH];       ///< Name of pool, used for informational purposes only.
    int pool_item_data_byte_size;              ///< Size of the data portion of each item in bytes.
    int pool_item_byte_size;                   ///< Size of each item in bytes (sizeof(CdiPoolItem) + data portion).
    int pool_item_count;                       ///< Number of items in the pool array.
    int pool_grow_count;                       ///< Number of pool items the pool array may be increased by.
    int pool_cur_grow_count;                   ///< Number of times the current pool has been increased.
    int pool_max_grow_count;                   ///< The maximum number of times the pool can be increased.
    CdiPoolItemOperatorFunction init_fn_ptr;   ///< Pointer to initialization function that this memory pool may have.
    void* init_context_ptr;                    ///< Pointer used by initialization function that this memory pool may have.
    bool is_existing_buffer;                   ///< Using an existing buffer. Don't free the memory when pool destroyed.
    CdiSinglyLinkedList allocated_buffer_list; ///< Linked list of memory pools.
    CdiSinglyLinkedList free_list;             ///< List of free items.
    CdiList in_use_list;                       ///< Doubly linked list of items currently in use.
    CdiCsID lock;                              ///< Lock used to protect multi-thread access the pool.

#ifdef DEBUG
    CdiPoolCallback debug_cb_ptr;              ///< Pointer to user-provided debug callback function
#endif
} CdiPoolState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * If lock used to protect pool resources from multi-threaded access exists, reserve it.
 *
 *@param state_ptr Pool state information for the lock.
 */
static inline void MultithreadedReserve(CdiPoolState* state_ptr)
{
    if (state_ptr->lock) {
        CdiOsCritSectionReserve(state_ptr->lock);
    }
}

/**
 * If lock used to protect pool resources from multi-threaded access exists, release it.
 *
 *@param state_ptr Pool state information for the lock.
 */
static inline void MultithreadedRelease(CdiPoolState* state_ptr)
{
    if (state_ptr->lock) {
        CdiOsCritSectionRelease(state_ptr->lock);
    }
}

/**
 * @brief Get pointer to the item's parent object (CdiPoolItem).
 *
 * @param item_ptr Pointer to object being queried.
 *
 * @return Pointer to parent of object.
 */
static inline CdiPoolItem* GetPoolItemFromItemDataPointer(const void* item_ptr)
{
    return CONTAINER_OF(item_ptr, CdiPoolItem, item_data_buffer);
}

/**
 * @brief Get pointer to the item's data given the address of the pool item (CdiPoolItem).
 *
 * @param pool_item_ptr Pointer to pool item.
 *
 * @return Pointer to item's data.
 */
static inline uint8_t* GetDataItem(CdiPoolItem* pool_item_ptr)
{
    return pool_item_ptr->item_data_buffer;
}

/**
 * @brief Adds pool item array to allocated buffer and free list.
 *
 * @param state_ptr Pool state information for lists and init function.
 * @param pool_item_array The array being attached.
 * @param item_count Number of items in array to be attached.
 *
 * @return true if all of the entries were added and the optional initialization function returned true for every item
 *         added, false if not.
 */
static inline bool AddEntriesToBuffers(CdiPoolState* state_ptr, uint8_t* pool_item_array, int item_count)
{
    bool ret = true;

    // Store this entry into the allocated buffer list.
    CdiSinglyLinkedListEntry* pool_item_array_list_ptr = (CdiSinglyLinkedListEntry*)pool_item_array;
    CdiSinglyLinkedListPushHead(&state_ptr->allocated_buffer_list, pool_item_array_list_ptr);

    // Offset from the link list entry for this array.
    uint8_t* pool_item_array_offset = pool_item_array + sizeof(CdiSinglyLinkedListEntry);
    for (int i = 0; ret && i < item_count; i++) {
        CdiPoolItem* pool_item_ptr = (CdiPoolItem*)(pool_item_array_offset + state_ptr->pool_item_byte_size * i);
        CdiSinglyLinkedListPushHead(&state_ptr->free_list, &pool_item_ptr->list_entry);
        if (state_ptr->init_fn_ptr != NULL) {
            ret = state_ptr->init_fn_ptr(state_ptr->init_context_ptr, GetDataItem(pool_item_ptr));
        }
    }

    return ret;
}

/**
 * @brief Creates a memory pool and returns ret_handle, which is a pointer to create memory pool.
 *
 * @param name_str Name of memory pool.
 * @param item_count Number of items in the pool.
 * @param grow_count Number of items that a pool may be increased by if the initial size requested is inadequate.
 * @param max_grow_count Maximum number of times a pool may be increased before an error occurs.
 * @param item_byte_size Size of each item in bytes.
 * @param thread_safe If true, locks are used to protect resources from multi-threaded access, otherwise locks are not
 *                    used and single threaded access to all resources is required.
 * @param pool_item_array Pointer to data buffer for the memory pool.
 * @param is_existing_buffer If true, using an existing buffer so don't free the memory when the pool is destroyed.
 * @param ret_handle_ptr Pointer to returned handle of the new pool.
 * @param init_fn The address of a function that will be called for each item in the pool at creation time; a value of
 *                NULL indicates that no initialization beyond zeroing the memory is to be done.
 * @param init_context_ptr A value to provide as init_context to init_fn().
 */
static bool PoolCreate(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                       uint32_t item_byte_size, bool thread_safe, void* pool_item_array, bool is_existing_buffer,
                       CdiPoolHandle* ret_handle_ptr, CdiPoolItemOperatorFunction init_fn, void* init_context_ptr)
{
    bool ret = true;

    CdiPoolState* state_ptr = (CdiPoolState*)CdiOsMemAllocZero(sizeof(CdiPoolState));
    if (NULL == state_ptr) {
        ret = false;
    }

    if (ret && thread_safe) {
        // Create critical section.
        if (!CdiOsCritSectionCreate(&state_ptr->lock)) {
            ret = false;
        }
    }

    if (ret) {
        CdiOsStrCpy(state_ptr->name_str, sizeof(state_ptr->name_str), name_str);
        state_ptr->pool_grow_count = grow_count;
        state_ptr->pool_max_grow_count = max_grow_count;
        state_ptr->pool_item_data_byte_size = item_byte_size;
        state_ptr->pool_item_byte_size = sizeof(CdiPoolItem) + item_byte_size;
        state_ptr->pool_item_count = item_count;
        state_ptr->is_existing_buffer = is_existing_buffer;
        state_ptr->init_fn_ptr = init_fn;
        state_ptr->init_context_ptr = init_context_ptr;

        // Initialize the allocated buffers.
        CdiSinglyLinkedListInit(&state_ptr->allocated_buffer_list);

        // Initialize the free list.
        CdiSinglyLinkedListInit(&state_ptr->free_list);

        // Initialize the in use list. We use a doubly-linked list so we can remove items from any location within
        // the list without walking it.
        CdiListInit(&state_ptr->in_use_list);

        ret = AddEntriesToBuffers(state_ptr, (uint8_t*)pool_item_array, (int)item_count);
        if (!ret) {
            CDI_LOG_THREAD(kLogError, "Pool[%s] adding initial entries to pool failed.",
                           state_ptr->name_str, state_ptr->pool_grow_count, state_ptr->pool_item_count);
        }
    }

    if (!ret) {
        CdiPoolDestroy((CdiPoolHandle)state_ptr);
        state_ptr = NULL;
    }

    *ret_handle_ptr = (CdiPoolHandle)state_ptr;

    return ret;
}

/**
 * @brief Increases a memory pool. NOTE: this function assumes that MultiThreadedReserve() has been called first.
 *
 * @param handle_ptr Pointer to pool handle.
 *
 * @return If successful true is returned, otherwise false is returned.
 */
static bool PoolIncrease(CdiPoolHandle handle_ptr)
{
    bool ret = true;
    void* pool_item_array = NULL;
    CdiPoolState* state_ptr = (CdiPoolState*)handle_ptr;

    // First check to see if this memory pool hasn't already exceeded its growth count.
    if (state_ptr->pool_cur_grow_count < state_ptr->pool_max_grow_count) {
        uint32_t size_needed = CdiPoolGetSizeNeeded(state_ptr->pool_grow_count, state_ptr->pool_item_byte_size);
        pool_item_array = CdiOsMemAllocZero(size_needed);
        if (NULL == pool_item_array) {
            CDI_LOG_THREAD(kLogError, "Not enough memory to increase allocation to pool[%s] by size[%d] items.",
                           state_ptr->name_str, state_ptr->pool_cur_grow_count);
            ret = false;
        } else {
            state_ptr->pool_item_count += state_ptr->pool_grow_count;
            state_ptr->pool_cur_grow_count++;
        }
    } else {
        // Don't want to log an error if the pool is not growable. When a non-growable pool is full, it should silently
        // return a warning to the caller.
        if (state_ptr->pool_max_grow_count) {
            // User tried to grow this pool too many times.
            CDI_LOG_THREAD(kLogError, "Too many size increases for pool[%s].", state_ptr->name_str);
        }
        ret = false;
    }

    if (ret) {
        ret = AddEntriesToBuffers(state_ptr, (uint8_t*)pool_item_array, (int)state_ptr->pool_grow_count);
        if (ret) {
            CDI_LOG_THREAD(kLogWarning, "Pool[%s] increased by[%d] to items count[%d].",
                           state_ptr->name_str, state_ptr->pool_grow_count, state_ptr->pool_item_count);
        } else {
            CDI_LOG_THREAD(kLogError, "Pool[%s] adding entries to pool failed.",
                           state_ptr->name_str, state_ptr->pool_grow_count, state_ptr->pool_item_count);
        }
    }
    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

uint32_t CdiPoolGetSizeNeeded(uint32_t item_count, uint32_t item_byte_size)
{
    // Each item in the pool requires storage for an CdiPoolItem structure plus the data itself. Each group of buffer
    // allocations (one initially plus one for each increase) requires a singly linked list entry.
    return sizeof(CdiSinglyLinkedListEntry) + (item_count * (sizeof(CdiPoolItem) + item_byte_size));
}

bool CdiPoolCreate(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                   uint32_t item_byte_size, bool thread_safe, CdiPoolHandle* ret_handle_ptr)
{
    // Have the function which takes an initialization function do the hard work.
    return CdiPoolCreateAndInitItems(name_str, item_count, grow_count, max_grow_count, item_byte_size, thread_safe,
                                     ret_handle_ptr, NULL, NULL);
}

bool CdiPoolCreateAndInitItems(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                               uint32_t item_byte_size, bool thread_safe, CdiPoolHandle* ret_handle_ptr,
                               CdiPoolItemOperatorFunction init_fn, void* init_context_ptr)
{
    uint32_t size_needed = CdiPoolGetSizeNeeded(item_count, item_byte_size);
    void* pool_item_array = CdiOsMemAllocZero(size_needed);
    if (NULL == pool_item_array) {
        CDI_LOG_THREAD(kLogError, "Not enough memory to allocate pool[%s] with size[%d]", name_str, size_needed);
        return false;
    }

    return PoolCreate(name_str, item_count, grow_count, max_grow_count, item_byte_size, thread_safe, pool_item_array,
                      false, ret_handle_ptr, init_fn, init_context_ptr);
}

bool CdiPoolCreateUsingExistingBuffer(const char* name_str, uint32_t item_count, uint32_t item_byte_size,
                                      bool thread_safe, void* buffer_ptr, uint32_t buffer_byte_size,
                                      uint32_t* buffer_byte_size_needed_ptr, CdiPoolHandle* ret_handle_ptr)
{
    // Have the function which takes an initialization function do the hard work.
    return CdiPoolCreateUsingExistingBufferAndInitItems(name_str, item_count, item_byte_size, thread_safe, buffer_ptr,
                                                        buffer_byte_size, buffer_byte_size_needed_ptr, ret_handle_ptr,
                                                        NULL, NULL);
}

bool CdiPoolCreateUsingExistingBufferAndInitItems(const char* name_str, uint32_t item_count, uint32_t item_byte_size,
                                                  bool thread_safe, void* buffer_ptr, uint32_t buffer_byte_size,
                                                  uint32_t* buffer_byte_size_needed_ptr, CdiPoolHandle* ret_handle_ptr,
                                                  CdiPoolItemOperatorFunction init_fn, void* init_context_ptr)
{
    bool ret = true;
    uint32_t size_needed = CdiPoolGetSizeNeeded(item_count, item_byte_size);

    if (buffer_byte_size_needed_ptr) {
        *buffer_byte_size_needed_ptr = size_needed;
    }

    if (buffer_ptr) {
        if (buffer_byte_size >= size_needed) {
            ret = PoolCreate(name_str, item_count, 0, 0, item_byte_size, thread_safe, buffer_ptr, true, ret_handle_ptr,
                             init_fn, init_context_ptr);
        } else {
            CDI_LOG_THREAD(kLogError, "Buffer[%s] size requested is larger than existing buffer. Requested size[%d]"
                           " Available size[%d]", name_str, size_needed, buffer_byte_size);
            ret = false;
        }
    }

    return ret;
}

void CdiPoolDestroy(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    if (state_ptr) {
        // Check to ensure the free list contains all entries before yanking the memory away.
        if (state_ptr->pool_item_count != CdiSinglyLinkedListSize(&state_ptr->free_list)) {
            CDI_LOG_THREAD(kLogFatal, "Pool[%s] to be destroyed has[%d] entries still in use.", state_ptr->name_str,
                           state_ptr->pool_item_count - CdiSinglyLinkedListSize(&state_ptr->free_list));
            assert(false);
        }

        if (!state_ptr->is_existing_buffer) {
            CdiSinglyLinkedListEntry* allocated_buffer_ptr = state_ptr->allocated_buffer_list.head_ptr;

            // Free up each of the allocated buffers.
            while (allocated_buffer_ptr) {
                CdiSinglyLinkedListEntry* next_allocated_buffer_ptr = allocated_buffer_ptr->next_ptr;
                CdiOsMemFree(allocated_buffer_ptr);
                allocated_buffer_ptr = next_allocated_buffer_ptr;
            }
        }

        CdiOsCritSectionDelete(state_ptr->lock);
        CdiOsMemFree(state_ptr);
    }
}

bool CdiPoolPeekInUse(CdiPoolHandle handle, void** ret_item_ptr)
{
    bool ret = true;
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    if (state_ptr) {
        MultithreadedReserve(state_ptr);

        CdiListEntry* list_entry_ptr = CdiListPeek(&state_ptr->in_use_list);
        if (NULL == list_entry_ptr) {
            *ret_item_ptr = NULL;
            ret = false;
        } else {
            CdiPoolItem* pool_item_ptr = (CdiPoolItem*)CONTAINER_OF(list_entry_ptr, CdiPoolItem, in_use_list_entry);
            *ret_item_ptr = GetDataItem(pool_item_ptr);
        }

        MultithreadedRelease(state_ptr);
    }

    return ret;
}

bool CdiPoolGet(CdiPoolHandle handle, void** ret_item_ptr)
{
    bool ret = true;
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    MultithreadedReserve(state_ptr);

    CdiPoolItem* pool_item_ptr = (CdiPoolItem*)CdiSinglyLinkedListPopHead(&state_ptr->free_list);
    if (pool_item_ptr == NULL) {
        // No items left, attempt to increase pool.
        if (!PoolIncrease(handle)) {
            *ret_item_ptr = NULL;
            ret = false;
        } else {
            pool_item_ptr = (CdiPoolItem*)CdiSinglyLinkedListPopHead(&state_ptr->free_list);
            *ret_item_ptr = GetDataItem(pool_item_ptr);
        }
    } else {
        *ret_item_ptr = GetDataItem(pool_item_ptr);
    }

#ifdef DEBUG
    if (state_ptr->debug_cb_ptr) {
        CdiPoolCbData cb_data = {
            .is_put = false,
            .num_entries = CdiSinglyLinkedListSize(&state_ptr->free_list),
            .item_data_ptr = *ret_item_ptr
        };
        (state_ptr->debug_cb_ptr)(&cb_data);
    }
#endif

    if (pool_item_ptr) {
        // Add the item to the in use list.
        CdiListAddHead(&state_ptr->in_use_list, &pool_item_ptr->in_use_list_entry);
    }

    MultithreadedRelease(state_ptr);

    return ret;
}

void CdiPoolPut(CdiPoolHandle handle, const void* item_ptr)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    CdiPoolItem* pool_item_ptr = GetPoolItemFromItemDataPointer(item_ptr);

    MultithreadedReserve(state_ptr);

#ifdef DEBUG
    if (state_ptr->debug_cb_ptr) {
        CdiPoolCbData cb_data = {
            .is_put = true,
            .num_entries = CdiSinglyLinkedListSize(&state_ptr->free_list),
            .item_data_ptr = item_ptr
        };
        (state_ptr->debug_cb_ptr)(&cb_data);
    }
#endif

    // Add the item back to the free list and remove from the in use list.
    CdiSinglyLinkedListPushHead(&state_ptr->free_list, &pool_item_ptr->list_entry);
    CdiListRemove(&state_ptr->in_use_list, &pool_item_ptr->in_use_list_entry);

    MultithreadedRelease(state_ptr);
}

void CdiPoolPutAll(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    if (state_ptr) {
        MultithreadedReserve(state_ptr);

        // Walk the pool list and free all the entries.
        void* entry_ptr = NULL;
        while (CdiPoolPeekInUse(handle, (void**)&entry_ptr)) {
            CdiPoolPut(handle, entry_ptr);
        }

        MultithreadedRelease(state_ptr);
    }
}

bool CdiPoolGetBulk(CdiPoolHandle handle, uint32_t item_count, void** ret_item_array)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    (void)state_ptr;
    (void)item_count;
    (void)ret_item_array;

    MultithreadedReserve(state_ptr);
    MultithreadedRelease(state_ptr);

    return false;
}

bool CdiPoolPutBulk(CdiPoolHandle handle, uint32_t item_count, const void* item_array)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    (void)state_ptr;
    (void)item_count;
    (void)item_array;

    MultithreadedReserve(state_ptr);
    MultithreadedRelease(state_ptr);

    return false;
}

const char* CdiPoolGetName(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    return state_ptr->name_str;
}

uint32_t CdiPoolGetItemSize(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    return state_ptr->pool_item_byte_size;
}

int CdiPoolGetFreeItemCount(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    return CdiSinglyLinkedListSize(&state_ptr->free_list);
}

bool CdiPoolForEachItem(CdiPoolHandle handle, CdiPoolItemOperatorFunction operator_function, const void* context_ptr)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;
    bool ret = true;
    MultithreadedReserve(state_ptr);

    if (state_ptr->pool_item_count != state_ptr->free_list.num_entries) {
        CDI_LOG_THREAD(kLogFatal, "For each on pool[%s] has[%d] entries still in use.", state_ptr->name_str,
                       state_ptr->pool_item_count - state_ptr->free_list.num_entries);
        assert(false);
        ret = false;
    } else {
        for (CdiSinglyLinkedListEntry* entry_ptr = state_ptr->free_list.head_ptr ; NULL != entry_ptr ;
             entry_ptr = entry_ptr->next_ptr) {
            ret = operator_function(context_ptr, GetDataItem((CdiPoolItem*)entry_ptr)) && ret;
        }
    }

    MultithreadedRelease(state_ptr);
    return ret;
}

#ifdef DEBUG
void CdiPoolDebugEnable(CdiPoolHandle handle, CdiPoolCallback cb_ptr)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    state_ptr->debug_cb_ptr = cb_ptr;
}

void CdiPoolDebugDisable(CdiPoolHandle handle)
{
    CdiPoolState* state_ptr = (CdiPoolState*)handle;

    state_ptr->debug_cb_ptr = NULL;
}
#endif // DEBUG
