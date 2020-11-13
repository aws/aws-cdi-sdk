// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file defines all the methods required for a singly linked list. A tail pointer is provided in order to enable
 * use as a FIFO. The implementation is not thread safe. Its simplicity keeps it efficient for O(1) complexity.
 *
 * empty list:
 *     head_ptr -> NULL
 *     tail_ptr -> NULL
 *
 * single item in list:
 *                 +----------+
 *     head_ptr -> | next_ptr | -> NULL
 *                 +----------+
 *                       ^
 *     tail_ptr ---------+
 *
 * larger list:
 *                 +----------+    +----------+           +----------+
 *     head_ptr -> | next_ptr | -> | next_ptr | -> ... -> | next_ptr | -> NULL
 *                 +----------+    +----------+           +----------+
 *                                                              ^
 *     tail_ptr ------------------------------------------------+
 */

#ifndef CDI_SINGLY_LINKEDLIST_API_H__
#define CDI_SINGLY_LINKEDLIST_API_H__

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Forward declaration to create pointer to singly linked list entry when used.
typedef struct CdiSinglyLinkedListEntry CdiSinglyLinkedListEntry;

/**
 * @brief This structure represents a single list entry.
 */
struct CdiSinglyLinkedListEntry {
    struct CdiSinglyLinkedListEntry* next_ptr; ///< Pointer to next item in list, NULL if this is the tail entry.
};

/**
 * @brief This structure represents a list.
 */
typedef struct {
    CdiSinglyLinkedListEntry* head_ptr;  ///< Head entry of list item. NULL valid if the list is empty.
    CdiSinglyLinkedListEntry* tail_ptr;  ///< Tail entry of list item. NULL valid if the list is empty.
    int num_entries;                     ///< Number of entries currently in this list
} CdiSinglyLinkedList;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize a list. Doesn't need to be inline, since it is only used once for each instance of the list.
 *
 * @param list_ptr Pointer to instance of the list.
 */
static inline void CdiSinglyLinkedListInit(CdiSinglyLinkedList* list_ptr)
{
    list_ptr->head_ptr    = NULL;
    list_ptr->tail_ptr    = NULL;
    list_ptr->num_entries = 0;
}

/**
 * Add a new entry to the head of the list.
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 */
static inline void CdiSinglyLinkedListPushHead(CdiSinglyLinkedList* list_ptr,
                                               CdiSinglyLinkedListEntry* new_entry_ptr)
{
    new_entry_ptr->next_ptr = list_ptr->head_ptr;
    list_ptr->head_ptr = new_entry_ptr;
    if (list_ptr->tail_ptr == NULL) {
        list_ptr->tail_ptr = new_entry_ptr;
    }
    list_ptr->num_entries++;
}

/**
 * Add a new entry to the tail of the list.
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 */
static inline void CdiSinglyLinkedListPushTail(CdiSinglyLinkedList* list_ptr,
                                               CdiSinglyLinkedListEntry* new_entry_ptr)
{
    new_entry_ptr->next_ptr = NULL;
    if (list_ptr->tail_ptr != NULL) {
        list_ptr->tail_ptr->next_ptr = new_entry_ptr;
    }
    list_ptr->tail_ptr = new_entry_ptr;
    if (list_ptr->head_ptr == NULL) {
        list_ptr->head_ptr = new_entry_ptr;
    }
    list_ptr->num_entries++;
}

/**
 * Pop an item off the head of the list, removing it from the list.
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Pointer to the popped head list entry or NULL if the list is empty.
 */
static inline CdiSinglyLinkedListEntry* CdiSinglyLinkedListPopHead(CdiSinglyLinkedList* list_ptr)
{
    CdiSinglyLinkedListEntry* p = list_ptr->head_ptr;
    if (p != NULL) {
        list_ptr->head_ptr = p->next_ptr;
        p->next_ptr = NULL;

        // Don't allow value to go negative.
        assert(list_ptr->num_entries >= 0);
        if (list_ptr->num_entries) {
            list_ptr->num_entries--;
        }
    }
    if (list_ptr->head_ptr == NULL) {
        list_ptr->tail_ptr = NULL;
    }
    return p;
}

/**
 * Check if the list is empty.
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return True if the list is empty, otherwise false.
 */
static inline bool CdiSinglyLinkedListIsEmpty(const CdiSinglyLinkedList* list_ptr)
{
    return list_ptr->head_ptr == NULL;
}

/**
 * Report the number of entries currently in the list.
 *
 * @param list_ptr Address of the list whose size is of interest.
 *
 * @return int The number of entries in the list; this will always be greater than or equal to zero (empty).
 */
static inline int CdiSinglyLinkedListSize(const CdiSinglyLinkedList* list_ptr)
{
    return list_ptr->num_entries;
}

/**
 * Provides a pointer to the head entry of a given list.
 *
 * @param list_ptr Pointer to the list to get the head entry ptr.
 *
 * @return Pointer to the head entry of the list_ptr list.
 */
static inline CdiSinglyLinkedListEntry* CdiSinglyLinkedListGetHead(const CdiSinglyLinkedList* list_ptr)
{
    return list_ptr->head_ptr;
}

/**
 * Provides a pointer to the next entry of a list entry.
 *
 * @param entry_ptr Pointer to an entry on a list.
 *
 * @return Pointer to the next entry on the list.
 */
static inline CdiSinglyLinkedListEntry* CdiSinglyLinkedListNextEntry(const CdiSinglyLinkedListEntry* entry_ptr)
{
    return entry_ptr->next_ptr;
}

#endif // CDI_SINGLY_LINKED_LIST_API_H__
