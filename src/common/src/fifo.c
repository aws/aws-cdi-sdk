// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and implementation for a simple FIFO.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "fifo_api.h"

#include <stddef.h>
#include <assert.h>

#include "configuration.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/**
 * @brief Structure used to hold state data for a single FIFO.
 */
typedef struct {
    char name_str[MAX_FIFO_NAME_LENGTH]; ///< Name of FIFO. Used for informational purposes only

    int head_index;                      ///< Current head index position in FIFO.
    int tail_index;                      ///< Current tail index position in FIFO.
    int item_count;                      ///< Number of items FIFO can store.
    int item_byte_size;                  ///< Size in bytes of each FIFO item.
    uint8_t* item_array;                 ///< Pointer to start of the array of FIFO item buffers.

    CdiCsID read_lock;                  ///< Critical section lock for FIFO reads.
    CdiCsID write_lock;                 ///< Critical section lock for FIFO writes.
    CdiSignalType just_pushed_signal;   ///< Signal used to identify whenever a FIFO item is pushed on the FIFO.
    CdiSignalType just_popped_signal;   ///< Signal used to identify whenever a FIFO item is popped off the FIFO.

    CdiFifoFullCallback full_cb_ptr;    ///< Pointer to user-provided FIFO full callback function.
    CdiUserCbParameter full_user_cb_param; ///< User-provided parameter passed in structure to full_cb_ptr.
#ifdef DEBUG
    CdiFifoCallback debug_cb_ptr;       ///< Pointer to user-provided debug callback function.
#endif
} CdiFifoState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool CdiFifoCreate(const char* name_str, int item_count, int item_byte_size, CdiFifoFullCallback full_cb_ptr,
                   CdiUserCbParameter full_user_cb_param, CdiFifoHandle* ret_handle_ptr)
{
    bool ret = true;

    CdiFifoState* state_ptr = (CdiFifoState*)CdiOsMemAllocZero(sizeof(CdiFifoState));
    if (NULL == state_ptr) {
        return false;
    }

    // Save a copy of the FIFO name.
    CdiOsStrCpy(state_ptr->name_str, sizeof(state_ptr->name_str), name_str);

    state_ptr->full_cb_ptr = full_cb_ptr;
    state_ptr->full_user_cb_param = full_user_cb_param;

    // Create critical sections.
    if (!CdiOsCritSectionCreate(&state_ptr->read_lock)) {
        ret = false;
    }

    if (!CdiOsCritSectionCreate(&state_ptr->write_lock)) {
        ret = false;
    }

    // Create signals.
    if (ret && !CdiOsSignalCreate(&state_ptr->just_pushed_signal)) {
        ret = false;
    }

    if (ret && !CdiOsSignalCreate(&state_ptr->just_popped_signal)) {
        ret = false;
    }

    if (ret) {
        // Allocate memory for the FIFO item buffers.
        state_ptr->item_array = (uint8_t *)CdiOsMemAllocZero(item_count * item_byte_size);
        if (NULL == state_ptr->item_array) {
            ret = false;
        }
    }

    if (ret) {
        // Initialize FIFO state data.
        state_ptr->head_index = 0;
        state_ptr->tail_index = 0;
        state_ptr->item_count = item_count;
        state_ptr->item_byte_size = item_byte_size;
    } else {
        // An error occurred, so clean-up any allocated FIFO resources.
        CdiFifoDestroy((CdiFifoHandle)state_ptr);
        state_ptr = NULL;
    }

    *ret_handle_ptr = (CdiFifoHandle)state_ptr;

    return ret;
}

void CdiFifoFlush(CdiFifoHandle handle)
{
    if (handle) {
        // Walk through each FIFO entry, removing it from the FIFO.
        while (CdiFifoRead(handle, 0, NULL, NULL));
    }
}

bool CdiFifoRead(CdiFifoHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, void* item_dest_ptr)
{
    bool ret = true;
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    CdiOsCritSectionReserve(state_ptr->read_lock);
    CdiOsSignalClear(state_ptr->just_pushed_signal);

    // Check if FIFO is empty.
    if (state_ptr->head_index == state_ptr->tail_index) {
        // FIFO is empty, so setup to wait for an item to be pushed to it.
        CdiSignalType signal_array[2];
        signal_array[0] = state_ptr->just_pushed_signal;
        signal_array[1] = abort_wait_signal;

        int signal_count = 1;
        if (abort_wait_signal) {
            signal_count++; // Abort signal is being used, so bump the signal count.
        }

        do {
            uint32_t signal_index;
            CdiOsSignalsWait(signal_array, signal_count, false, timeout_ms, &signal_index);
            if (0 != signal_index) {
                // Wait was aborted (signal_index=1) or timed-out (signal_index=OS_SIG_TIMEOUT).
                ret = false;
                break;
            }
        } while (state_ptr->head_index == state_ptr->tail_index); // Ensure FIFO is not empty.
    }

    if (ret) {
        if (item_dest_ptr) {
            // Copy the data from the FIFO buffer to the memory pointed to by item_dest_ptr.
            memcpy(item_dest_ptr, &state_ptr->item_array[state_ptr->tail_index * state_ptr->item_byte_size],
                state_ptr->item_byte_size);
        }

#ifdef DEBUG
        if (state_ptr->debug_cb_ptr) {
            CdiFifoCbData cb_data = {
                .is_read = true,
                .head_index = state_ptr->head_index,
                .tail_index = state_ptr->tail_index,
                .item_data_ptr = item_dest_ptr
            };
            (state_ptr->debug_cb_ptr)(&cb_data);
        }
#endif

        // Update the tail index and set the just popped item signal.
        state_ptr->tail_index = (state_ptr->tail_index + 1) % state_ptr->item_count;
        CdiOsSignalSet(state_ptr->just_popped_signal);
    }

    CdiOsCritSectionRelease(state_ptr->read_lock);

    return ret;
}

