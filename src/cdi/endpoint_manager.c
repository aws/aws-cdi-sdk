// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used with the SDK that is not part of the API. The
 * Endpoint Manager is used to synchronize connection reset and shutdown events across all threads that are related to
 * the connection.
 */

// Page for Endpoint Manager.
/*!
 * @page endpoint_manager_main_page Endpoint Manager Architecture
 * @tableofcontents
 *
 * @section endpoint_manager_overview Architecture Overview
 *
 * In order to coordinate endpoint state changes such as reset, start and shutdown operations, a specific sequence of
 * events must occur. There are many resouces such as queues, memory pools and threads that are used for an endpoint.
 * All threads related to an endpoint must be blocked before any resource changes such as flushing queues or returning
 * used memory pool items to their pool can be made. Once a state change completes, all threads are unblocked and return
 * to their normal running state. The specific steps used are described below:
 * 1. All threads related to the connection must register with then Endpoint Manager using
 *    EndpointManagerThreadRegister(). This does not include threads related to the probe control interface such as
 *    ProbeControlThread(), since it runs all the time (never gets blocked).
 * 2. When a request to perform an endpoint state change is made using EndpointManagerQueueEndpointReset(),
 *    EndpointManagerQueueEndpointStart() or EndpointManagerShutdownConnection(), the
 *    #EndpointManagerState::new_command_signal is set. The Poll thread must call EndpointManagerPoll() as part of its
 *    normal poll loop to determine if it should perform adapter level polling or not. All other registered threads must
 *    monitor this signal and when set, must call EndpointManagerThreadWait(), which blocks the thread.
 * 3. After the non-poll registered threads have called EndpointManagerThreadWait(), the endpoint state change is
 *    carried out using EndpointManagerThread().
 * 4. After the endpoint state change completes, the registered threads that are blocked in EndpointManagerThreadWait()
 *    are allowed to continue and calls to EndpointManagerPoll() by the poll thread will return true (can call adapter
 *    poll functions). NOTE: In a shutdown condition, the EndpointManagerThread() exits as part of this process.
 *
 * The diagram shown below provides an overview of the Endpoint Manager architecture.
 * @image html "endpoint_manager_architecture.jpg"
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h" // Must include this file first due to #define _GNU_SOURCE
#include "endpoint_manager.h"

#include <arpa/inet.h> // For inet_ntop()

#include "adapter_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "internal.h"
#include "internal_rx.h"
#include "internal_tx.h"
#include "internal_utility.h"
#include "rx_reorder_payloads.h"
#include "statistics.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This defines a structure that contains all the state information for endpoint state changes.
 */
typedef struct {
    /// @brief Used to store an instance of this object in a list using this element as the list item.
    CdiListEntry list_entry;

    EndpointManagerState* endpoint_manager_ptr; ///< Pointer to Endpoint Manager.

    CdiQueueHandle command_queue_handle; ///< Queue used to hold endpoint state change commands.
    volatile bool got_new_command;       ///< True if got a new command.
    volatile bool got_shutdown;          ///< True if got a connection shutdown command.
    bool queued_to_destroy;              ///< True if endpoint is queued to be destroyed

    CdiEndpointState cdi_endpoint;       ///< CDI endpoint state associated with this endpoint.
} InternalEndpointState;

/**
 * @brief This defines a structure that contains all the state information for endpoint state changes.
 */
struct EndpointManagerState {
    CdiConnectionState* connection_state_ptr; ///< Pointer to connection associated with this Endpoint Manager.

    /// @brief Lock used to protect access to endpoint_list, when required.
    CdiCsID endpoint_list_lock;
    /// @brief List of endpoints associated with this connection (InternalEndpointState).
    CdiList endpoint_list;

    /// @brief Queue used to hold handles of endpoints that need to be destroyed.
    CdiQueueHandle destroy_endpoint_queue_handle;
    CdiSignalType endpoints_destroyed_signal; ///< Signal used when endpoints in queue are destroyed.

    volatile bool got_shutdown;        ///< True if got a connection shutdown command.

    /// @brief True if Endpoint Manager thread is done and exiting (or has exited). NOTE: Must use state_lock when
    /// accessing it.
    volatile bool thread_done;

    CdiThreadID thread_id;             ///< Endpoint state thread identifier

    /// @brief Lock used to protect access to endpoint state.
    CdiCsID state_lock;

    CdiSignalType shutdown_signal;     ///< Signal used to shutdown Endpoint Manager.
    CdiSignalType new_command_signal;  ///< Signal used to start processing a command.
    uint32_t queued_commands_count;    ///< Total number of pending commands in endpoint queues.
    CdiSignalType command_done_signal; ///< Signal used when command processing has finished.

    volatile bool poll_thread_waiting;     ///< If true, poll thread is running, but not using any resources.
    CdiSignalType poll_thread_exit_signal; ///< Signal used when poll thread is exiting,

    /// @brief Signal used when all registered threads are waiting. Signal is set in EndpointManagerThreadWait() when
    // thread_wait_count equals tx_registered_thread_count (if Tx) or rx_registered_thread_count (if Rx).
    CdiSignalType all_threads_waiting_signal;

    /// @brief Signal used when all registered threads are running. Signal is set at initialization and in
    /// EndpointManagerThreadWait() when thread_wait_count reaches zero.
    CdiSignalType all_threads_running_signal;

