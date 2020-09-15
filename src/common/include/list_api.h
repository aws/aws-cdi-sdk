// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in list.c. Many of the functions here are static
 * in-line for performance reasons and they don't contain much logic.
 */

#ifndef CDI_LIST_API_H__
#define CDI_LIST_API_H__

#include "utilities_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>

#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Forward declaration to create pointer to list entry when used.
typedef struct CdiListEntry CdiListEntry;
/**
 * @brief This structure represents a single list entry.
 */
struct CdiListEntry {
    CdiListEntry* next_ptr; ///< Pointer to next item in list. If no items in list, will point to itself.
    CdiListEntry* prev_ptr; ///< Pointer to previous item in list. If no items in list, will point to itself.
};

/**
 * @brief This structure represents a list.
 */
typedef struct {
    CdiListEntry head_entry; ///< Head entry of list item. Only valid if count >= 1.
    unsigned int count;       ///< Number of entries in the list (used for convenience).
} CdiList;

/**
 * @brief This structure represents a list iterator.
 */
typedef struct {
    CdiListEntry* head_ptr; ///< Pointer to head entry of list.
    CdiListEntry* next_ptr; ///< Pointer to next item in list. If no items in list, will point to head_entry_ptr.
} CdiListIterator;

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
 * NOTE: All the APIs in this file are not thread-safe. However, read list entry APIs that use next_entry_ptr such as
 * CdiListIteratorGetNext() can be used without thread-safe resource locks.
 *
 * @param list_ptr Pointer to instance of the list.
 */
void CdiListInit(CdiList* list_ptr);

/**
 * Get the head pointer of the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Pointer to the head list entry.
 */
static inline CdiListEntry* CdiListGetHead(CdiList* list_ptr)
{
    return &list_ptr->head_entry;
}

/**
 * Check if the list is empty. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return True if the list is empty, otherwise false.
 */
static inline bool CdiListIsEmpty(const CdiList* list_ptr)
{
    return (0 >= list_ptr->count);
}

/**
 * Add a new entry after the item specified in prev_ptr. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 * @param prev_entry_ptr Pointer to entry to add the new item after.
 */
static inline void CdiListAddAfter(CdiList* list_ptr, CdiListEntry* new_entry_ptr, CdiListEntry* prev_entry_ptr)
{
    CdiListEntry *next_entry_ptr = prev_entry_ptr->next_ptr;
    // Update the new entry first, then insert it into the list. This allows multi-threaded access to read the list.
    new_entry_ptr->next_ptr = next_entry_ptr;
    new_entry_ptr->prev_ptr = prev_entry_ptr;

    next_entry_ptr->prev_ptr = new_entry_ptr;
    prev_entry_ptr->next_ptr = new_entry_ptr;
    list_ptr->count++;
}

/**
 * Add a new entry before the item specified in next_ptr. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 * @param next_entry_ptr Pointer to entry to add the new item
 *                      before.
 */
static inline void CdiListAddBefore(CdiList* list_ptr, CdiListEntry* new_entry_ptr, CdiListEntry* next_entry_ptr)
{
    CdiListEntry *prev_entry_ptr = next_entry_ptr->prev_ptr;
    // Update the new entry first, then insert it into the list. This allows multi-threaded access to read the list.
    new_entry_ptr->next_ptr = next_entry_ptr;
    new_entry_ptr->prev_ptr = prev_entry_ptr;

    next_entry_ptr->prev_ptr = new_entry_ptr;
    prev_entry_ptr->next_ptr = new_entry_ptr;
    list_ptr->count++;
}

/**
 * Add a new entry to the head of the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 */
static inline void CdiListAddHead(CdiList* list_ptr, CdiListEntry* new_entry_ptr)
{
    CdiListAddBefore(list_ptr, new_entry_ptr, list_ptr->head_entry.next_ptr);
}

/**
 * Add a new entry to the tail of the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 * @param new_entry_ptr Pointer to new entry to add to the list.
 */
static inline void CdiListAddTail(CdiList* list_ptr, CdiListEntry* new_entry_ptr)
{
    CdiListAddAfter(list_ptr, new_entry_ptr, list_ptr->head_entry.prev_ptr);
}

/**
 * Return the next head entry of the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Pointer to the head list next entry or NULL if the list is empty.
 */
static inline CdiListEntry* CdiListPeek(const CdiList* list_ptr)
{
    if (CdiListIsEmpty(list_ptr)) {
        return NULL;
    }

    return list_ptr->head_entry.next_ptr;
}

/**
 * Return the tail entry of the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Pointer to the tail list entry or NULL if the list is empty.
 */
static inline CdiListEntry* CdiListPeekTail(const CdiList* list_ptr)
{
    if (CdiListIsEmpty(list_ptr)) {
        return NULL;
    }

    return list_ptr->head_entry.prev_ptr;
}

/**
 * Remove an item from the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 * @param entry_ptr Pointer to list item to remove.
 */
static inline void CdiListRemove(CdiList* list_ptr, CdiListEntry* entry_ptr)
{
    // CdiListEntries should always point to other entries or point back to themselves.  If the next_ptr is NULL then
    // the CdiListEntry was never added to a list and so should not be removed from the list.
    if (NULL != entry_ptr->next_ptr) {
        entry_ptr->next_ptr->prev_ptr = entry_ptr->prev_ptr;
        entry_ptr->prev_ptr->next_ptr = entry_ptr->next_ptr;
        entry_ptr->next_ptr = entry_ptr;
        entry_ptr->prev_ptr = entry_ptr;

        assert(list_ptr->count);
        list_ptr->count--;
    }
}

/**
 * Pop an item off the head of the list, removing it from the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Pointer to the popped head list entry or NULL if the list is empty.
 */
static inline CdiListEntry* CdiListPop(CdiList* list_ptr)
{
    CdiListEntry* first_ptr;

    if (CdiListIsEmpty(list_ptr)) {
        return NULL;
    }

    first_ptr = list_ptr->head_entry.next_ptr;
    CdiListRemove(list_ptr, first_ptr);

    return first_ptr;
}

/**
 * Get the number of items in the list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to instance of the list.
 *
 * @return Number of items in the list.
 */
static inline int CdiListCount(const CdiList* list_ptr)
{
    return list_ptr->count;
}

/**
 * Initialize an iterator list. Concerning thread-safety, @see CdiListInit().
 *
 * @param list_ptr Pointer to list being initialized.
 * @param ret_iterator_ptr Return pointer of list that was initialized.
 *
 */
static inline void CdiListIteratorInit(CdiList* list_ptr, CdiListIterator* ret_iterator_ptr)
{
    ret_iterator_ptr->head_ptr = CdiListGetHead(list_ptr);
    ret_iterator_ptr->next_ptr = CdiListPeek(list_ptr);
}

/**
 * Get the next entry in an iterator list. Concerning thread-safety, @see CdiListInit().
 *
 * @param iterator_ptr Pointer to list entry to get the next element.
 *
 * @return next list entry
 */
static inline CdiListEntry* CdiListIteratorGetNext(CdiListIterator* iterator_ptr)
{
    CdiListEntry* ret_entry_ptr = iterator_ptr->next_ptr;

    // Don't walk an empty list.
    if (ret_entry_ptr) {
        // If at head of the list, then no more entries, so use NULL.
        if (ret_entry_ptr->next_ptr == iterator_ptr->head_ptr) {
            iterator_ptr->next_ptr = NULL;
        } else {
            iterator_ptr->next_ptr = ret_entry_ptr->next_ptr;
        }
    }

    return ret_entry_ptr;
}

#endif // CDI_LIST_API_H__
