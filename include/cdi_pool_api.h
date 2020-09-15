// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file declares the public API data types, structures and functions that comprise the CDI Pool Utility API.
 */

// Page for CDI Pool API
/*!
 * @page cdi_pool_api_home_page CDI Pool API
 *
 * @section cdi_pool_api_intro_sec Introduction
 *
 * CDI Pool provides a collection of generic functions for managing memory use. Using CDI Pool functions, users can
 * pre-allocate a memory block sized to fit a given number of items of the user's data type, then reserve and free
 * items from the pool as needed. Pools can be created and zeroed-out, and optionally the contents of each pool
 * item can be initialized using a user-provided callback function. Finally, pools can be set up to grow automatically
 * when they are not large enough to accommodate a new request, and can error out when a provided grow limit is reached.
 *
 * Refer to @ref cdi_pool_api.h for API details.
 *
 * @section cdi_pool_api_example_sec Example
 * As outlined in the API, several functions exist for creating pools. The following example demonstrates the
 * instructions needed to generate a pool of transmit buffers from a pre-allocated buffer space. In this example, the
 * pool function CdiPoolCreateUsingExistingBuffer() is used to allocate payload buffers within the adapter buffer that
 * is reserved when the adapter is initialized.
 *
 * Set defines and initialize variables. Use the CdiPoolHandle type to create a pool handle.
 * @code
   CdiAdapterData* adapter_data_ptr;
   CdiPoolHandle tx_buffer_pool_handle;

   // Set the number of total bytes to reserve from the adapter for payload storage.
   adapter_data_ptr->tx_buffer_size_bytes = TX_PAYLOAD_SIZE_BYTES * TX_NUM_PAYLOAD_BUFFERS;
   @endcode
 *
 * Initialize the adapter interface, and then use the returned ret_tx_buffer_ptr buffer pointer to create a pool of
 * payload buffers which the application's transmit code can pull from later. The pool create function will return the
 * actual number of bytes needed in buffer_bytes_size_needed_ptr.
 * @code
   CdiCoreNetworkAdapterInitialize(adapter_data_ptr, &adapter_handle);
   CdiPoolCreateUsingExistingBuffer("Tx Buffer Pool", TX_NUM_PAYLOAD_BUFFERS, TX_PAYLOAD_SIZE_BYTES,
                                    false, adapter_data_ptr->ret_tx_buffer_ptr,
                                    adapter_data_ptr->tx_buffer_size_bytes, buffer_bytes_size_needed_ptr,
                                    &tx_buffer_pool_handle);
   @endcode
 *
 * Whenever a payload is to be sent, get a payload buffer from the pool. If the pool get fails, use pool functions to
 * get information about the pool to use in a log message.
 * @code
   uint32_t* new_tx_buffer_ptr;
   if (!CdiPoolGet(tx_buffer_pool_handle, (void**)&new_tx_buffer_ptr)) {
       CDI_LOG_THREAD(kLogError, "Unable to get a buffer of [%d] bytes from pool[%s]. There are [%d] items left.",
                       CdiPoolGetItemSize(tx_buffer_pool_handle, CdiPoolGetName(tx_buffer_pool_handle),
                       CdiPoolGetFreeItemCount(tx_buffer_pool_handle));
   }
   @endcode
 *
 * In the case of transmit buffers, the buffers will be used by the SDK until the Tx Callback occurs. Pool items should
 * be put back into the pool during the application's Tx Callback function. Buffers are to be returned to the pool using
 * CdiPoolPut().
 * @code
   CdiPoolPut(tx_buffer_pool_handle, used_buffer_ptr);
   @endcode
 *
 * When the application is shutting down, destroy the pool. Under normal management and use of pool gets and puts,
 * the call to CdiPoolPutAll() is unnecessary, and would probably be masking a coding error, but it can be used as a
 * safeguard if desired.
 * @code
   CdiPoolPutAll(tx_buffer_pool_handle);
   CdiPoolDestroy(tx_buffer_pool_handle);
   @endcode
 */

#ifndef CDI_POOL_API_H__
#define CDI_POOL_API_H__

#include <stdbool.h>
#include <inttypes.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

