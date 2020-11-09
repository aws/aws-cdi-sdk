// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * When packets are received from the transmitter, they can arrive in any order. The routines here will
 * put the packets in order.
 *
 * For more information, including Theory of Operation:
 * @ref rx_reorder_home_page
 */

// Page for CDI-CORE Rx Reorder
/*!
 * @page rx_reorder_home_page Rx Reorder Home Page
 * @tableofcontents
 * @section theory_of_operation Theory of Operation
 *
 * The Rx Reorder sublock receives an SgList. In each SgList is a pointer to a linked list of SglEntry(s).<br>
 * The first SglEntry has a CDI Header that contains the sequence number of this SgList.
 *
 * While most lists will arrive in order, they may also arrive in a random order. This is the nature of the<br>
 * transfer from the Tx to the Rx through the network.
 *
 * This module will take these out of order SgLists and put them in order. It does so by maintaining a doubly-linked<br>
 * list of type RxReorderList. When an entire payload of data has been received, there should be only one<br>
 * RxReorderList present, which represents the entire payload. If there are more than one RxReorderLists present,<br>
 * then this means that some out of order list(s) have been received that are not properly attached.
 *
 * The figure below shows an SgList received by this module.
 *
 * @image html "rx_reorder_sglist.png"
 * \htmlonly
 * <table width=100>
 * <td align="right"><b>SgList</b>
 * </table>
 * \endhtmlonly
 *
 * The next figure shows an Rx Reorder List. These are the lists that are created and stored when an out of order<br>
 * list is received. They contain an SgList (with the CDI Header removed), a next pointer, a previous pointer, and a top<br>
 * and bottom sequence number. These two numbers represent the top of and bottom of this list, respectively.
 *
 * @image html "rx_reorder_list.png"
 * \htmlonly
 * <table width=150>
 * <td align="right"><b>RxReorderList</b><br>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * @section example Example reorder sequence
 *
 * What follows is an example sequence that illustrates the creation and attachment of RxReorderLists and SgLists.<br>
 * When a new SgList is added to an existing RxReorderList.SgList, it will be represented with a yellow background:
 * @image html "rx_reorder_new_sg_list.png"
 * <b>New SgList attached to RxReorderList.SgList</b>
 * \htmlonly
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * When a new RxReorderList is created it will have a pink background:
 * @image html "rx_reorder_new_rx_reorder_list.png"
 * \htmlonly
 * <table width=175>
 * <td align="right"><b>New RxReorderList</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * The notation of the RxReorderList is "top-bot". E.g. An RxReorderList having 101 at the top and 230 at the bottom<br>
 * is designated as 101-230. A list containing only one element would have the number repeated. E.g. An RxReorderList with<br>
 * only element 7 would be designated 7-7.
 *
 * @subsection specific_example Example arrival sequence: 2, 7, 6, 4, 5, 0, 1, 3.
 * This example payload consists of 8 SgLists.<br><br>
 *
 * Sequence 2 arrives and there are no RxReorderLists, so one is created. The SgList sent is added to the SgList in<br>
 * RxReorderList, and the CDI header is removed.
 *
 * @image html "rx_reorder_ex_2.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 2</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 7 arrives, which is out of order so a new RxReorderList is created for it and that list is attached to the right.
 *
 * @image html "rx_reorder_ex_7.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 7</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 6 arrives, which is belongs at the top of the existing RxReorderList 7-7, so it is placed there.
 *
 * @image html "rx_reorder_ex_6.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 6</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 4 arrives, which does not belong on any existing list, so a new RxReorderList is created for it and that<br>
 * list is placed between the 2-2 and 6-7 reorder lists.
 *
 * @image html "rx_reorder_ex_4.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 4</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 5 arrives, which belongs at the bottom of the existing RxReorderList 4-4, so it is placed there.<br>
 * List 6-7 is then attached to the bottom of list 4-5. The memory resource for list 6-7 is then returned to the pool.
 *
 * @image html "rx_reorder_ex_5.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 5</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 0 arrives, which is out of order so a new RxReorderList is created for it and that list is placed to left of list 2-2.
 *
 * @image html "rx_reorder_ex_0.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 0</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 1 arrives, which belongs at the bottom of the existing RxReorderList 0-0, so it is placed there.<br>
 * List 2-2 is then attached to the bottom of list 0-1. The memory resource for list 2-2 is then returned to the pool.
 *
 * @image html "rx_reorder_ex_1.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 1</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * Sequence 3 arrives, which belongs at the bottom of the existing list 0-2, so it is placed there.<br>
 * List 4-7 is then attached to the bottom of list 0-3. The memory resource for list 4-7 is then returned to the pool.
 *
 * @image html "rx_reorder_ex_3.png"
 * \htmlonly
 * <table width=325>
 * <td align="right"><b>Sequence 3</b>
 * </table>
 * <hr width="900" align="left">
 * \endhtmlonly
 *
 * At this point there is one list (0-7), which represents the entire example payload.
 * <br><br><br><br>
 *
 */

