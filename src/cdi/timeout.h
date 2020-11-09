// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in timeout.c
 */

#ifndef CDI_TIMEOUT_H__
#define CDI_TIMEOUT_H__

#include <stdbool.h>

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_pool_api.h"
#include "fifo_api.h"
#include "list_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/// Maximum number of timers per timeout instance
#define MAX_TIMERS      25
/// Number of timers that may be added when increasing memory pool if needed.
#define MAX_TIMERS_GROW  5

/**
 * @brief TimeoutDataState is the basic object used to build the list of timers
 * This object contains a timeout, a next and previous handle, and user data
 * pointer, and a user data callback function pointer
 */
typedef struct TimeoutDataState {
    /// store an instance of this object in a list ordered by deadline_us values
    CdiListEntry list_entry;
    /// when will this object expire in microseconds
    uint64_t deadline_us;
    /// pointer to callback function to execute if this item expires
    void* cb_ptr;
    /// pointer to user data that can be used in callback function
    void* user_data_ptr;
    //struct TimeoutDataState*  next_handle;
    //struct TimeoutDataState*  prev_handle;
} TimeoutDataState;

/**
 * @brief This is the handle for TimeoutDataState
 */
typedef struct TimeoutDataState* TimeoutHandle;

/** @brief This structure contains all of the state information for the timer
 * instance. This includes signals, thread ID's, and pointers to the memory
 * pool and timer list
 */
typedef struct TimeoutInstanceState {
    /// Thread ID for the main timeout management thread
    CdiThreadID    main_thread_id;
    /// Thread ID for the thread that executes the callback functions
    CdiThreadID    cb_thread_id;
    /// handle for the FIFO that main thread puts callbacks into for callback thread to execute
    CdiFifoHandle  cb_fifo_handle;
    /// go_signal indicates that there is at least one active timer entry
    CdiSignalType  go_signal;
    /// shutdown timer instance signaled from CdiTimeoutDestroy
    CdiSignalType  shutdown_signal;
    /// stop_signal indicates someone has removed active timeout or added a timeout with a closer deadline
    CdiSignalType  stop_signal;
    /// list of active timeout objects
    CdiList        timeout_list;
    /// Pool of available TimeoutDataState objects
    CdiPoolHandle  mem_pool_handle;
    /// Critical section to indicate that timeout_list is being modified
    CdiCsID        critical_section;
    /// Handle of log to use for this timeout's main thread.
    CdiLogHandle log_handle;
} TimeoutInstanceState;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// @brief a handle for use with TimeoutInstanceState structure
typedef struct TimeoutInstanceState* CdiTimeoutInstanceHandle;

/// @brief Structure for Callback functions to use to return data from callback
typedef struct {
    TimeoutHandle handle; ///<Handle for timeout data state.
    void *user_data_ptr; ///<Pointer to data space for this handle.
} CdiTimeoutCbData;

/// @brief Callback function definition used by CdiTimeoutAdd
typedef void (*CdiTimeoutCallback)(const CdiTimeoutCbData* data_ptr);

/**
 * @brief Creates a TimeoutInstanceState structure memory allocated for a pool of
 * TimeoutDataState structures as well as all signals and threads setup
 *
 * @param log_handle Handle of log to use for log messages by this object's internal threads.
 * @param ret_handle returns the handle to the TimeoutInstanceState structure
 *
 * @return kCdiStatusOk if successful, other CdiReturnStatus codes on failure
 */
CdiReturnStatus CdiTimeoutCreate(CdiLogHandle log_handle, CdiTimeoutInstanceHandle* ret_handle);

/**
 *  @brief Destroys a TimeoutInstanceState structure freeing associated mememory
 *  and other resources such as waking associated threads get them to exit.
 *
 *  @param handle is the handle for the TimeoutInstanceState structure to be
 *  destroyed and have associated resources freed.
 *
 */
void CdiTimeoutDestroy(CdiTimeoutInstanceHandle handle);

/**
 * @brief Adds a timeout to the active timeout list and puts the timeout in order based on when the timeout occurs
 *
 * @param instance_handle Pointer to this timeout instance.
 * @param cb_ptr Pointer to Callback data structure CdiTimeoutCbData
 * @param timeout_us Timeout time in microseconds
 * @param user_data_ptr Pointer to user data for this object.
 * @param ret_handle a Handle returned of CdiTimeoutCbData object
 *
 * @return true if timeout was successfully added to timeout list; false if failed
 */
bool CdiTimeoutAdd(CdiTimeoutInstanceHandle instance_handle, CdiTimeoutCallback cb_ptr, int timeout_us,
                   void* user_data_ptr, TimeoutHandle* ret_handle);

/**
 * @brief Removes a timeout from the timeout list thread, generally upon completion of operation
 *
 * @param handle for the timeout that is to be removed from the timeout list
 * @param instance_handle for the thread level state data
 *
 * @return true if timeout has successfully been removed from timeout list. false if failed
 */
bool CdiTimeoutRemove(TimeoutHandle handle, CdiTimeoutInstanceHandle instance_handle);

#endif // CDI_TIMEOUT_H__