    int thread_wait_count;       ///< Number of endpoint threads that are waiting.
    int registered_thread_count; ///< Number of registered threads associated with this endpoint.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Convert an CDI endpoint handle into and internal endpoint state structure (InternalEndpointState).
 *
 * @param handle CDI Endpoint handle
 *
 * @return Pointer to internal endpoint state structure.
 */
static InternalEndpointState* CdiEndpointToInternalEndpoint(CdiEndpointHandle handle) {
    InternalEndpointState* endpoint_ptr = CONTAINER_OF(handle, InternalEndpointState, cdi_endpoint);
    return endpoint_ptr;
}

/**
 * Add a new command to the endpoint queue to be processed by EndpointManagerThread().
 *
 * @param internal_endpoint_ptr Pointer to internal endpoint structure to queue new command.
 * @param command New command to add to the queue.
 *
 * @return True if command placed in queue, otherwise false is returned.
 */
static bool SetCommand(InternalEndpointState* internal_endpoint_ptr, EndpointManagerCommand command)
{
    bool ret = false;
    EndpointManagerState* mgr_ptr = internal_endpoint_ptr->endpoint_manager_ptr;
    CdiEndpointHandle handle = &internal_endpoint_ptr->cdi_endpoint;
    const char* remote_ip_str = EndpointManagerEndpointRemoteIpGet(handle);
    const int remote_port = EndpointManagerEndpointRemotePortGet(handle);
    const char* command_str = InternalUtilityKeyEnumToString(kKeyEndpointManagerCommand, command);

    // Prevent the signals/variables used in this block from being accessed by other threads.
    CdiOsCritSectionReserve(mgr_ptr->state_lock);

    // Ignore all new commands if we got a shutdown command.
    if (!internal_endpoint_ptr->got_shutdown) {
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager,
            "Endpoint Manager remote IP[%s:%d] queuing command[%s].", remote_ip_str, remote_port, command_str);
        internal_endpoint_ptr->got_new_command = true;
        if (kEndpointStateShutdown == command) {
            internal_endpoint_ptr->got_shutdown = true;
        }
        // Increment counter before pushing into the queue, since it may be immediately popped off.
        CdiOsAtomicInc32(&mgr_ptr->queued_commands_count);
        if (!CdiQueuePush(internal_endpoint_ptr->command_queue_handle, &command)) {
            // Queue full, so decrement counter and generate log message.
            CdiOsAtomicDec32(&mgr_ptr->queued_commands_count);
            CDI_LOG_THREAD(kLogInfo, "Add endpoint command queue[%s] full.",
                           CdiQueueGetName(internal_endpoint_ptr->command_queue_handle));
            internal_endpoint_ptr->got_new_command = false;
        } else {
            CdiOsSignalSet(mgr_ptr->new_command_signal);
            ret = true;
        }
    } else {
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager,
            "Endpoint Manager remote IP[%s:%d] ignoring command[%s] while shutting down.",
            remote_ip_str, remote_port, command_str);
    }

    CdiOsCritSectionRelease(mgr_ptr->state_lock);

    return ret;
}

/**
 * Flush resources associated with the specified connection.
 *
 * @param endpoint_ptr Pointer to endpoint to free resources.
 *
 * @return Status code indicating success or failure.
 */
static CdiReturnStatus FlushResources(InternalEndpointState* endpoint_ptr)
{
    EndpointManagerState* mgr_ptr = endpoint_ptr->endpoint_manager_ptr;

    if (kHandleTypeTx == mgr_ptr->connection_state_ptr->handle_type) {
        // Clean up TxPayloadThread() resources.
        TxPayloadThreadFlushResources(&endpoint_ptr->cdi_endpoint);
        // Clean up PollThread() resources.
        CdiAdapterPollThreadFlushResources(endpoint_ptr->cdi_endpoint.adapter_endpoint_ptr);
    } else {
        // Clean up Rx endpoint resources.
        RxEndpointFlushResources(&endpoint_ptr->cdi_endpoint);
    }

    // Clean up adapter level resources used by PollThread(). NOTE: For the EFA adapter, it will notify EFA Probe that
    // resetting the endpoint has completed. Therefore, this step must be the last one used as part of the connection
    // reset sequence.
    return CdiAdapterResetEndpoint(endpoint_ptr->cdi_endpoint.adapter_endpoint_ptr);
}

/**
 * Destroy an endpoint, closing its adapter endpoint and freeing resources used by it.
 *
 * @param handle Handle of endpoint to destroy.
 */
static void DestroyEndpoint(CdiEndpointHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle->connection_state_ptr->endpoint_manager_handle;

    // Get thread-safe access to endpoint resources. Users can free buffers via RxEnqueueFreeBuffer() while internally
    // an endpoint is being destroyed here.
    CdiOsCritSectionReserve(mgr_ptr->connection_state_ptr->adapter_connection_ptr->endpoint_lock);

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager, "Destroying endpoint IP[%s:%d]",
                             EndpointManagerEndpointRemoteIpGet(handle), EndpointManagerEndpointRemotePortGet(handle));

    InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    FlushResources(internal_endpoint_ptr);

    // Close the adapter endpoint, if it exists.
    if (handle->adapter_endpoint_ptr) {
        CdiAdapterCloseEndpoint(handle->adapter_endpoint_ptr);
        handle->adapter_endpoint_ptr = NULL;
    }

    if (kHandleTypeTx == handle->connection_state_ptr->handle_type) {
        TxEndpointDestroy(handle);
    } else {
        RxEndpointDestroy(handle);
    }

    // Walk through the endpoint list and try to find this endpoint. If it is in the list, remove it.
    CdiEndpointHandle list_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
    while (list_endpoint_handle) {
        if (handle == list_endpoint_handle) {
            internal_endpoint_ptr = CdiEndpointToInternalEndpoint(list_endpoint_handle);
            // Must protect access to the list when removing an entry.
            CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
            CdiListRemove(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
            CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
            break;
        }
        list_endpoint_handle = EndpointManagerGetNextEndpoint(list_endpoint_handle);
    }

    internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    if (internal_endpoint_ptr->command_queue_handle) {
        // Pull items off queue one at a time so we can adjust queued_commands_count.
        EndpointManagerCommand command;
        while (CdiQueuePop(internal_endpoint_ptr->command_queue_handle, &command)) {
            assert(0 != mgr_ptr->queued_commands_count);
            CdiOsAtomicDec32(&mgr_ptr->queued_commands_count);
        }
        CdiQueueDestroy(internal_endpoint_ptr->command_queue_handle);

        // Invalidate the endpoint state in case the application tries to use its handle again.
        internal_endpoint_ptr->cdi_endpoint.magic = 0;
    }

    CdiOsMemFree(internal_endpoint_ptr);

    CdiOsCritSectionRelease(mgr_ptr->connection_state_ptr->adapter_connection_ptr->endpoint_lock);
}

/**
 * Thread used to manage endpoint reset, start and shutdown events.
 *
 * @param ptr Pointer to Endpoint Manager state data.
 *
 * @return The return value is not used.
 */