#include <stdbool.h>

#include "rx_reorder.h"

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_pool_api.h"
#include "cdi_raw_api.h"
#include "private.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Adds a scatter-gather list to a reorder list. First entry of the SGL may have an offset.
 *
 * @param payload_sgl_entry_pool_handle Handle for free SGL memory.
 * @param sglist_ptr list Which will be appended to.
 * @param new_sglist_ptr Pointer to entry to be added to list.
 * @param initial_offset First SGL entry will have this offset applied.
 * @param num_bytes_added_ptr Pointer to the number of bytes that were successfully added to list.
 * @return True if adding SGL list is successful.
 */
static bool AddSgListToReorderList(CdiPoolHandle payload_sgl_entry_pool_handle, CdiSgList* sglist_ptr,
                                   const CdiSgList* new_sglist_ptr, int initial_offset, int* num_bytes_added_ptr)
{
    bool ret = true;
    *num_bytes_added_ptr = 0;
    int initial_offset_local = initial_offset;
    for (CdiSglEntry* new_sgl_ptr = new_sglist_ptr->sgl_head_ptr; new_sgl_ptr != NULL;
                      new_sgl_ptr = new_sgl_ptr->next_ptr) {
#ifdef DEBUG_INTERNAL_SGL_ENTRIES
        CdiCDIPacketCommonHeader* common_hdr_ptr = new_sgl_ptr->address_ptr;
#endif
        CdiSglEntry* payload_sgl_entry_ptr = NULL;
        // Log warning if we get a packet with no payload, as this should never happen.
        if (new_sgl_ptr->size_in_bytes <= initial_offset_local) {
            CDI_LOG_THREAD(kLogWarning, "Got sequence[%d] on payload[%d] with no payload data.",
                           new_sglist_ptr->sgl_head_ptr->packet_sequence_num, common_hdr_ptr->payload_num);
        }
        // Create a new payload SGL entry and then append it to the queue.
        if (!CdiPoolGet(payload_sgl_entry_pool_handle, (void**)&payload_sgl_entry_ptr)) {
            ret = false;
            break;
        }

        // If we have a truncated packet that is shorter than the expected header, then make sure we don't go past the
        // packet end.
        initial_offset_local = CDI_MIN(initial_offset_local, new_sgl_ptr->size_in_bytes);

        // Initialize the new payload SGL entry and then add it to the tail of the payload SGL list.
        payload_sgl_entry_ptr->next_ptr = NULL;
        payload_sgl_entry_ptr->address_ptr = (uint8_t*)new_sgl_ptr->address_ptr + initial_offset_local;
        payload_sgl_entry_ptr->size_in_bytes = new_sgl_ptr->size_in_bytes - initial_offset_local;
        *num_bytes_added_ptr += payload_sgl_entry_ptr->size_in_bytes;

#ifdef DEBUG_INTERNAL_SGL_ENTRIES
        payload_sgl_entry_ptr->packet_sequence_num = common_hdr_ptr->packet_sequence_num;
        payload_sgl_entry_ptr->payload_num = common_hdr_ptr->payload_num;
#endif
        SglAppend(sglist_ptr, payload_sgl_entry_ptr);

        initial_offset_local = 0; // Only the first entry will have an offset.
    }
    return ret;
}

/**
 * @brief Adds an SGL list to the top of an existing reorder list. First SGL entry of SGL list may have offset.
 *
 * @param payload_sgl_entry_pool_handle Handle for free SGL memory.
 * @param cur_reorder_list_ptr Pointer to list which will be appended to.
 * @param new_sglist_ptr Pointer to entry to be added to list.
 * @param sequence_num The sequence number of this SGL list.
 * @param initial_offset First SGL entry will have this offset applied.
 * @param num_bytes_added_ptr Pointer to the number of bytes that were successfully added to list.
 * @return True if adding SGL list is successful.
 */