bool CdiFifoWrite(CdiFifoHandle handle, int timeout_ms, CdiSignalType abort_wait_signal, const void* data_ptr)
{
    bool ret = true;
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    CdiOsCritSectionReserve(state_ptr->write_lock);
    CdiOsSignalClear(state_ptr->just_popped_signal);

    int new_head_index = (state_ptr->head_index + 1) % state_ptr->item_count;

    // Check if FIFO is full.
    if (ret && new_head_index == state_ptr->tail_index) {
        // FIFO is full, so setup to wait for an item to be popped off it.
        CdiSignalType signal_array[2];
        signal_array[0] = state_ptr->just_popped_signal;
        signal_array[1] = abort_wait_signal;

        int signal_count = 1;
        if (abort_wait_signal) {
            signal_count++; // Abort signal is being used, so bump the signal count.
        }
        do {
            uint32_t signal_index;
            CdiOsSignalsWait(signal_array, signal_count, false, timeout_ms, &signal_index);
            if (0 != signal_index) {
                // Wait was aborted (signal_index=1) or timed-out (signal_index=OS_SIG_TIMEOUT).
                ret = false;
                break;
            }
        } while (new_head_index == state_ptr->tail_index); // Ensure FIFO is not full.
    }

    // If FIFO was full and full callback address exists, invoke the callback.
    if (!ret && state_ptr->full_cb_ptr) {
        // Lock reads, so tail_index cannot be changed.
        CdiOsCritSectionReserve(state_ptr->read_lock);

        int last_write_index = (state_ptr->head_index - 1) % state_ptr->item_count;
        void* item_head_ptr = &state_ptr->item_array[last_write_index * state_ptr->item_byte_size];
        void* item_tail_ptr = &state_ptr->item_array[state_ptr->tail_index * state_ptr->item_byte_size];

        CdiFifoFullCbData cb_data = {
            .fifo_handle = handle,
            .fifo_user_cb_param = state_ptr->full_user_cb_param,
            .new_item_data_ptr = data_ptr,
            .head_item_data_ptr = item_head_ptr,
            .tail_item_data_ptr = item_tail_ptr,
        };
        // Invoke the FIFO full callback.
        (state_ptr->full_cb_ptr)(&cb_data);

        CdiOsCritSectionRelease(state_ptr->read_lock);
    }

    if (ret) {
        // Copy the data to the FIFO buffer before updating the head_index pointer, so the read operation always has
        // valid data.
        void* item_dest_ptr = &state_ptr->item_array[state_ptr->head_index * state_ptr->item_byte_size];
        memcpy(item_dest_ptr, data_ptr, state_ptr->item_byte_size);

#ifdef DEBUG
        if (state_ptr->debug_cb_ptr) {
            CdiFifoCbData cb_data = {
                .is_read = false,
                .head_index = state_ptr->head_index,
                .tail_index = state_ptr->tail_index,
                .item_data_ptr = item_dest_ptr
            };
            (state_ptr->debug_cb_ptr)(&cb_data);
        }
#endif

        // Update the head index and set the just pushed item signal.
        state_ptr->head_index = new_head_index;
        CdiOsSignalSet(state_ptr->just_pushed_signal);
    }

    CdiOsCritSectionRelease(state_ptr->write_lock);

    return ret;
}

const char* CdiFifoGetName(CdiFifoHandle handle)
{
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    if (state_ptr) {
        return state_ptr->name_str;
    }

    return NULL;
}

#ifdef DEBUG
void CdiFifoDebugEnable(CdiFifoHandle handle, CdiFifoCallback cb_ptr)
{
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    state_ptr->debug_cb_ptr = cb_ptr;
}

void CdiFifoDebugDisable(CdiFifoHandle handle)
{
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    state_ptr->debug_cb_ptr = NULL;
}
#endif //DEBUG

void CdiFifoDestroy(CdiFifoHandle handle)
{
    CdiFifoState* state_ptr = (CdiFifoState*)handle;

    if (state_ptr) {
        // Ensure that the FIFO is empty.
        assert(state_ptr->head_index == state_ptr->tail_index);

        // Not setting to NULL, since the memory is directly freed below.
        CdiOsCritSectionDelete(state_ptr->read_lock);
        CdiOsCritSectionDelete(state_ptr->write_lock);
        CdiOsSignalDelete(state_ptr->just_pushed_signal);
        CdiOsSignalDelete(state_ptr->just_popped_signal);

        if (state_ptr->item_array) {
            CdiOsMemFree(state_ptr->item_array);
        }
        CdiOsMemFree(state_ptr);
    }
}