// #define USE_MEMORY_POOL_APPENDED_LISTS

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a Memory Pool. Each handle represents a instance
 * of a Memory Pool.
 */
typedef struct CdiPoolState* CdiPoolHandle;

/**
 * Prototype of a function for operations on pool members during initialization, destruction, and "for each". This
 * function will be called item_count times, once for each item. (item_count being an argument to CdiPoolCreate() or
 * CdiPoolCreateUsingExistingBuffer().
 *
 * @param context_ptr A pointer value provided as the init_context argument to CdiPoolCreate() or
 *                    CdiPoolCreateUsingExistingBuffer().
 * @param item_ptr The address of the item being added to the pool.
 *
 * @return true if the operation was successful, false if not.
 */
typedef bool (*CdiPoolItemOperatorFunction)(const void* context_ptr, void* item_ptr);

/**
 * @brief Contains the state of a single pool get or put operation.
 */
typedef struct {
    bool is_put;     ///< True if pool get triggered the callback, otherwise a pool put triggered it.
    int num_entries; ///< Current number of entries in the free_list.
    const void* item_data_ptr; ///< Pointer to item data.
} CdiPoolCbData;

/**
 * @brief Prototype of pool debug callback function.
 *
 * This callback function is invoked whenever an item is put to or get from the pool.
 *
 * @param data_ptr A pointer to an CdiFifoCbData structure.
 */
typedef void (*CdiPoolCallback)(const CdiPoolCbData* data_ptr);

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the size of the buffer needed to create a pool for the specified number of items and item size.
 *
 * @param item_count Number of items in the pool.
 * @param item_byte_size Size of each item in bytes.
 *
 * @return Size of buffer in bytes needed to create pool,
 */
uint32_t CdiPoolGetSizeNeeded(uint32_t item_count, uint32_t item_byte_size);

/**
 * Create a new memory pool. Memory is allocated by this function.
 *
 * @param name_str Pointer to name of pool to copy to the new pool instance.
 * @param item_count Number of initial items in the pool.
 * @param grow_count Number of items that a pool will be increased by if the initial size requested is inadequate.
 * @param max_grow_count Maximum number of times a pool may be increased before an error occurs.
 * @param item_byte_size Size of each item in bytes.
 * @param thread_safe If true, locks are used to protect resources from multi-threaded access, otherwise locks are not
 *                    used and single threaded access to all APIs is required.
 * @param ret_handle_ptr Pointer to returned handle of the new pool.
 *
 * @return true if successful, otherwise false (not enough memory).
 */
bool CdiPoolCreate(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                   uint32_t item_byte_size, bool thread_safe, CdiPoolHandle* ret_handle_ptr);

/**
 * Create a new memory pool and initialize each item in it using the provided callback function. Memory is allocated by
 * this function.
 *
 * @param name_str Pointer to name of pool to copy to the new pool instance.
 * @param item_count Number of items in the pool.
 * @param grow_count Number of items that a pool will be increased by if the initial size requested is inadequate.
 * @param max_grow_count Maximum number of times a pool may be increased before an error occurs.
 * @param item_byte_size Size of each item in bytes.
 * @param thread_safe If true, locks are used to protect resources from multi-threaded access, otherwise locks are not
 *                    used and single threaded access to all APIs is required.
 * @param ret_handle_ptr Pointer to returned handle of the new pool.
 * @param init_fn The address of a function that will be called for each item in the pool at creation time; a value of
 *                NULL indicates that no initialization beyond zeroing the memory is to be done. If false is returned by
 *                the function, the pool creation will fail and all the resources allocated in the process of creating
 *                it will be freed.
 * @param init_context_ptr A value to provide as init_context to init_fn().
 *
 * @return true if successful, otherwise false (not enough memory).
 */
bool CdiPoolCreateAndInitItems(const char* name_str, uint32_t item_count, uint32_t grow_count, uint32_t max_grow_count,
                               uint32_t item_byte_size, bool thread_safe, CdiPoolHandle* ret_handle_ptr,
                               CdiPoolItemOperatorFunction init_fn, void* init_context_ptr);