static CDI_THREAD EndpointManagerThread(void* ptr)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)ptr;

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager, "Endpoint Manager thread starting. Connection name[%s].",
                             mgr_ptr->connection_state_ptr->saved_connection_name_str);

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(mgr_ptr->connection_state_ptr->log_handle);

    CdiSignalType signal_array[3];
    signal_array[0] = mgr_ptr->all_threads_waiting_signal; // If set, have command to process.
    signal_array[1] = mgr_ptr->shutdown_signal;            // If set, shutting down.
    signal_array[2] = mgr_ptr->poll_thread_exit_signal;    // If set, poll thread is exiting.
    uint32_t signal_index = 0;

    bool keep_alive = true;
    while (!CdiOsSignalGet(mgr_ptr->shutdown_signal) && keep_alive) {
        // Wait for all registered threads to be waiting, a shutdown or poll thread is exiting.
        CdiOsSignalsWait(signal_array, 3, false, CDI_INFINITE, &signal_index);
        if (0 == signal_index) {
            // Got all_threads_waiting_signal, so Walk through the endpoints.
            CdiOsSignalClear(mgr_ptr->all_threads_waiting_signal);

            // Walk through the list of endpoints associated with this Endpoint Manager and process commands in the
            // endpoint's queue.
            CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
            while (endpoint_handle) {
                InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(endpoint_handle);
                EndpointManagerCommand command;
                while (internal_endpoint_ptr && CdiQueuePop(internal_endpoint_ptr->command_queue_handle, &command)) {
                    assert(0 != mgr_ptr->queued_commands_count);
                    CdiOsAtomicDec32(&mgr_ptr->queued_commands_count);
                    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager,
                                            "Endpoint Manager remote IP[%s:%d] processing command[%s]",
                                            EndpointManagerEndpointRemoteIpGet(endpoint_handle),
                                            EndpointManagerEndpointRemotePortGet(endpoint_handle),
                                            InternalUtilityKeyEnumToString(kKeyEndpointManagerCommand, command));
                    CdiReturnStatus rs = kCdiStatusOk;
                    switch (command) {
                        case kEndpointStateIdle:
                            // Nothing special to do.
                            break;
                        case kEndpointStateReset:
                            rs = FlushResources(internal_endpoint_ptr);
                            break;
                        case kEndpointStateStart:
                            rs = CdiAdapterStartEndpoint(endpoint_handle->adapter_endpoint_ptr);
                            break;
                        case kEndpointStateShutdown:
                            rs = FlushResources(internal_endpoint_ptr);
                            keep_alive = false;
                            break;
                    }
                    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager, "Processing command [%s] complete[%s]",
                        InternalUtilityKeyEnumToString(kKeyEndpointManagerCommand, command),
                        CdiCoreStatusToString(rs));
                }
                endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
            }
        }

        // Commands have completed. Set signal to unblock registered connection threads that are blocked in
        // EndpointManagerThreadWait() so they can continue running.
        CdiOsSignalSet(mgr_ptr->command_done_signal);
    }

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager, "Endpoint Manager thread exiting. Connection name[%s].",
                            mgr_ptr->connection_state_ptr->saved_connection_name_str);

    // Acquire lock before accessing the resources below.
    CdiOsCritSectionReserve(mgr_ptr->state_lock);
    mgr_ptr->thread_done = true;
    // set new_command_signal, since watchers use it to also wakeup in the event of a shutdown.
    CdiOsSignalSet(mgr_ptr->new_command_signal);
    CdiOsSignalSet(mgr_ptr->command_done_signal);
    CdiOsCritSectionRelease(mgr_ptr->state_lock);

    CdiLoggerThreadLogUnset();
    return 0; // Return code not used.
}

/**
 * Increment the thread wait counter. If the count matches the number of threads registered to wait, a signal will be
 * set to wakeup EndpointManagerThread() so it can process pending endpoint state changes.
 *
 * @param mgr_ptr Pointer to Endpoint Manager state data.
 */
static void IncrementThreadWaitCount(EndpointManagerState* mgr_ptr)
{
    // Increment the thread wait counter.
    int current_count = CdiOsAtomicInc32(&mgr_ptr->thread_wait_count);

    // If all registered threads are here, then ok to start processing the new state.
    if (current_count >= mgr_ptr->registered_thread_count) {
        // Acquire lock before accessing the resources below.
        CdiOsCritSectionReserve(mgr_ptr->state_lock);
        // Clear the new_command_signal if the Endpoint Manager thread is running and there are no commands in the
        // queue.
        if (!mgr_ptr->thread_done && 0 == CdiOsAtomicLoad32(&mgr_ptr->queued_commands_count)) {
            CdiOsSignalClear(mgr_ptr->new_command_signal);
        }
        CdiOsCritSectionRelease(mgr_ptr->state_lock);

        // Clear signal used to ensure all threads have exited this function (none are blocked).
        CdiOsSignalClear(mgr_ptr->all_threads_running_signal);
        // Set signal to wakeup EndpointManagerThread() so it can process the new state.
        CdiOsSignalSet(mgr_ptr->all_threads_waiting_signal);
    }
}

/**
 * Decrement the thread wait counter. If the count reaches zero, the state of signals will be changed so additional
 * state change commands can be processed by EndpointManagerThread().
 *
 * @param mgr_ptr Pointer to Endpoint Manager state data.
 */
static void DecrementThreadWaitCount(EndpointManagerState* mgr_ptr)
{
    // Decrement the thread wait counter. When it reaches zero, all threads have reached here and are running
    // again, so update signals to allow another command to start.
    int current_count = CdiOsAtomicDec32(&mgr_ptr->thread_wait_count);
    assert(current_count >= 0);
    if (0 == current_count) {
        // Acquire lock before accessing the resources below.
        CdiOsCritSectionReserve(mgr_ptr->state_lock);
        // Clear the command_done_signal if the Endpoint Manager thread is still running.
        if (!mgr_ptr->thread_done) {
            CdiOsSignalClear(mgr_ptr->command_done_signal);
        }
        CdiOsCritSectionRelease(mgr_ptr->state_lock);

        CdiOsSignalSet(mgr_ptr->all_threads_running_signal);
    }
}

