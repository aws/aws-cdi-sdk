// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains a unit test for the CdiList functionality.
 */

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "list_api.h"
#include "utilities_api.h"

#include <stdbool.h>

static const bool verbose = false;  ///< Set to true to see passing test results.

/**
 * This macro performs a test. Call it with a conditional expression that must be true in order for the unit test to
 * pass.
 */
#define CHECK(condition) \
    do { \
        if (condition) { \
            if (verbose) CDI_LOG_THREAD(kLogInfo, "%s OK", #condition); \
        } else { \
            CDI_LOG_THREAD(kLogError, "%s failed", #condition); \
            return kCdiStatusFatal; \
        } \
    } while (false);

CdiReturnStatus TestUnitList(void)
{
    // Make and initialize a list.
    CdiList list;
    CdiListInit(&list);

    // Define the structure of items to put into the list.
    typedef struct {
        CdiListEntry list_entry;
        int n;
    } Item;

    // Make four unique items to put into the list.
    Item item1 = {
        .n = 1
    };
    Item item2 = {
        .n = 2
    };
    Item item3 = {
        .n = 3
    };
    Item item4 = {
        .n = 4
    };

    // Make sure the list says it's empty prior to putting anything into it.
    CHECK(CdiListIsEmpty(&list));

    // Add the first item to the head and ensure that the list says it's no longer empty.
    CdiListAddHead(&list, &item1.list_entry);
    CHECK(!CdiListIsEmpty(&list));

    // Add the other items using add after, add before, and add to tail.
    CdiListAddAfter(&list, &item3.list_entry, &item1.list_entry);
    CdiListAddBefore(&list, &item2.list_entry, &item3.list_entry);
    CdiListAddTail(&list, &item4.list_entry);

    // Make sure the list reports the correct size.
    CHECK(CdiListCount(&list) == 4);

    // Check that peeking at the head sees the right item.
    CdiListEntry* list_entry_ptr = NULL;
    list_entry_ptr = CdiListPeek(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 1);

    // Check that peeking at the tail sees the right item.
    list_entry_ptr = CdiListPeekTail(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 4);

    // Check that iterating through the list finds all of the items in the correct order.
    CdiListIterator list_iterator;
    CdiListIteratorInit(&list, &list_iterator);
    int i = 0;
    while (NULL != (list_entry_ptr = CdiListIteratorGetNext(&list_iterator))) {
        CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == i + 1);
        i++;
    }
    CHECK(i == 4);

    // Make sure that popping from the head returns the right item.
    list_entry_ptr = CdiListPop(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 1);

    // Check that the list says it has the right number of items now.
    CHECK(CdiListCount(&list) == 3);

    // Check that peeking at the head finds the correct item now.
    list_entry_ptr = CdiListPeek(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 2);

    // Check that popping another item from the list returns the right one.
    list_entry_ptr = CdiListPop(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 2);

    // Check that popping another item from the list returns the right one.
    list_entry_ptr = CdiListPop(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 3);

    // Check that the list doesn't yet report that it's empty.
    CHECK(!CdiListIsEmpty(&list));

    // Pop the last item from the list and make sure the right one was returned.
    list_entry_ptr = CdiListPop(&list);
    CHECK((CONTAINER_OF(list_entry_ptr, Item, list_entry))->n == 4);

    // See that the list now reports that it is empty again.
    CHECK(CdiListIsEmpty(&list));

    return kCdiStatusOk;
}