static bool AddSgListToRxReorderListTop(CdiPoolHandle payload_sgl_entry_pool_handle,
                                        CdiReorderList* cur_reorder_list_ptr,
                                        const CdiSgList* new_sglist_ptr, int sequence_num,
                                        int initial_offset, int* num_bytes_added_ptr)
{
    bool ret = true;
    // Save off the head pointer because it will be replaced with the new entry.
    CdiSglEntry* tmp_sgl_head_ptr = cur_reorder_list_ptr->sglist.sgl_head_ptr;
    CdiSglEntry* tmp_sgl_tail_ptr = cur_reorder_list_ptr->sglist.sgl_tail_ptr;
    cur_reorder_list_ptr->sglist.sgl_head_ptr = NULL;
    cur_reorder_list_ptr->sglist.sgl_tail_ptr = NULL;
    if (AddSgListToReorderList(payload_sgl_entry_pool_handle, &cur_reorder_list_ptr->sglist,
                               new_sglist_ptr, initial_offset, num_bytes_added_ptr)) {
#ifdef DEBUG_RX_REORDER_ALL
        CDI_LOG_THREAD(kLogInfo, "Got sequence[%d] and attaching to top of list [%d-%d].",
                       sequence_num, cur_reorder_list_ptr->top_sequence_num, cur_reorder_list_ptr->bot_sequence_num);
#endif
        cur_reorder_list_ptr->sglist.sgl_tail_ptr->next_ptr = tmp_sgl_head_ptr;
        cur_reorder_list_ptr->sglist.sgl_tail_ptr = tmp_sgl_tail_ptr;
        cur_reorder_list_ptr->top_sequence_num = sequence_num;
    } else {
        ret = false;
    }
    return ret;
}

/**
 * @brief Adds an SGL list to the bottom of an existing reorder list. First SGL entry of SGL list may have offset.
 *
 * @param payload_sgl_entry_pool_handle Handle for free SGL memory.
 * @param cur_reorder_list_ptr Pointer to list which will be appended to.
 * @param new_sglist_ptr Pointer to entry to be added to list.
 * @param sequence_num The sequence number of this SGL list.
 * @param initial_offset First SGL entry will have this offset applied.
 * @param num_bytes_added_ptr Pointer to the number of bytes that were successfully added to list.
 * @return True if adding SGL list is successful.
 */
static bool AddSgListToRxReorderListBottom(CdiPoolHandle payload_sgl_entry_pool_handle,
                                           CdiReorderList* cur_reorder_list_ptr,
                                           const CdiSgList* new_sglist_ptr, int sequence_num,
                                           int initial_offset, int* num_bytes_added_ptr)
{
    bool ret = true;
    if (AddSgListToReorderList(payload_sgl_entry_pool_handle, &cur_reorder_list_ptr->sglist,
                               new_sglist_ptr, initial_offset, num_bytes_added_ptr)) {
#ifdef DEBUG_RX_REORDER_ALL
        CDI_LOG_THREAD(kLogInfo, "Got sequence[%d] and attaching to bottom of list [%d-%d].",
                       sequence_num, cur_reorder_list_ptr->top_sequence_num, cur_reorder_list_ptr->bot_sequence_num);
#endif
        cur_reorder_list_ptr->bot_sequence_num = sequence_num;
    } else {
        ret = false;
    }
    return ret;
}

/**
 * @brief Inserts a reorder list between two reorder lists.
 *
 * @param prev_reorder_list_ptr Pointer to list to left of new list being added.
 * @param next_reorder_list_ptr Pointer to list to right of new list being added.
 * @param new_reorder_list_ptr Pointer to list being added.
 */
static void InsertRxReorderList(CdiReorderList* prev_reorder_list_ptr,
                                CdiReorderList* next_reorder_list_ptr, CdiReorderList* new_reorder_list_ptr)
{
    new_reorder_list_ptr->next_ptr = next_reorder_list_ptr;
    new_reorder_list_ptr->prev_ptr = prev_reorder_list_ptr;
    if (NULL != prev_reorder_list_ptr) {
        prev_reorder_list_ptr->next_ptr = new_reorder_list_ptr;
    }
    if (NULL != next_reorder_list_ptr) {
        next_reorder_list_ptr->prev_ptr = new_reorder_list_ptr;
    }
}

