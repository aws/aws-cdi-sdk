// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in fifo.c.
 */

#ifndef CDI_FIFO_API_H__
#define CDI_FIFO_API_H__

#include <stdbool.h>
#include <stdint.h>

#include "cdi_core_api.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a FIFO. Each handle represents a instance of a
 * FIFO.
 */
typedef struct CdiFifoState* CdiFifoHandle;

/**
 * @brief A structure of this type is passed as the parameter to CdiFifoFullCallback().
 */
typedef struct {
    CdiFifoHandle fifo_handle; ///< FIFO handle.
    CdiUserCbParameter fifo_user_cb_param; ///< User defined callback parameter.
    const void* new_item_data_ptr; ///< Pointer to item trying to be written to FIFO.
    void* head_item_data_ptr; ///< Pointer to current head data item in FIFO.
    void* tail_item_data_ptr; ///< Pointer to current tail data item in FIFO.
} CdiFifoFullCbData;

/**
 * @brief Prototype of FIFO write callback function. It is invoked whenever CdiFifoWrite() is used to write to a full
 * FIFO.
 *
 * @param cb_data_ptr A pointer to an CdiFifoFullCbData structure.
 */
typedef void (*CdiFifoFullCallback)(const CdiFifoFullCbData* cb_data_ptr);

/**
 * @brief A structure of this type is passed as the parameter to CdiFifoCallback(). It contains the state of a
 * single FIFO read or write operation.
 */
typedef struct {
    bool is_read;        ///< True if FIFO read triggered the callback, otherwise a FIFO write triggered it.
    int head_index;      ///< Current head index position in FIFO.
    int tail_index;      ///< Current tail index position in FIFO.
    void* item_data_ptr; ///< Pointer to item data.
} CdiFifoCbData;

/**
 * @brief Prototype of FIFO debug callback function.
 *
 * This callback function is invoked whenever an item is written to or read from the FIFO.
 *
 * @param data_ptr A pointer to an CdiFifoCbData structure.
 */
typedef void (*CdiFifoCallback)(const CdiFifoCbData* data_ptr);

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create a FIFO.
 *
 * @param name_str Pointer to name of FIFO to copy to the new FIFO instance.
 * @param item_count Number of items in the FIFO (FIFO depth).
 * @param item_byte_size Size of each item in bytes.
 * @param full_cb_ptr Address of callback function invoked when the FIFO is full and CdiFifoWrite() is used. Specify
 *                    NULL if not used.
 * @param full_user_cb_param User defined parameter used in structure passed to full_cb_ptr.
 * @param ret_handle_ptr Pointer to returned handle of the new FIFO.
 *
 * @return true if successful, otherwise false (not enough memory to allocate the FIFO).
 */
bool CdiFifoCreate(const char* name_str, int item_count, int item_byte_size, CdiFifoFullCallback full_cb_ptr,
                   CdiUserCbParameter full_user_cb_param, CdiFifoHandle* ret_handle_ptr);

/**
 * Read an item from the FIFO buffer and copy to item_dest_ptr.
 *
 * @param handle FIFO handle.
 * @param timeout_ms If FIFO is empty, this is the number of milliseconds to wait for data before returning.
 * @param abort_wait_signal Optional signal used to abort the read if the FIFO is empty. Use NULL to specific no signal.
 * @param item_dest_ptr Pointer to buffer where to copy the item to. Size of buffer must be large enough to hold the
 *                      data. Data size was set when the FIFO was created (see item_byte_size).
 *
 * @return true if successful, otherwise false (FIFO is empty).
 */
bool CdiFifoRead(CdiFifoHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, void* item_dest_ptr);

/**
 * Flush all entries from the FIFO.
 *
 * @param handle FIFO handle.
 */
void CdiFifoFlush(CdiFifoHandle handle);

/**
 * Write an item to the FIFO.
 *
 * @param handle FIFO handle.
 * @param timeout_ms If FIFO is full, this is the number of milliseconds to wait for an open entry before returning.
 * @param abort_wait_signal Optional signal used to abort the write if the FIFO is full. Use NULL to specific no signal.
 * @param item_ptr Pointer where to copy the item from.
 *
 * @return true if successful, otherwise false (FIFO is full).
 */
bool CdiFifoWrite(CdiFifoHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, const void* item_ptr);

/**
 * Get name of FIFO that was defined when FIFO was created.
 *
 * @param handle FIFO handle.
 */
const char* CdiFifoGetName(CdiFifoHandle handle);

#ifdef DEBUG
/**
 * Enable triggering of a user provided callback function whenever CdiFifoRead() or CdiFifoWrite() is used. This is
 * typically used to provide debug information to the caller.
 *
 * @param handle FIFO handle.
 * @param cb_ptr Pointer to callback function.
 */
void CdiFifoDebugEnable(CdiFifoHandle handle, CdiFifoCallback cb_ptr);

/**
 * Disable a previously enabled FIFO debug callback.
 *
 * @param handle FIFO handle.
 */
void CdiFifoDebugDisable(CdiFifoHandle handle);
#endif //DEBUG

/**
 * Destroy a FIFO.
 *
 * @param handle FIFO handle.
 */
void CdiFifoDestroy(CdiFifoHandle handle);

#endif // CDI_FIFO_API_H__