/**
 * Create a new memory pool from a user-provided buffer and initialize each item in it using the provided callback
 * function. Some additional memory is required for each item in the pool to hold data used internally by this pool API.
 * So, the required buffer size must be larger than:
 *
 * item count x item_byte_size
 *
 * To determine the exact size needed, use CdiPoolGetSizeNeeded().
 *
 * @param name_str Pointer to name of pool to copy to the new pool instance.
 * @param item_count Number of items in the pool.
 * @param item_byte_size Size of each item in bytes.
 * @param thread_safe If true, locks are used to protect resources from multi-threaded access, otherwise locks are not
 *                    used and single threaded access to all APIs is required.
 * @param buffer_ptr Pointer to buffer to use for the memory pool.
 * @param buffer_byte_size Size of buffer in bytes.
 * @param buffer_byte_size_needed_ptr Pointer to returned size of buffer actually needed or used. See comments for
 *                                return value below.
 * @param ret_handle_ptr Pointer to returned handle of the new pool.
 *
 * @return true if successful. If false is returned, the value returned in buffer_byte_size_needed will be the
 *  number of bytes needed in order to create the pool. If true is returned, the value returned in
 *  buffer_byte_size_needed will be the number of bytes actually used for the pool.
 */
bool CdiPoolCreateUsingExistingBuffer(const char* name_str, uint32_t item_count, uint32_t item_byte_size,
                                      bool thread_safe, void* buffer_ptr, uint32_t buffer_byte_size,
                                      uint32_t* buffer_byte_size_needed_ptr, CdiPoolHandle* ret_handle_ptr);

/**
 * Create a new memory pool from a user-provided buffer. Some additional memory is required for each item in the pool to
 * hold data used internally by this pool API. So, the required buffer size must be larger than:
 *
 * item count x item_byte_size
 *
 * To determine the exact size needed, use CdiPoolGetSizeNeeded().
 *
 * @param name_str Pointer to name of pool to copy to the new pool instance.
 * @param item_count Number of items in the pool.
 * @param item_byte_size Size of each item in bytes.
 * @param thread_safe If true, locks are used to protect resources from multi-threaded access, otherwise locks are not
 *                    used and single threaded access to all APIs is required.
 * @param buffer_ptr Pointer to buffer to use for the memory pool.
 * @param buffer_byte_size Size of buffer in bytes.
 * @param buffer_byte_size_needed_ptr Pointer to returned size of buffer actually needed or used. See comments for
 *                                return value below.
 * @param ret_handle_ptr Pointer to returned handle of the new pool.
 * @param init_fn The address of a function that will be called for each item in the pool at creation time; a value of
 *                NULL indicates that no initialization beyond zeroing the memory is to be done. If false is returned by
 *                the function, the pool creation will fail and all the resources allocated in the process of creating
 *                it will be freed.
 * @param init_context_ptr A value to provide as init_context to init_fn().
 *
 * @return true if successful. If false is returned, the value returned in buffer_byte_size_needed will be the
 *  number of bytes needed in order to create the pool. If true is returned, the value returned in
 *  buffer_byte_size_needed will be the number of bytes actually used for the pool.
 */
bool CdiPoolCreateUsingExistingBufferAndInitItems(const char* name_str, uint32_t item_count, uint32_t item_byte_size,
                                                  bool thread_safe, void* buffer_ptr, uint32_t buffer_byte_size,
                                                  uint32_t* buffer_byte_size_needed_ptr, CdiPoolHandle* ret_handle_ptr,
                                                  CdiPoolItemOperatorFunction init_fn, void* init_context_ptr);

/**
 * Get a pointer to the head of the allocated buffer list. If the list is empty, NULL will be written to ret_item_ptr
 * and false returned.
 *
 * NOTE: Since the returned pointer still resides in the pool, the caller must ensure that other threads cannot use it.
 * This means other threads won't be using CdiPoolPut() for the pool item.
 *
 * @param handle Memory pool handle.
 * @param ret_item_ptr Pointer to returned pointer to buffer.
 *
 * @return true if successful, otherwise false (allocated buffer list is empty).
 */
bool CdiPoolPeekInUse(CdiPoolHandle handle, void** ret_item_ptr);

/**
 * Get a pointer to an available buffer in the pool. If not enough memory is available,
 * memory will be increased if possible.
 *
 * @param handle Memory pool handle.
 * @param ret_item_ptr Pointer to returned pointer to buffer.
 *
 * @return true if successful, otherwise false (no free buffers).
 */
