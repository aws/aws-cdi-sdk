// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains a unit test for the CdiCoreGather() function.
 */

#include <stddef.h>
#include <string.h>

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// The maximum number of SGL entries in a single SGL used in this unit test.
#define MAX_UNIT_TEST_SGL_ENTRIES 5
/// The size of the buffers used in this unit test. It determines the maximum size of an SGL to be tested.
#define UNIT_TEST_BUFFER_SIZE 1000

/**
 * This is a super simple SGL entry like structure used in the definitions of the test cases.
 */
typedef struct {
    int start;  ///< The offset from the start of the test data to be used for an SGL entry.
    int count;  ///< The number of bytes of test data to be used for an SGL entry.
} Entry;

/**
 * This structure defines the parameters of a single test case.
 */
typedef struct {
    const char * const name;                  ///< A name for this case, printed if the test failed.
    Entry entries[MAX_UNIT_TEST_SGL_ENTRIES]; ///< The SGL entries used to run the test case.
    int entry_count;                          ///< The number of valid array elements in the entries field.
    int start_offset;                         ///< The offset value to be passed in to CdiCoreGather().
    int byte_count;                           ///< The byte_count value to be passed in to CdiCoreGather().
    int expected_count;                       ///< The expected return value of CdiCoreGather().
} CaseParams;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/**
 * This is a block of 256 random byte values used for running the test cases.
 */
