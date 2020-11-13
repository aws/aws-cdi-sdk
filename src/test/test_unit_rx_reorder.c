// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used with the SDK that is not part of the API.
 *
 * @brief
 * Test the RxReorder function by sending in out of sequence sgls and get an in-order sgl.
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "cdi_logger_api.h"
#include "cdi_raw_api.h"
#include "cdi_test.h"
#include "internal_rx.h"
#include "rx_reorder.h"
#include "test_control.h"
#include "test_receiver.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/// @brief  The maximum number of Rx reorder SGL lists we want to send.
#define TEST_UNIT_RX_REORDER_NUM_SGLS 32

/// @brief A modulus used for generating a random list length.
#define TEST_UNIT_RX_REORDER_RAND_LEN 3

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// Main routine to test rx_reorder.
bool TestUnitRxReorder(void)
{
    // Array of out of sequence values (can be made truly random later).
    uint16_t random_sequence_num_array[] = { 2, 0, 1, 6, 7, 4, 3, 5, 8, 10, 12, 11, 9, 15, 14, 13};
    int num_rand_seq_num = sizeof(random_sequence_num_array)/sizeof(random_sequence_num_array[0]);
    srand(time(0));
    int rand_len = 0;
    int tot_sgls = 0;
    CdiReturnStatus ret = kCdiStatusOk;
    bool rx_ret = true;

    CdiConnectionState con_state = { 0 };
    CdiConnectionState* con_state_ptr = &con_state;
    con_state.magic = kMagicCon;

    CdiEndpointState endpoint_state = { 0 };
    CdiEndpointState* endpoint_state_ptr = &endpoint_state;
    RxPayloadState rx_payload_state;
    endpoint_state_ptr->rx_state.payload_state_array_ptr[0] = &rx_payload_state;

    CdiSglEntry* sgl_entry_ptr = NULL;
    CdiSgList* new_sgl_list_ptr = NULL;

    CdiSgList sgl_list_pool[TEST_UNIT_RX_REORDER_NUM_SGLS];
    CdiSgList* sgl_list_ptr = NULL;
    memset(&sgl_list_pool[0], 0, sizeof(sgl_list_pool[TEST_UNIT_RX_REORDER_NUM_SGLS]));

    CdiSglEntry sgl_entry_pool[TEST_UNIT_RX_REORDER_NUM_SGLS*TEST_UNIT_RX_REORDER_RAND_LEN];
    memset(&sgl_entry_pool[0], 0, sizeof(sgl_entry_pool[TEST_UNIT_RX_REORDER_NUM_SGLS]));

    CdiCDIPacketNum0Header header_zero;
    memset(&header_zero, 0, sizeof(header_zero));

    CdiCDIPacketCommonHeader common_hdr_pool[TEST_UNIT_RX_REORDER_NUM_SGLS];
    memset(&common_hdr_pool[0], 0, sizeof(common_hdr_pool[TEST_UNIT_RX_REORDER_NUM_SGLS]));

    // Create a pool of locations.
    if (!CdiPoolCreate("Rx CdiSglEntry Payload Pool",
                       TEST_UNIT_RX_REORDER_NUM_SGLS*TEST_UNIT_RX_REORDER_RAND_LEN,    // item_count
                       TEST_UNIT_RX_REORDER_NUM_SGLS*TEST_UNIT_RX_REORDER_RAND_LEN,    // grow_count
                       MAX_POOL_GROW_COUNT,
                       sizeof(CdiSglEntry), true, // true= Make thread-safe,
                       &con_state_ptr->rx_state.payload_sgl_entry_pool_handle)) {
        ret = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == ret) {
        if (!CdiPoolCreate("Rx CdiReorderList Out of Order Pool", MAX_RX_OUT_OF_ORDER, MAX_RX_OUT_OF_ORDER_GROW,
                           MAX_POOL_GROW_COUNT, sizeof(CdiReorderList), true, // true= Make thread-safe
                           &con_state_ptr->rx_state.reorder_entries_pool_handle)) {
            ret = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == ret) {
        // Initialize the sequence numbers.
        int j=0;
        int k=0;
        int this_is_sequence_zero = 0;
        // Setting the payload state value of packet_sequence_num marks it as invalid when being checked
        // when a payload arrives.
        for (int i=0; i!=TEST_UNIT_RX_REORDER_NUM_SGLS; i++) {
            if (random_sequence_num_array[k] == 0) {
                common_hdr_pool[i].packet_sequence_num = random_sequence_num_array[k] + j;
                if (j == 0) {
                    this_is_sequence_zero = k; // Need to remember where the actual head of list is, which will occur within num_rand_seq_num.
                }
                k++;
            } else {
                common_hdr_pool[i].packet_sequence_num = random_sequence_num_array[k] + j;
                if (k == num_rand_seq_num-1) {
                    j+=num_rand_seq_num;
                    k=0;
                } else {
                    k++;
                }
            }
        }

        // Initialize the list that we will send to be reordered.
        // Head will point to the top entry in the pool.
        for (int i=0; i!=TEST_UNIT_RX_REORDER_NUM_SGLS; i++) {
            sgl_list_ptr = &sgl_list_pool[i];
            sgl_entry_ptr = &sgl_entry_pool[tot_sgls++];
            sgl_list_ptr->sgl_head_ptr = sgl_entry_ptr;

            if (this_is_sequence_zero !=i) {
                rand_len = (rand() % TEST_UNIT_RX_REORDER_RAND_LEN) + 1; // Generate a random length of list.
                rand_len = 1; // Force length of list to 1.
                for (int j=0; j!= rand_len; j++) {
                    // Only the first entry in the mini-list will have a common header.
                    if (j == 0) {
                        sgl_entry_ptr->address_ptr = &common_hdr_pool[i];
#ifdef DEBUG_INTERNAL_SGL_ENTRIES
                        sgl_entry_ptr->packet_sequence_num = common_hdr_pool[i].packet_sequence_num;
                        sgl_entry_ptr->payload_num = 0;
#endif
                        sgl_entry_ptr->size_in_bytes = rand_len*sizeof(CdiCDIPacketCommonHeader)+1;
                        header_zero.total_payload_size += sgl_entry_ptr->size_in_bytes; // Total size of payload in bytes.
                    } else {
                        // Add the payload SGL entry to the bottom of the list.
                        CdiSglEntry* new_entry_ptr = &sgl_entry_pool[tot_sgls++];
                        sgl_entry_ptr->next_ptr = new_entry_ptr;
                        sgl_entry_ptr = new_entry_ptr;
                    }
                }
            } else {
                // This is sequence number 0.
                memset(&endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->work_request_state.app_payload_cb_data.payload_sgl,
                       0, sizeof(CdiSgList));

                sgl_entry_ptr->address_ptr = &header_zero;
                sgl_entry_ptr->size_in_bytes = sizeof(CdiCDIPacketNum0Header)+1;
                header_zero.total_payload_size += sgl_entry_ptr->size_in_bytes;
            }
        }

        new_sgl_list_ptr = &sgl_list_pool[0];
        CdiCDIPacketCommonHeader* common_hdr_ptr = new_sgl_list_ptr->sgl_head_ptr->address_ptr;
        CdiCDIPacketNum0Header* hdr0_ptr = (CdiCDIPacketNum0Header*)common_hdr_ptr;
        int cdi_header_size = sizeof(CdiCDIPacketCommonHeader);
        if (0 == common_hdr_ptr->packet_sequence_num) {
            cdi_header_size = sizeof(CdiCDIPacketNum0Header) + hdr0_ptr->extra_data_size;
        }

        rx_ret = CdiRxPayloadReorderStateInit(con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                              con_state_ptr->rx_state.reorder_entries_pool_handle,
                                              endpoint_state_ptr->rx_state.payload_state_array_ptr[0], new_sgl_list_ptr,
                                              cdi_header_size, common_hdr_ptr->packet_sequence_num);

        for (int i=1; rx_ret && i!=TEST_UNIT_RX_REORDER_NUM_SGLS; i++) {
            new_sgl_list_ptr = &sgl_list_pool[i];
            common_hdr_ptr = new_sgl_list_ptr->sgl_head_ptr->address_ptr;
            rx_ret = CdiRxReorder(con_state_ptr->rx_state.payload_sgl_entry_pool_handle,
                                  con_state_ptr->rx_state.reorder_entries_pool_handle,
                                  endpoint_state_ptr->rx_state.payload_state_array_ptr[0], new_sgl_list_ptr,
                                  cdi_header_size, common_hdr_ptr->packet_sequence_num);
        }
        if (NULL != endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr->next_ptr &&
            NULL != endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr->prev_ptr) {
            CDI_LOG_THREAD(kLogError, "Test finished and there are dangling lists.");
            CdiReorderList* reorder_list_ptr = endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr;
            while (reorder_list_ptr) {
                CDI_LOG_THREAD(kLogDebug, "Dangling list [%d-%d].", reorder_list_ptr->top_sequence_num,reorder_list_ptr->bot_sequence_num);
                reorder_list_ptr = reorder_list_ptr->next_ptr;
            }
            ret = kCdiStatusFatal;
        }
        // At this point the list can be checked by starting with endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr->sglist.sgl_head_ptr.
#ifdef DEBUG_INTERNAL_SGL_ENTRIES
        int packet_sequence_num = 0;
        CdiSglEntry* reorder_entry_ptr = endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr->sglist.sgl_head_ptr;
        while (reorder_entry_ptr) {
            if (packet_sequence_num != reorder_entry_ptr->packet_sequence_num) {
                CDI_LOG_THREAD(kLogError, "Yah! Expected packet sequence [%d] and got [%d].",packet_sequence_num,reorder_entry_ptr->packet_sequence_num);
                ret = kCdiStatusFatal;
            } else {
                CDI_LOG_THREAD(kLogDebug, "Match. Expected packet sequence [%d] and got [%d].",packet_sequence_num,reorder_entry_ptr->packet_sequence_num);
            }
            reorder_entry_ptr = reorder_entry_ptr->next_ptr;
            packet_sequence_num++;
        }
    }
#endif
    // get rid of everything
    CdiReorderList* reorder_list_ptr = endpoint_state_ptr->rx_state.payload_state_array_ptr[0]->reorder_list_ptr;
    while (reorder_list_ptr) {
        CdiSglEntry* entry_ptr = reorder_list_ptr->sglist.sgl_head_ptr;
        while (entry_ptr) {
            CdiSglEntry *next_entry_ptr = entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
            CdiPoolPut(con_state_ptr->rx_state.payload_sgl_entry_pool_handle, entry_ptr);
            entry_ptr = next_entry_ptr;
        }
        CdiReorderList *next_ptr = reorder_list_ptr->next_ptr; // Save next entry, since Put() will free its memory.
        CdiPoolPut(con_state_ptr->rx_state.reorder_entries_pool_handle, reorder_list_ptr);
        reorder_list_ptr = next_ptr;
    }
    if (con_state_ptr->rx_state.payload_sgl_entry_pool_handle) {
        CdiPoolDestroy(con_state_ptr->rx_state.payload_sgl_entry_pool_handle);
    }
    if (con_state_ptr->rx_state.reorder_entries_pool_handle) {
        CdiPoolDestroy(con_state_ptr->rx_state.reorder_entries_pool_handle);
    }
    return ret == kCdiStatusOk;
}