/**
 * @brief Attaches a list that is to the right of the current list and frees the attached list's memory space.
 *
 * @param reorder_entries_pool_handle Handle for free rx reorder list memory.
 * @param cur_reorder_list_ptr Pointer to the list that will have it's next list attached.
 */
static void AttachNextRxReorderList(CdiPoolHandle reorder_entries_pool_handle, CdiReorderList* cur_reorder_list_ptr)
{
    CdiReorderList* tmp_reorder_list_ptr = NULL;
    // Attach the next list to this one.
    cur_reorder_list_ptr->sglist.sgl_tail_ptr->next_ptr = cur_reorder_list_ptr->next_ptr->sglist.sgl_head_ptr;
    cur_reorder_list_ptr->sglist.sgl_tail_ptr = cur_reorder_list_ptr->next_ptr->sglist.sgl_tail_ptr;
#ifdef DEBUG_RX_REORDER_ALL
    CDI_LOG_THREAD(kLogInfo, "Deleting list [%d-%d] by attaching to bottom of list [%d-%d].",
                   cur_reorder_list_ptr->next_ptr->top_sequence_num,
                   cur_reorder_list_ptr->next_ptr->bot_sequence_num,
                   cur_reorder_list_ptr->top_sequence_num, cur_reorder_list_ptr->bot_sequence_num);
#endif
    // Make this one's next value point to what the next list used to point to.
    cur_reorder_list_ptr->bot_sequence_num = cur_reorder_list_ptr->next_ptr->bot_sequence_num;
    tmp_reorder_list_ptr = cur_reorder_list_ptr->next_ptr;
    cur_reorder_list_ptr->next_ptr = tmp_reorder_list_ptr->next_ptr;
    // Make the next prev_ptr point back to this list because the one it used to point to is removed.
    if (NULL != tmp_reorder_list_ptr->next_ptr) {
        tmp_reorder_list_ptr->next_ptr->prev_ptr = cur_reorder_list_ptr;
    }
    // Remove the next reorder list because it has been attached.
    CdiPoolPut(reorder_entries_pool_handle, (void*)tmp_reorder_list_ptr);
}

/**
 * @brief Creates a new reorder list and then inserts it between two reorder lists.
 * The SGL list is added to the created reorder list. First SGL entry of SGL list may have offset.
 *
 * @param reorder_entries_pool_handle Handle for free rx reorder list memory.
 * @param payload_sgl_entry_pool_handle Handle for free SGL memory.
 * @param new_sglist_ptr Pointer to entry to be added to list.
 * @param sequence_num The sequence number of this SGL list.
 * @param initial_offset First SGL entry will have this offset applied.
 * @param num_bytes_added_ptr Pointer to the number of bytes that were successfully added to list.
 * @param prev_reorder_list_ptr Pointer to list to left of new list being added.
 * @param next_reorder_list_ptr Pointer to list to right of new list being added.
 * @param new_reorder_list_ptr Pointer to pointer of newly created list.
 * @return True if creating reorder list and adding SGL list is successful.
 */
static bool CreateAndInsertRxReorderList(CdiPoolHandle reorder_entries_pool_handle,
                                         CdiPoolHandle payload_sgl_entry_pool_handle,
                                         const CdiSgList* new_sglist_ptr, int sequence_num,
                                         int initial_offset, int* num_bytes_added_ptr,
                                         CdiReorderList* prev_reorder_list_ptr,
                                         CdiReorderList* next_reorder_list_ptr,
                                         CdiReorderList** new_reorder_list_ptr)
{
    bool ret = true;
    // Fetch a new reorder list.
    // This memory is not initialized for performance reasons. All pointers must be explicitly initialized.
    if (!CdiPoolGet(reorder_entries_pool_handle, (void**)new_reorder_list_ptr)) {
        ret = false;
    } else {
        (*new_reorder_list_ptr)->next_ptr = NULL;
        (*new_reorder_list_ptr)->prev_ptr = NULL;
        (*new_reorder_list_ptr)->sglist.total_data_size = 0;
        (*new_reorder_list_ptr)->sglist.sgl_head_ptr = NULL;
        (*new_reorder_list_ptr)->sglist.sgl_tail_ptr = NULL;
        (*new_reorder_list_ptr)->top_sequence_num = sequence_num;
        (*new_reorder_list_ptr)->bot_sequence_num = sequence_num;
        if (AddSgListToReorderList(payload_sgl_entry_pool_handle, &(*new_reorder_list_ptr)->sglist,
                                   new_sglist_ptr, initial_offset, num_bytes_added_ptr)) {
#ifdef DEBUG_RX_REORDER_MIN
            CDI_LOG_THREAD(kLogInfo, "Creating new list [%d].",sequence_num);
#endif
            // If rxreorder creation was successful, insert it in the rxreorder list.
            InsertRxReorderList(prev_reorder_list_ptr, next_reorder_list_ptr, *new_reorder_list_ptr);
        } else {
            ret = false;
        }
    }
    return ret;
}