bool CdiPoolGet(CdiPoolHandle handle, void** ret_item_ptr);

#ifdef USE_MEMORY_POOL_APPENDED_LISTS
/**
 * Get a pointer to an available buffer in the pool and append the new item to an existing item in the pool. This
 * allows the entire list of items to be freed by one call to CdiPoolPut() using the first item in the list.
 *
 * @param handle Memory pool handle.
 * @param existing_item_ptr Pointer to existing pool item to append the new item to. Pointer can be NULL if no
 *        existing item exists.
 * @param ret_item_ptr Pointer to returned pointer to buffer.
 *
 * @return true if successful, otherwise false (no free buffers).
 */
bool CdiPoolGetAndAppend(CdiPoolHandle handle, void* existing_item_ptr, void** ret_item_ptr);
#endif

/**
 * Put a buffer back into the pool.
 *
 * @param handle Memory pool handle.
 * @param item_ptr Pointer to buffer to put back.
 */
void CdiPoolPut(CdiPoolHandle handle, const void* item_ptr);

/**
 * Put all the used buffers back into the pool.
 *
 * @param handle Memory pool handle.
 */
void CdiPoolPutAll(CdiPoolHandle handle);

/**
 * Get an array of pointers to available buffers in the pool.
 *
 * @param handle Memory pool handle.
 * @param item_count Number of buffers to get.
 * @param ret_item_array Pointer to returned array of buffer pointers.
 *
 * @return true if successful, otherwise false (not enough free buffers).
 */
bool CdiPoolGetBulk(CdiPoolHandle handle, uint32_t item_count, void** ret_item_array);

/**
 * Put an array of buffers back into the pool.
 *
 * @param handle Memory pool handle.
 * @param item_count Number of buffers to put.
 * @param item_array Pointer to array of buffer pointers to put back.
 *
 * @return true if successful, otherwise false.
 */
bool CdiPoolPutBulk(CdiPoolHandle handle, uint32_t item_count, const void* item_array);

/**
 * Get name of pool that was defined when pool was created.
 *
 * @param handle Pool handle.
 *
 * @return Pointer to NULL terminated name of the pool.
 */
const char* CdiPoolGetName(CdiPoolHandle handle);

/**
 * Get byte size of buffer for a single pool item.
 *
 * @param handle Pool handle.
 *
 * @return Byte size of the buffer.
 */
uint32_t CdiPoolGetItemSize(CdiPoolHandle handle);

/**
 * Get the number of free items currently available in the pool.
 *
 * @param handle Pool handle.
 *
 * @return Number of free items.
 */
int CdiPoolGetFreeItemCount(CdiPoolHandle handle);

/**
 * Call a function for each item in the pool. If any items are allocated from the pool when this function is called, no
 * operations will be performed and false will be returned.
 *
 * @param handle Memory pool handle.
 * @param operator_function Pointer to a function that is to be called once for each item in the pool. The items'
 *                          address and context_ptr are passed in to the function.
 * @param context_ptr This value is passed as the first actual argument to the operator function.
 *
 * @return false if any items are currently allocated from the pool or if operator_function returned false for at least
 *         one item in the pool, otherwise true.
 */
bool CdiPoolForEachItem(CdiPoolHandle handle, CdiPoolItemOperatorFunction operator_function, const void* context_ptr);

#ifdef DEBUG
/**
 * Enable triggering of a user provided callback function whenever CdiPoolGet() or CdiPoolPut() is used. This is
 * typically used to provide debug information to the caller.
 *
 * @param handle pool handle.
 * @param cb_ptr Pointer to callback function.
 */
void CdiPoolDebugEnable(CdiPoolHandle handle, CdiPoolCallback cb_ptr);

/**
 * Disable a previously enabled pool debug callback.
 *
 * @param handle pool handle.
 */
void CdiPoolDebugDisable(CdiPoolHandle handle);
#endif //DEBUG

/**
 * Destroy a memory pool.
 *
 * @param handle Memory pool handle.
 */
void CdiPoolDestroy(CdiPoolHandle handle);

#ifdef __cplusplus
}
#endif

#endif // CDI_POOL_API_H__
