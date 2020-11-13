// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions of types needed for the reordering of packets.
 */

#ifndef RX_REORDER_H__
#define RX_REORDER_H__

#include <stdint.h>

#include "internal.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Adds initial entry to payload_state_ptr->reorder_list_ptr.
 *
 * @param payload_sgl_entry_pool_handle Handle to memory pool of payload SGL entries.
 * @param reorder_entries_pool_handle Handle to memory pool of rx_reorder entries.
 * @param payload_state_ptr Current state of the payload, specifically a single rx_reorder entry.
 * @param new_sglist_ptr An SGL to be added to the end of the payload sgl.
 * @param initial_offset First SGL entry will have this offset applied.
 * @param sequence_num The sequence number of this SGL list.
 *
 * @return True if successful.
 */
bool CdiRxPayloadReorderStateInit(CdiPoolHandle payload_sgl_entry_pool_handle,
                                  CdiPoolHandle reorder_entries_pool_handle,
                                  RxPayloadState* payload_state_ptr,
                                  const CdiSgList* new_sglist_ptr, int initial_offset, int sequence_num);

/**
 * @brief Adds an entry to the payload sgl. Also checks for and maintains outstanding packets that are received out of
 * order. If an SGL arrives that is out of order, it will be added to a doubly linked list (reorder list) of
 * outstanding dangling lists.
 *
 * Once all of the data for a payload is received, then the entries for payload_state_ptr->reorder_list_ptr->next_ptr
 * and payload_state_ptr->reorder_list_ptr->prev_ptr will be NULL, otherwise there are dangling lists that have
 * not been attached to the single payload list.
 *
 * @param payload_sgl_entry_pool_handle Handle to memory pool of payload SGL entries.
 * @param reorder_entries_pool_handle Handle to memory pool of rx_reorder entries.
 * @param payload_state_ptr Current state of the payload, specifically a single rx_reorder entry.
 * @param new_sglist_ptr An SGL to be added to the end of the payload sgl.
 * @param initial_offset First SGL entry will have this offset applied
 * @param sequence_num The sequence number of this SGL list.
 *
 * @return True if successful.
 */
bool CdiRxReorder(CdiPoolHandle payload_sgl_entry_pool_handle,
                  CdiPoolHandle reorder_entries_pool_handle,
                  RxPayloadState* payload_state_ptr,
                  const CdiSgList* new_sglist_ptr, int initial_offset, int sequence_num);

/**
 * @brief removes all lists and sgls used in processing the out of order packets
 *
 * @param reorder_list_ptr pointer to a single list entry to start the removal process
 * @param payload_sgl_entry_pool_handle handle to memory pool of sgls
 * @param reorder_entries_pool_handle handle to memory pool of rx reorder lists
 */
void CdiRxReorderFreeLists(CdiReorderList* reorder_list_ptr, CdiPoolHandle payload_sgl_entry_pool_handle,
                                                             CdiPoolHandle reorder_entries_pool_handle);

#endif  // RX_REORDER_H__