static uint8_t data[] = {
    0x9a, 0xd7, 0x04, 0xf4, 0x50, 0xaa, 0x87, 0x93, 0x8d, 0x5d, 0x8f, 0xb3, 0xc3, 0xcd, 0xc8, 0x6e,
    0x35, 0xdb, 0xfa, 0xcf, 0x02, 0xdd, 0xa9, 0x7c, 0x2c, 0x4b, 0x2e, 0x5b, 0x20, 0xe0, 0x23, 0xf6,
    0x43, 0xb4, 0x81, 0x3a, 0x93, 0xb6, 0x54, 0x4d, 0xbd, 0x08, 0x7d, 0x6b, 0xee, 0x4f, 0xef, 0x51,
    0x38, 0x88, 0x8c, 0x3e, 0xcd, 0x0e, 0xc0, 0x58, 0x97, 0x0c, 0xe8, 0x96, 0xec, 0xaa, 0x32, 0x97,
    0xba, 0xff, 0x3c, 0x43, 0xce, 0x90, 0xe5, 0xa0, 0xfb, 0x93, 0xf2, 0x77, 0x60, 0x21, 0x33, 0xf0,
    0x78, 0xa6, 0x64, 0xe4, 0x6a, 0xcc, 0x73, 0xba, 0x8c, 0x72, 0x63, 0x94, 0xbc, 0xfb, 0xb1, 0xe1,
    0x9b, 0x17, 0x79, 0x18, 0x53, 0xbc, 0x75, 0xe8, 0x0e, 0xfa, 0x23, 0x2b, 0x2b, 0x8a, 0x3b, 0x0f,
    0xc0, 0xd0, 0xc6, 0xf6, 0x66, 0xb4, 0x5b, 0x36, 0x02, 0xa0, 0xf0, 0xa6, 0xad, 0x40, 0x6b, 0x17,
    0x68, 0x4c, 0xc0, 0xb3, 0x9b, 0x23, 0xab, 0x03, 0x18, 0xcc, 0x0a, 0x20, 0x0a, 0x32, 0xeb, 0x64,
    0x46, 0x8d, 0x78, 0x57, 0xd4, 0x86, 0x03, 0x8e, 0xbd, 0x3b, 0x5f, 0x9f, 0x81, 0x44, 0x41, 0x6e,
    0xc9, 0xe0, 0x43, 0x0c, 0x4b, 0xe9, 0x8a, 0x6f, 0xe7, 0x1c, 0x47, 0xbf, 0x6e, 0x65, 0x45, 0xfc,
    0x8a, 0xf1, 0xdb, 0xb4, 0x8e, 0x93, 0x4d, 0xee, 0x7c, 0xd8, 0xd4, 0x4e, 0x35, 0x3c, 0x54, 0xe9,
    0xab, 0xc1, 0x71, 0x4b, 0x8a, 0x7c, 0xca, 0x2e, 0x30, 0x53, 0x64, 0xa6, 0x28, 0x29, 0x89, 0x88,
    0x9b, 0x83, 0xe5, 0x0c, 0x5c, 0x51, 0xc6, 0x39, 0xce, 0xb9, 0x68, 0x48, 0x11, 0xae, 0x8c, 0x8a,
    0x4b, 0xd2, 0x1c, 0xa2, 0x2c, 0x65, 0x6e, 0xb9, 0x47, 0x76, 0x14, 0xda, 0x26, 0x0e, 0xbd, 0x4d,
    0xf9, 0x59, 0x0c, 0x9f, 0x3d, 0xe1, 0x25, 0x99, 0x0c, 0x88, 0xfd, 0x65, 0xf4, 0x2d, 0x41, 0xc0,
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initializes a single SGL entry structure suitably for use in TestCase().
 *
 * @param sgl_entry_ptr The address of the structure to initialize.
 * @param address_ptr The data address to have the entry point to.
 * @param size_in_bytes The number of bytes to set the entry to.
 */
static void SglEntryInit(CdiSglEntry* sgl_entry_ptr, uint8_t* address_ptr, int size_in_bytes)
{
    sgl_entry_ptr->address_ptr = address_ptr;
    sgl_entry_ptr->size_in_bytes = size_in_bytes;
    sgl_entry_ptr->next_ptr = NULL;
    sgl_entry_ptr->internal_data_ptr = NULL;
#ifdef DEBUG_INTERNAL_SGL_ENTRIES
    sgl_entry_ptr->packet_sequence_num = 0;
    sgl_entry_ptr->payload_num = 0;
#endif
}

/**
 * Runs a single test case. Two buffers are used, the first is filled with the entire contents of the entries specified
 * in params. The second buffer is written into with CdiCoreGather(). The contents of the two buffers are then compared
 * and if they're the same the function returns true.
 *
 * @param params_ptr The address of the structure containing the test case parameters.
 *
 * @return bool true if the case passed, false if it failed.
 */
static bool TestCase(const CaseParams* params_ptr)
{
    int total_data_size = 0;
    for (int i = 0; i < params_ptr->entry_count; i++) {
        total_data_size += params_ptr->entries[i].count;
    }

    if (UNIT_TEST_BUFFER_SIZE < total_data_size) {
        return false;
    }

    // test_buffer1 is filled using entries then the start_offset and byte_count are applied when calling memcmp()
    uint8_t test_buffer1[UNIT_TEST_BUFFER_SIZE];
    uint8_t* p = test_buffer1;
    for (int i = 0; i < params_ptr->entry_count; i++) {
        memcpy(p, data + params_ptr->entries[i].start, params_ptr->entries[i].count);
        p += params_ptr->entries[i].count;
    }

    // test_buffer2 is filled using CdiSglGather(); first convert Entries into a proper SGL
    CdiSglEntry sgl_entries[MAX_UNIT_TEST_SGL_ENTRIES];
    for (int i = 0; i < params_ptr->entry_count; i++) {
        SglEntryInit(&sgl_entries[i], data + params_ptr->entries[i].start, params_ptr->entries[i].count);
    }
    for (int i = 0; i < params_ptr->entry_count - 1; i++) {
        sgl_entries[i].next_ptr = &sgl_entries[i + 1];
    }
    CdiSgList sgl = {
        .internal_data_ptr = NULL,
        .sgl_head_ptr = &sgl_entries[0],
        .sgl_tail_ptr = &sgl_entries[params_ptr->entry_count - 1],
        .total_data_size = total_data_size
    };
    uint8_t test_buffer2[UNIT_TEST_BUFFER_SIZE];
    const int actual_count = CdiCoreGather(&sgl, params_ptr->start_offset, test_buffer2, params_ptr->byte_count);
    bool ret = actual_count == params_ptr->expected_count;
    ret = ret && memcmp(test_buffer1 + params_ptr->start_offset, test_buffer2, actual_count) == 0;
    return ret;
}

/**
 * Runs all of the defined CdiCoreGather() test cases. Testing stops on the first failed case.
 *
 * @return bool true if all of the cases passed, false if one case failed.
 */
bool TestUnitSgl(void)
{
    CaseParams cases[] = {
        //                                                                         entry_count
        //                                                                             start_offset
        //                                                                                 byte_count
        // name                               entries                                           expected_count
        { "simple",                          {{  0, 256}                        }, 1,   0, 256, 256 },
        { "two",                             {{  0, 128}, {128, 128}            }, 2,   0, 256, 256 },
        { "offset",                          {{  0,  64}                        }, 1,  32,  32,  32 },
        { "source limited",                  {{  0,  64}                        }, 1,   0, 256,  64 },
        { "destination limited",             {{ 64,  64}                        }, 1,   0, 100,  64 },
        { "destination limited with offset", {{ 64,  64}                        }, 1,  10, 100,  54 },
        { "three",                           {{ 10,  18}, { 16,   1}, { 33,  25}}, 3,   9,  25,  25 },
        { "zero sized entry",                {{ 88,  10}, { 10,   0}, { 55,  55}}, 3,   4,  50,  50 },
    };

    bool failed = false;
    for (unsigned int i = 0; !failed && i < CDI_ARRAY_ELEMENT_COUNT(cases); i++) {
        failed = !TestCase(&cases[i]);
        if (failed) {
            CDI_LOG_THREAD(kLogError, "SGL test [%s] failed.", cases[i].name);
        }
    }

    return !failed;
}