/**
 * @brief Walks the rxreorder list to the right, looking for a spot to place the new SGL list.
 *
 * @param reorder_entries_pool_handle Handle for free rx reorder list memory.
 * @param payload_sgl_entry_pool_handle Handle for free SGL memory.
 * @param num_bytes_added_ptr Pointer to the number of bytes that were successfully added to list.
 * @param cur_reorder_list_ptr Pointer to list which will have new_sgl_list added to it.
 * @param new_sglist_ptr Pointer to entry to be added to list.
 * @param sequence_num The sequence number of this SGL list.
 * @param initial_offset First SGL entry will have this offset applied.
 * @return True if adding SGL list is successful.
 */
static bool ProcessList(CdiPoolHandle reorder_entries_pool_handle, CdiPoolHandle payload_sgl_entry_pool_handle,
                        int* num_bytes_added_ptr, CdiReorderList** cur_reorder_list_ptr,
                        const CdiSgList* new_sglist_ptr, int sequence_num, int initial_offset)
{

    bool ret = true;
    CdiReorderList* new_reorder_list_ptr = NULL;
    CdiReorderList* tmp_reorder_list_ptr = *cur_reorder_list_ptr;
    CdiReorderList* tmp_reorder_list_ptr_prev = NULL;

    // Search to find where to attach list.
    while (tmp_reorder_list_ptr) {
#ifdef DEBUG_RX_REORDER_ERROR
        // This should never happen.
        if (sequence_num >= tmp_reorder_list_ptr->top_sequence_num &&
            sequence_num <= tmp_reorder_list_ptr->bot_sequence_num) {
            CDI_LOG_THREAD(kLogWarning, "Sequence number[%d] has already been received! Skipping.", sequence_num);
            ret = false;
            break;
        }
#endif
        if (sequence_num > tmp_reorder_list_ptr->bot_sequence_num) {
            tmp_reorder_list_ptr_prev = tmp_reorder_list_ptr;
            tmp_reorder_list_ptr = tmp_reorder_list_ptr->next_ptr;
        } else {
            break;
        }
    }
    // if the very first one we got is < than the bot_sequence_num, that means it either belongs
    // at the top of the list or to the left
    if (NULL == tmp_reorder_list_ptr_prev) {
        if (sequence_num == tmp_reorder_list_ptr->top_sequence_num-1) {
            if (AddSgListToRxReorderListTop(payload_sgl_entry_pool_handle, tmp_reorder_list_ptr, new_sglist_ptr,
                                            sequence_num, initial_offset, num_bytes_added_ptr)) {
            } else {
                ret = false;
            }
        } else {
            // List didn't belong on top of existing list, insert to the left and set
            // the payload_state pointer to this new list
            if (CreateAndInsertRxReorderList(reorder_entries_pool_handle, payload_sgl_entry_pool_handle,
                                             new_sglist_ptr, sequence_num, initial_offset, num_bytes_added_ptr,
                                             NULL, tmp_reorder_list_ptr, &new_reorder_list_ptr)) {
                *cur_reorder_list_ptr = new_reorder_list_ptr;
            } else {
                ret = false;
            }
        }
    } else {
        // Does this belong on the bottom of existing list?
        if (sequence_num == tmp_reorder_list_ptr_prev->bot_sequence_num+1) {
            if (AddSgListToRxReorderListBottom(payload_sgl_entry_pool_handle, tmp_reorder_list_ptr_prev,
                                               new_sglist_ptr, sequence_num, initial_offset,
                                               num_bytes_added_ptr)) {
                // Check to see if next list needs attaching because something has been added to the end of this one.
                if (tmp_reorder_list_ptr) {
                    if (tmp_reorder_list_ptr->top_sequence_num == sequence_num+1) {
                        // Attach top of next list to bottom of this list.
                        AttachNextRxReorderList(reorder_entries_pool_handle, tmp_reorder_list_ptr_prev);
                    }
                }
            } else {
                ret = false;
            }
        } else {
            // List didn't belong on bottom of existing list, check to see if it should be attached to
            // top of next list.
            if (NULL != tmp_reorder_list_ptr) {
                if (sequence_num == tmp_reorder_list_ptr->top_sequence_num-1) {
                    if (AddSgListToRxReorderListTop(payload_sgl_entry_pool_handle, tmp_reorder_list_ptr,
                                                    new_sglist_ptr, sequence_num, initial_offset,
                                                    num_bytes_added_ptr)) {
                    } else {
                        ret = false;
                    }
                } else {
                    // List didn't belong on top of existing list, insert between the two lists.
                    if (CreateAndInsertRxReorderList(reorder_entries_pool_handle, payload_sgl_entry_pool_handle,
                                                     new_sglist_ptr, sequence_num, initial_offset,
                                                     num_bytes_added_ptr, tmp_reorder_list_ptr_prev,
                                                     tmp_reorder_list_ptr, &new_reorder_list_ptr)) {
                    } else {
                        ret = false;
                    }
                }
            } else {
                // No next list, so create a new list to the right.
                if (CreateAndInsertRxReorderList(reorder_entries_pool_handle, payload_sgl_entry_pool_handle,
                                                 new_sglist_ptr, sequence_num, initial_offset, num_bytes_added_ptr,
                                                 tmp_reorder_list_ptr_prev, NULL, &new_reorder_list_ptr)) {
                } else {
                    ret = false;
                }
            }
        }
    }
    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void CdiRxReorderFreeLists(CdiReorderList* reorder_list_ptr, CdiPoolHandle payload_sgl_entry_pool_handle,
                           CdiPoolHandle reorder_entries_pool_handle)
{
    while (reorder_list_ptr) {
        // First remove the SGL that is in this reorder list.
        if (!FreeSglEntries(payload_sgl_entry_pool_handle, reorder_list_ptr->sglist.sgl_head_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to return SGL entry to free pool.");
        }

        CdiReorderList* reorder_list_next_ptr = reorder_list_ptr->next_ptr;
        // Now remove this reorder list.
        CdiPoolPut(reorder_entries_pool_handle, (void*)reorder_list_ptr);
        reorder_list_ptr = reorder_list_next_ptr;
        if (reorder_list_ptr) {
            reorder_list_ptr->prev_ptr = NULL;
        }
    }
}

bool CdiRxPayloadReorderStateInit(CdiPoolHandle payload_sgl_entry_pool_handle,
                                  CdiPoolHandle reorder_entries_pool_handle, RxPayloadState* payload_state_ptr,
                                  const CdiSgList* new_sglist_ptr, int initial_offset, int sequence_num)
{
    bool ret = true;
    CdiReorderList* new_reorder_list_ptr = NULL;
    int num_bytes_added = 0;

    // Because this is initialization, need only create a new rxreorder list and finish.
    if (CreateAndInsertRxReorderList(reorder_entries_pool_handle, payload_sgl_entry_pool_handle,
                                     new_sglist_ptr, sequence_num, initial_offset, &num_bytes_added,
                                     NULL, NULL, &new_reorder_list_ptr)) {
        payload_state_ptr->reorder_list_ptr = new_reorder_list_ptr;
        payload_state_ptr->data_bytes_received = num_bytes_added;
    } else {
        ret = false;
    }

    if (!ret) {
        CdiRxReorderFreeLists(payload_state_ptr->reorder_list_ptr, payload_sgl_entry_pool_handle,
                              reorder_entries_pool_handle);
    }

    return ret;
}

bool CdiRxReorder(CdiPoolHandle payload_sgl_entry_pool_handle,
                  CdiPoolHandle reorder_entries_pool_handle,
                  RxPayloadState* payload_state_ptr,
                  const CdiSgList* new_sglist_ptr, int initial_offset, int sequence_num)
{
    bool ret = true;
    int num_bytes_added = 0;

    // Search for a place to put this sequence number.
    ret = ProcessList(reorder_entries_pool_handle, payload_sgl_entry_pool_handle,
                      &num_bytes_added, &payload_state_ptr->reorder_list_ptr, new_sglist_ptr,
                      sequence_num, initial_offset);
    if (ret) {
        payload_state_ptr->data_bytes_received += num_bytes_added;
    } else {
        CdiRxReorderFreeLists(payload_state_ptr->reorder_list_ptr, payload_sgl_entry_pool_handle,
                              reorder_entries_pool_handle);
    }

    return ret;
}