/**
 * Create resources common to both Tx and Rx endpoints.
 *
 * @param mgr_ptr Pointer to Endpoint Manager.
 * @param ret_internal_endpoint_ptr Address where to write returned endpoint handle.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus CreateEndpointCommonResources(EndpointManagerState* mgr_ptr,
                                                     InternalEndpointState** ret_internal_endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    InternalEndpointState* internal_endpoint_ptr = NULL;

    if (CdiListCount(&mgr_ptr->endpoint_list) >= CDI_MAX_ENDPOINTS_PER_CONNECTION) {
        CDI_LOG_THREAD(kLogError,
            "Failed to create endpoint. Already at the maximum[%d] allowed in a single connection.",
            CDI_MAX_ENDPOINTS_PER_CONNECTION);
        rs = kCdiStatusArraySizeExceeded;
    }

    if (kCdiStatusOk == rs) {
        internal_endpoint_ptr = (InternalEndpointState*)CdiOsMemAllocZero(sizeof *internal_endpoint_ptr);
        if (internal_endpoint_ptr == NULL) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        internal_endpoint_ptr->endpoint_manager_ptr = mgr_ptr;
        internal_endpoint_ptr->cdi_endpoint.magic = kMagicEndpoint;
        internal_endpoint_ptr->cdi_endpoint.connection_state_ptr = mgr_ptr->connection_state_ptr;

        if (!CdiQueueCreate("Endpoint Command Queue", MAX_ENDPOINT_COMMAND_QUEUE_SIZE, NO_GROW_SIZE, NO_GROW_COUNT,
                            sizeof(EndpointManagerCommand), kQueueSignalNone,
                            &internal_endpoint_ptr->command_queue_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk != rs && internal_endpoint_ptr) {
        CdiOsMemFree(internal_endpoint_ptr);
        internal_endpoint_ptr = NULL;
    }

    *ret_internal_endpoint_ptr = internal_endpoint_ptr;

    return rs;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus EndpointManagerCreate(CdiConnectionHandle handle, CdiCoreStatsCallback stats_cb_ptr,
                                      CdiUserCbParameter stats_user_cb_param,
                                      const CdiStatsConfigData* stats_config_ptr, EndpointManagerHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)CdiOsMemAllocZero(sizeof *mgr_ptr);
    if (mgr_ptr == NULL) {
        return kCdiStatusNotEnoughMemory;
    }

    mgr_ptr->connection_state_ptr = handle;

    if (!CdiOsSignalCreate(&mgr_ptr->shutdown_signal)) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->new_command_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->all_threads_running_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        } else {
            // Set by default so on startup, threads are not blocked in EndpointManagerThreadWait().
            CdiOsSignalSet(mgr_ptr->all_threads_running_signal);
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->poll_thread_exit_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->all_threads_waiting_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->command_done_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&mgr_ptr->endpoints_destroyed_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsCritSectionCreate(&mgr_ptr->endpoint_list_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsCritSectionCreate(&mgr_ptr->state_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiQueueCreate("DestroyEndpoint Queue", CDI_MAX_ENDPOINTS_PER_CONNECTION, NO_GROW_SIZE, NO_GROW_COUNT,
                            sizeof(CdiEndpointHandle), kQueueSignalNone,
                            &mgr_ptr->destroy_endpoint_queue_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create statistics state resources.
        rs = StatsCreate(handle, stats_cb_ptr, stats_user_cb_param, cdi_global_context.cw_sdk_handle,
                         cdi_global_context.metrics_gathering_sdk_handle, &handle->stats_state_ptr);
    }
    if (kCdiStatusOk == rs) {
        // Set the initial stats configuration settings. Since they have not been set yet, use true here to ensure
        // they are applied.
        rs = CoreStatsConfigureInternal(handle, stats_config_ptr, true);
    }

    if (kCdiStatusOk == rs) {
        CdiListInit(&mgr_ptr->endpoint_list);

        // Start the thread which will service endpoint state changes.
        if (!CdiOsThreadCreate(EndpointManagerThread, &mgr_ptr->thread_id, "EPManager", mgr_ptr,
                               handle->start_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk != rs) {
        EndpointManagerDestroy(mgr_ptr);
        CdiOsMemFree(mgr_ptr);
        mgr_ptr = NULL;
    }

    *ret_handle_ptr = (EndpointManagerHandle)mgr_ptr;

    return rs;
}

void EndpointManagerDestroy(EndpointManagerHandle handle)
{
    if (handle) {
        EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

        if (mgr_ptr->thread_id) {
            // Clean-up thread resources by waiting here for it to exit using thread join.
            CdiOsThreadJoin(mgr_ptr->thread_id, CDI_INFINITE, NULL);
            mgr_ptr->thread_id = NULL;
        }
        // Now that the thread has stopped, it is safe to clean up the remaining resources.

        StatsDestroy(mgr_ptr->connection_state_ptr->stats_state_ptr);
        mgr_ptr->connection_state_ptr->stats_state_ptr = NULL;

        CdiQueueDestroy(mgr_ptr->destroy_endpoint_queue_handle);
        mgr_ptr->destroy_endpoint_queue_handle = NULL;

        CdiOsCritSectionDelete(mgr_ptr->state_lock);
        mgr_ptr->state_lock = NULL;

        CdiOsCritSectionDelete(mgr_ptr->endpoint_list_lock);
        mgr_ptr->endpoint_list_lock = NULL;

        CdiOsSignalDelete(mgr_ptr->endpoints_destroyed_signal);
        mgr_ptr->endpoints_destroyed_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->command_done_signal);
        mgr_ptr->command_done_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->all_threads_waiting_signal);
        mgr_ptr->all_threads_waiting_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->poll_thread_exit_signal);
        mgr_ptr->poll_thread_exit_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->all_threads_running_signal);
        mgr_ptr->all_threads_running_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->new_command_signal);
        mgr_ptr->new_command_signal = NULL;

        CdiOsSignalDelete(mgr_ptr->shutdown_signal);
        mgr_ptr->shutdown_signal = NULL;

        CdiOsMemFree(mgr_ptr);
    }
}

EndpointManagerHandle EndpointManagerConnectionToEndpointManager(CdiConnectionHandle handle)
{
    CdiConnectionState* con_ptr = (CdiConnectionState*)handle;
    return con_ptr->endpoint_manager_handle;
}

void EndpointManagerRemoteEndpointInfoSet(CdiEndpointHandle handle, const struct sockaddr_in* remote_address_ptr,
                                          const char* stream_name_str)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;

    if (remote_address_ptr) {
        inet_ntop(AF_INET, &remote_address_ptr->sin_addr, endpoint_ptr->remote_ip_str,
                  sizeof(endpoint_ptr->remote_ip_str));
        endpoint_ptr->remote_sockaddr_in = *remote_address_ptr;
    } else {
        memset(&endpoint_ptr->remote_sockaddr_in, 0, sizeof(endpoint_ptr->remote_sockaddr_in));
    }

    if (stream_name_str) {
        CdiOsStrCpy(endpoint_ptr->stream_name_str, sizeof(endpoint_ptr->stream_name_str),
                    stream_name_str);
    } else {
        endpoint_ptr->stream_name_str[0] = '\0';
    }
}

const char* EndpointManagerEndpointStreamNameGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    if ('\0' == endpoint_ptr->stream_name_str[0]) {
        return NULL;
    }
    return endpoint_ptr->stream_name_str;
}

const char* EndpointManagerEndpointRemoteIpGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    return endpoint_ptr->remote_ip_str;
}

int EndpointManagerEndpointRemotePortGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    return ntohs(endpoint_ptr->remote_sockaddr_in.sin_port); // Convert network byte order to int
}

const struct sockaddr_in* EndpointManagerEndpointRemoteAddressGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    return &endpoint_ptr->remote_sockaddr_in;
}

void EndpointManagerQueueEndpointReset(CdiEndpointHandle handle)
{
    EndpointManagerConnectionStateChange(handle, kCdiConnectionStatusDisconnected, NULL);

    // Start the reset endpoint process.
    InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    SetCommand(internal_endpoint_ptr, kEndpointStateReset);
}

void EndpointManagerQueueEndpointStart(CdiEndpointHandle handle)
{
    // Start the start endpoint process.
    InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    SetCommand(internal_endpoint_ptr, kEndpointStateStart);
}

void EndpointManagerShutdownConnection(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    if (NULL == mgr_ptr) {
        return;
    }

    mgr_ptr->got_shutdown = true;

    CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
    bool sent_command = false;
    while (endpoint_handle) {
        InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(endpoint_handle);
        // Start the shutdown endpoint process.
        if (SetCommand(internal_endpoint_ptr, kEndpointStateShutdown)) {
            sent_command = true;
        }
        endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
    }

    // Now that shutdown command has been added to the queue for each endpoint, set shutdown flags so poll threads
    // exit their main loop and start shutting down by invoking EndpointManagerPollThreadExit().
    if (handle->connection_state_ptr->adapter_connection_ptr) {
        CdiOsSignalSet(handle->connection_state_ptr->adapter_connection_ptr->shutdown_signal);
    }

    // If threads have started and the done signal is valid, wait for all threads associated with this connection to
    // process being shutdown. If there are no registered threads then skip this check.
    //
    // NOTE: The start_signal only gets set at the end of RxCreateInternal() and TxCreateInternal() if the connection
    // has been successfully created. In the case where creation has failed, this function has already been called from
    // within those same functions, so no additional race-condition logic is required here.
    if (mgr_ptr->registered_thread_count) {
        if (mgr_ptr->connection_state_ptr->start_signal && mgr_ptr->command_done_signal &&
             CdiOsSignalGet(mgr_ptr->connection_state_ptr->start_signal) && sent_command) {
            // Ok to wait for the shutdown command to be processed.
            CdiOsSignalWait(mgr_ptr->command_done_signal, CDI_INFINITE, NULL);
        }

        if (mgr_ptr->poll_thread_exit_signal) {
            // Wait for the poll thread to exit.
            CdiOsSignalWait(mgr_ptr->poll_thread_exit_signal, CDI_INFINITE, NULL);
        }
    }

    // Destroy stats before endpoints are destroyed, so we can capture the last stats set from the endpoints.
    StatsDestroy(mgr_ptr->connection_state_ptr->stats_state_ptr);
    mgr_ptr->connection_state_ptr->stats_state_ptr = NULL;

    // Wait for internal connection thread to stop first, since some of the resources are shared by the adapter.
    if (kHandleTypeTx == mgr_ptr->connection_state_ptr->handle_type) {
        TxConnectionThreadJoin(mgr_ptr->connection_state_ptr);
    }

    // Destroy all endpoints related to this Endpoint Manager.
    CdiEndpointHandle cdi_endpoint_handle = NULL;
    while (NULL != (cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(handle))) {
        // This removes the endpoint from the list, so just keep getting the first one.
        DestroyEndpoint(cdi_endpoint_handle);
    }

    // Now that all of the endpoints have been shutdown the endpoint manager thread can also be shutdown.
    if (handle->connection_state_ptr->endpoint_manager_handle->shutdown_signal) {
        CdiOsSignalSet(handle->connection_state_ptr->endpoint_manager_handle->shutdown_signal);
    }

    CdiAdapterDestroyConnection(mgr_ptr->connection_state_ptr->adapter_connection_ptr);
}

CdiSignalType EndpointManagerThreadRegister(EndpointManagerHandle handle, const char* thread_name_str)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

    int count = CdiOsAtomicInc32(&mgr_ptr->registered_thread_count);
    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager,
                             "Endpoint Manager registered thread[%s]. Number of threads registered[%d].",
                             thread_name_str, count);
    return mgr_ptr->new_command_signal;
}

void EndpointManagerThreadWait(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

    // Block in case a previous state change has not finished. To be considered finished, all registered threads
    // must have exited this function (they are not blocked).
    CdiOsSignalWait(mgr_ptr->all_threads_running_signal, CDI_INFINITE, NULL);

    // Increment the thread wait counter.
    IncrementThreadWaitCount(mgr_ptr);

    // Block until EndpointManagerThread() has completed processing the command.
    CdiOsSignalWait(mgr_ptr->command_done_signal, CDI_INFINITE, NULL);

    // Decrement the thread wait counter. When it reaches zero, all threads have reached here and are running
    // again, so update signals to allow another command to start.
    DecrementThreadWaitCount(mgr_ptr);
}

bool EndpointManagerIsConnectionShuttingDown(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    return mgr_ptr->got_shutdown;
}

CdiSignalType EndpointManagerGetNotificationSignal(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    return mgr_ptr->new_command_signal;
}

void EndpointManagerConnectionStateChange(CdiEndpointHandle handle, CdiConnectionStatus status_code,
                                          const char* error_msg_str)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    AdapterEndpointState* adapter_endpoint_ptr = endpoint_ptr->adapter_endpoint_ptr;
    EndpointManagerState* mgr_ptr = CdiEndpointToInternalEndpoint(handle)->endpoint_manager_ptr;
    bool ignore = (status_code == adapter_endpoint_ptr->connection_status_code);

    // Only notify the application if the status code has changed.
    if (!ignore) {
        adapter_endpoint_ptr->connection_status_code = status_code;

        if (kHandleTypeRx == endpoint_ptr->connection_state_ptr->handle_type) {
            // Connection is Rx. Clear the flag indicating that a payload has been received.
            endpoint_ptr->connection_state_ptr->rx_state.received_first_payload = false;
            // If status is disconnected, notify the application if there are no connected endpoints related to the
            // connection.
            if (kCdiConnectionStatusDisconnected == status_code) {
                CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
                if (CdiListCount(&mgr_ptr->endpoint_list) > 1) {
                    ignore = true; // Other endpoints are still connected, so don't notify the application.
                }
                CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
            }
        }
    }

    if (!ignore) {
        // If connected and all other endpoints related to this connection are also connected, then set the adapter's
        // connection state to connected.
        if (kCdiConnectionStatusConnected == status_code) {
            CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
            CdiEndpointHandle found_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
            while (found_handle) {
                if (handle != found_handle &&
                    kCdiConnectionStatusConnected != found_handle->adapter_endpoint_ptr->connection_status_code) {
                    status_code = kCdiConnectionStatusDisconnected;
                    break;
                } else {
                    found_handle = EndpointManagerGetNextEndpoint(found_handle);
                }
            }
            CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
        }

        // Set connection state for the adapter's connection (all endpoints related to the connection must be connected,
        // otherwise it is not considered connected).
        adapter_endpoint_ptr->adapter_con_state_ptr->connection_status_code = status_code;
        CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager,
                                 "Notifying app of connection remote IP[%s:%d] state change[%s].",
                                 EndpointManagerEndpointRemoteIpGet(handle),
                                 EndpointManagerEndpointRemotePortGet(handle),
                                 CdiUtilityKeyEnumToString(kKeyConnectionStatus,
                                                           adapter_endpoint_ptr->connection_status_code));

        CdiCoreConnectionCbData cb_data = {
            .status_code = adapter_endpoint_ptr->connection_status_code,
            .err_msg_str = error_msg_str,
            .tx_stream_endpoint_handle = (kHandleTypeTx == endpoint_ptr->connection_state_ptr->handle_type) ?
                                          handle : NULL, // Only valid for Tx endpoints
            .remote_ip_str = EndpointManagerEndpointRemoteIpGet(handle),
            .remote_dest_port = EndpointManagerEndpointRemotePortGet(handle),
            .connection_user_cb_param = adapter_endpoint_ptr->adapter_con_state_ptr->data_state.connection_user_cb_param,
            .negotiated_version_num = 0,
            .negotiated_major_version_num = 0,
            .negotiated_probe_version_num = 0,
        };

        if (kCdiConnectionStatusDisconnected == status_code) {
            adapter_endpoint_ptr->endpoint_stats_ptr->dropped_connection_count++;
            adapter_endpoint_ptr->endpoint_stats_ptr->connected = false;
        } else {
            adapter_endpoint_ptr->endpoint_stats_ptr->connected = true;

            // Set negotiated version number information if it exists.
            if (adapter_endpoint_ptr->protocol_handle) {
                CdiProtocolVersionNumber* version_ptr = &adapter_endpoint_ptr->protocol_handle->negotiated_version;
                cb_data.negotiated_version_num = version_ptr->version_num;
                cb_data.negotiated_major_version_num = version_ptr->major_version_num;
                cb_data.negotiated_probe_version_num = version_ptr->probe_version_num;
            }
        }

        // Call the application's user registered connection function.
        (adapter_endpoint_ptr->adapter_con_state_ptr->data_state.connection_cb_ptr)(&cb_data);
    }
}

CdiReturnStatus EndpointManagerTxCreateEndpoint(EndpointManagerHandle handle, bool is_multi_stream,
                                                const char* dest_ip_addr_str, int dest_port,
                                                const char* stream_name_str,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    // Make a copy of provided stream name or copy the connection name if no stream name provided.
    char temp_stream_name_str[CDI_MAX_STREAM_NAME_STRING_LENGTH];
    const char* src_str = (stream_name_str && '\0' != stream_name_str[0])
                          ? stream_name_str : con_ptr->saved_connection_name_str;
    CdiOsStrCpy(temp_stream_name_str, sizeof(temp_stream_name_str), src_str);

    CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);

    int stream_count = CdiListCount(&mgr_ptr->endpoint_list);
    if (stream_count > CDI_MAX_ENDPOINTS_PER_CONNECTION) {
        CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogError,
                       "[%d] streams exceeds the maximum[%d] allowed in a single connection.",
                       stream_count, CDI_MAX_ENDPOINTS_PER_CONNECTION);
        rs = kCdiStatusInvalidParameter;
    }

    CdiEndpointState* endpoint_ptr = NULL;
    InternalEndpointState* internal_endpoint_ptr = NULL;
    if (kCdiStatusOk == rs && is_multi_stream) {
        // For multi-stream endpoints, if matching destination endpoint already exists then use it.
        CdiEndpointHandle found_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
        while (found_handle) {
            int found_dest_port = EndpointManagerEndpointRemotePortGet(found_handle);
            if (0 == CdiOsStrCmp(found_handle->remote_ip_str, dest_ip_addr_str) &&
                found_dest_port == dest_port) {
                endpoint_ptr = found_handle;
                internal_endpoint_ptr = CdiEndpointToInternalEndpoint(found_handle);
                CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogInfo,
                               "Using existing Tx endpoint with same remote IP[%s:%d].", dest_ip_addr_str, dest_port);
                break;
            }
            found_handle = EndpointManagerGetNextEndpoint(found_handle);
        }
    }

    if (NULL == endpoint_ptr) {
        if (kCdiStatusOk == rs) {
            rs = CreateEndpointCommonResources(mgr_ptr, &internal_endpoint_ptr);
        }

        if (kCdiStatusOk == rs) {
            endpoint_ptr = &internal_endpoint_ptr->cdi_endpoint;
            struct sockaddr_in dest_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(dest_port), // Convert int port to network byte order
                .sin_addr = { 0 },
                .sin_zero = { 0 }
            };
            inet_pton(AF_INET, dest_ip_addr_str, &dest_addr.sin_addr);
            EndpointManagerRemoteEndpointInfoSet(endpoint_ptr, &dest_addr, stream_name_str);

            if (!CdiOsCritSectionCreate(&endpoint_ptr->tx_state.payload_num_lock)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }

        if (kCdiStatusOk == rs) {
            // Open an endpoint to send packets to a remote host. Do this last since doing so will open the flood gates
            // for callbacks to begin.
            CdiAdapterEndpointConfigData config_data = {
                .connection_handle = con_ptr->adapter_connection_ptr,
                .cdi_endpoint_handle = endpoint_ptr,

                .msg_from_endpoint_func_ptr = TxPacketWorkRequestComplete,
                .msg_from_endpoint_param_ptr = endpoint_ptr,

                .remote_address_str = dest_ip_addr_str,
                .port_number = dest_port,
                .endpoint_stats_ptr = &endpoint_ptr->transfer_stats.endpoint_stats,
            };
            if (kCdiStatusOk != CdiAdapterOpenEndpoint(&config_data, &endpoint_ptr->adapter_endpoint_ptr)) {
                rs = kCdiStatusFatal;
            }
        }

        if (kCdiStatusOk == rs) {
            CdiOsSignalSet(con_ptr->start_signal); // Start connection threads.
            CdiAdapterStartEndpoint(endpoint_ptr->adapter_endpoint_ptr); // Start adapter endpoint threads.
            CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo, "Successfully created Tx remote IP[%s:%d] endpoint. Name[%s]",
                        dest_ip_addr_str, dest_port, con_ptr->saved_connection_name_str);

            // Protect multi-threaded access to the list.
            CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
            CdiListAddTail(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
            CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
        } else if (endpoint_ptr) {
            DestroyEndpoint(endpoint_ptr);
            endpoint_ptr = NULL;
            internal_endpoint_ptr = NULL; // DestroyEndpoint() frees this.
        }
    }

    if (ret_endpoint_handle_ptr) {
        *ret_endpoint_handle_ptr = endpoint_ptr;
    }

    CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);

    return rs;
}

CdiReturnStatus EndpointManagerRxCreateEndpoint(EndpointManagerHandle handle, int dest_port,
                                                const struct sockaddr_in* source_address_ptr,
                                                const char* stream_name_str,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    InternalEndpointState* internal_endpoint_ptr = NULL;
    rs = CreateEndpointCommonResources(mgr_ptr, &internal_endpoint_ptr);

    CdiEndpointState* endpoint_ptr = NULL;
    if (kCdiStatusOk == rs) {
        endpoint_ptr = &internal_endpoint_ptr->cdi_endpoint;

        // Multiple threads may use the CdiCoreRxFreeBuffer() API, which pushes items onto this queue. So, we want to
        // enable thread-safe writes when creating it by using kQueueMultipleWritersFlag.
        if (!CdiQueueCreate("RxFreeBuffer CdiSgList Queue", MAX_PAYLOADS_PER_CONNECTION, CDI_FIXED_QUEUE_SIZE,
                            CDI_FIXED_QUEUE_SIZE, sizeof(CdiSgList), kQueueSignalNone | kQueueMultipleWritersFlag,
                            &endpoint_ptr->rx_state.free_buffer_queue_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    // Since this endpoint can be created dynamically as part of a control command received from a remote transmitter,
    // we need to save the remote address before creating the adapter endpoint. The adapter endpoint's control interface
    // can start using it immediately.
    if (endpoint_ptr) {
        EndpointManagerRemoteEndpointInfoSet(endpoint_ptr, source_address_ptr, stream_name_str);
    }

    if (kCdiStatusOk == rs) {
        // Open an endpoint to receive packets from a remote host.
        CdiAdapterEndpointConfigData config_data = {
            .connection_handle = con_ptr->adapter_connection_ptr,
            .cdi_endpoint_handle = endpoint_ptr,

            .msg_from_endpoint_func_ptr = RxPacketReceive,
            .msg_from_endpoint_param_ptr = endpoint_ptr,

            .port_number = dest_port,
            .endpoint_stats_ptr = &endpoint_ptr->transfer_stats.endpoint_stats,
        };
        if (kCdiStatusOk != CdiAdapterOpenEndpoint(&config_data, &endpoint_ptr->adapter_endpoint_ptr)) {
            rs = kCdiStatusFatal;
        }
    }

    if (kCdiStatusOk == rs) {
        CdiOsSignalSet(con_ptr->start_signal); // Start connection threads.
        CdiAdapterStartEndpoint(endpoint_ptr->adapter_endpoint_ptr);   // Start adapter endpoint threads.
        CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo,
                       "Successfully created Rx stream endpoint. Listen port[%d] Name[%s]",
                       dest_port, con_ptr->saved_connection_name_str);

        // Protect multi-threaded access to the list.
        CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
        CdiListAddTail(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
        CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
    } else if (endpoint_ptr) {
        DestroyEndpoint(endpoint_ptr);
        endpoint_ptr = NULL;
        internal_endpoint_ptr = NULL; // DestroyEndpoint() frees this
    }

    if (ret_endpoint_handle_ptr) {
        *ret_endpoint_handle_ptr = endpoint_ptr;
    }

    return rs;
}

CdiReturnStatus EndpointManagerProtocolVersionSet(CdiEndpointHandle handle,
                                                  const CdiProtocolVersionNumber* remote_version_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (handle->adapter_endpoint_ptr->protocol_handle) {
        ProtocolVersionDestroy(handle->adapter_endpoint_ptr->protocol_handle);
        handle->adapter_endpoint_ptr->protocol_handle = NULL;
    }

    ProtocolVersionSet(remote_version_ptr, &handle->adapter_endpoint_ptr->protocol_handle);
    if (kHandleTypeRx == handle->connection_state_ptr->handle_type) {
        rs = RxEndpointCreateDynamicPools(handle);
    }

    return rs;
}

void EndpointManagerEndpointDestroy(CdiEndpointHandle handle)
{
    EndpointManagerState* mgr_ptr = CdiEndpointToInternalEndpoint(handle)->endpoint_manager_ptr;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    // Protect access to the list, since multiple threads may call this function.
    CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
    // Walk through the endpoint list, ensuring that it has not already been queued to be destroyed.
    bool destroy = false;
    CdiEndpointHandle found_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
    while (found_handle) {
        if (handle == found_handle) {
            InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(found_handle);
            if (!internal_endpoint_ptr->queued_to_destroy) {
                internal_endpoint_ptr->queued_to_destroy = true;
                destroy = true;
            }
            break;
        }
        found_handle = EndpointManagerGetNextEndpoint(found_handle);
    }
    CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);

    if (destroy) {
        CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo, "Destroy endpoint remote IP[%s:%d].",
                       EndpointManagerEndpointRemoteIpGet(handle),
                       EndpointManagerEndpointRemotePortGet(handle));

        EndpointManagerConnectionStateChange(handle, kCdiConnectionStatusDisconnected, NULL);

        CdiOsSignalClear(mgr_ptr->endpoints_destroyed_signal);
        if (!CdiQueuePush(mgr_ptr->destroy_endpoint_queue_handle, &handle)) {
            CDI_LOG_THREAD(kLogInfo, "Destroy endpoint queue[%s] full.",
                           CdiQueueGetName(mgr_ptr->destroy_endpoint_queue_handle));
        }

        CdiSignalType signal_array[3];
        signal_array[0] = mgr_ptr->endpoints_destroyed_signal;
        signal_array[1] = mgr_ptr->connection_state_ptr->shutdown_signal;
        signal_array[2] = handle->adapter_endpoint_ptr->shutdown_signal;

        // Wait until endpoint gets destroyed by waiting for endpoints_destroyed_signal and then checking the list of
        // endpoints to ensure it has been removed. If not, wait again.
        bool found = true;
        while (found) {
            // Make sure the poll thread isn't sleeping. We need it to call EndpointManagerPoll, which in turn destroys
            // the endpoint for us.
            CdiOsSignalSet(EndpointManagerGetNotificationSignal(mgr_ptr));
            uint32_t signal_index;
            CdiOsSignalsWait(signal_array, 3, false, CDI_INFINITE, &signal_index);
            if (0 == signal_index) {
                found = false;
                CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
                found_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
                while (found_handle) {
                    if (found_handle == handle) {
                        found = true;
                        break;
                    } else {
                        found_handle = EndpointManagerGetNextEndpoint(found_handle);
                    }
                }
                CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
            } else {
                // Got shutdown signal.
                break;
            }
        }
    }
}

bool EndpointManagerIsEndpoint(EndpointManagerHandle handle, CdiEndpointHandle endpoint_handle)
{
    bool ret = false;

    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    if (mgr_ptr) {
        CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);

        int count = CdiListCount(&mgr_ptr->endpoint_list);
        InternalEndpointState* endpoint_ptr = (InternalEndpointState*)CdiListPeek(&mgr_ptr->endpoint_list);

        InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(endpoint_handle);
        for (int i = 0; i < count; i++) {
            if (endpoint_ptr == internal_endpoint_ptr) {
                ret = true; // Found it.
                break;
            }
            endpoint_ptr = (InternalEndpointState*)endpoint_ptr->list_entry.next_ptr;
        }
        CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
    }

    return ret;
}

CdiEndpointHandle EndpointManagerGetFirstEndpoint(EndpointManagerHandle handle)
{
    CdiEndpointHandle endpoint_handle = NULL;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    if (mgr_ptr) {
        InternalEndpointState* endpoint_ptr = (InternalEndpointState*)CdiListPeek(&mgr_ptr->endpoint_list);
        if (endpoint_ptr) {
            endpoint_handle = &endpoint_ptr->cdi_endpoint;
        }
    }
    return endpoint_handle;
}

CdiEndpointHandle EndpointManagerGetNextEndpoint(CdiEndpointHandle handle)
{
    InternalEndpointState* endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    EndpointManagerState* mgr_ptr = endpoint_ptr->endpoint_manager_ptr;
    CdiEndpointHandle cdi_endpoint_handle = NULL;

    endpoint_ptr = (InternalEndpointState*)endpoint_ptr->list_entry.next_ptr;
    if (endpoint_ptr != (InternalEndpointState*)CdiListGetHead(&mgr_ptr->endpoint_list)) {
        cdi_endpoint_handle = &endpoint_ptr->cdi_endpoint;
    }

    return cdi_endpoint_handle;
}

AdapterEndpointHandle EndpointManagerEndpointToAdapterEndpoint(CdiEndpointHandle handle)
{
    return handle->adapter_endpoint_ptr;
}

int EndpointManagerEndpointGetCount(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

    CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
    int count = CdiListCount(&mgr_ptr->endpoint_list);
    CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);

    return count;
}

bool EndpointManagerPoll(CdiEndpointHandle* handle_ptr)
{
    bool do_poll = true;
    bool get_first = false;

    CdiEndpointHandle handle = *handle_ptr;
    EndpointManagerState* mgr_ptr = handle->connection_state_ptr->endpoint_manager_handle;

    // Don't destroy endpoints while waiting for commands to be done
    // executing, as we may never poll out of that state otherwise if the
    // endpoint we destroyed was the one processing the command.
    if (!mgr_ptr->poll_thread_waiting) {
        // If any endpoints need to be destroyed, do so now.
        bool destroyed = false;
        CdiEndpointHandle destroy_endpoint_handle = NULL;
        while (CdiQueuePop(mgr_ptr->destroy_endpoint_queue_handle, &destroy_endpoint_handle)) {
            // Destroy the endpoint.
            if (handle == destroy_endpoint_handle) {
                do_poll = false; // Endpoint is being destroyed, so don't use it anymore.
                get_first = true;
            }
            DestroyEndpoint(destroy_endpoint_handle);
            destroyed = true;
        }
        if (destroyed) {
            CdiOsSignalSet(mgr_ptr->endpoints_destroyed_signal);
        }
    }

    if (do_poll && mgr_ptr->thread_done) {
        // Endpoint Manager thread is done. If poll thread was waiting, decrement thread wait count and clear flag.
        if (mgr_ptr->poll_thread_waiting) {
            DecrementThreadWaitCount(mgr_ptr);
            mgr_ptr->poll_thread_waiting = false;
        }
        do_poll = false;
    }

    if (do_poll) {
        // Determine if this endpoint is processing a state change command and needs to have polling paused.
        InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
        if (CdiOsSignalReadState(mgr_ptr->new_command_signal) || mgr_ptr->poll_thread_waiting) {
            if (internal_endpoint_ptr->got_new_command) {
                do_poll = false;
            }
            if (!mgr_ptr->poll_thread_waiting) {
                mgr_ptr->poll_thread_waiting = true;
                IncrementThreadWaitCount(mgr_ptr);
                // Now that we have incremented the thread wait count, the Endpoint Manager could try to process the
                // pending command now, so don't let the poll thread do any work yet.
                do_poll = false;
            } else if (CdiOsSignalReadState(mgr_ptr->command_done_signal)) {
                DecrementThreadWaitCount(mgr_ptr);
                // Even though this is a poll thread where we don't want to use OS resources, we need to use a critical
                // section here to synchronize an empty queue condition and the got_new_command variable. This logic
                // only executes while the connection state of an endpoint is changing.
                CdiOsCritSectionReserve(mgr_ptr->state_lock);
                if (CdiQueueIsEmpty(internal_endpoint_ptr->command_queue_handle)) {
                    internal_endpoint_ptr->got_new_command = false;
                    do_poll = true;
                }
                CdiOsCritSectionRelease(mgr_ptr->state_lock);
                mgr_ptr->poll_thread_waiting = false;
            }
        }
        *handle_ptr = EndpointManagerGetNextEndpoint(handle);
    } else {
        if (get_first) {
            *handle_ptr = EndpointManagerGetFirstEndpoint(mgr_ptr);
        } else {
            *handle_ptr = EndpointManagerGetNextEndpoint(handle);
        }
    }

    return do_poll;
}

bool EndpointManagerPollThreadExit(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

    // Walk through each endpoint.
    CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
    while (endpoint_handle) {
        EndpointManagerPoll(&endpoint_handle);
    }

    bool done = !mgr_ptr->poll_thread_waiting; // Done when poll thread is no longer in wait state.
    if (done) {
        CdiOsSignalSet(mgr_ptr->poll_thread_exit_signal);
    }

    return done;
}
