// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
 *    #EndpointManagerState::new_command_signal is set. The Polling thread must call EndpointManagerPoll() as part of
 *    its normal poll loop to determine if it should perform adapter level polling or not. All other registered threads
 *    must monitor this signal and when set, must call EndpointManagerThreadWait(), which blocks the thread.
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

#include "adapter_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "internal.h"
#include "internal_rx.h"
#include "internal_tx.h"
#include "internal_utility.h"
#include "statistics.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This defines a structure that contains all the the state information for endpoint state changes.
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
 * @brief This defines a structure that contains all the the state information for endpoint state changes.
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

    CdiThreadID thread_id;             ///< Endpoint state thread identifier

    /// @brief Lock used to protect access to endpoint state.
    CdiCsID state_lock;

    CdiSignalType new_command_signal;  ///< Signal used to start processing a command.
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
    int registered_thread_count; ///< Numober of registered threads associated with this endpoint.
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
 */
static void SetCommand(InternalEndpointState* internal_endpoint_ptr, EndpointManagerCommand command)
{
    EndpointManagerState* mgr_ptr = internal_endpoint_ptr->endpoint_manager_ptr;

    CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentEndpointManager, "Endpoint Manager stream ID[%d] got command[%s]",
                             internal_endpoint_ptr->cdi_endpoint.stream_identifier,
                             InternalUtilityKeyEnumToString(kKeyEndpointManagerCommand, command));

    // Prevent the signals/variables used in this block from being accessed by other threads.
    CdiOsCritSectionReserve(mgr_ptr->state_lock);

    // Ignore all new commands if we got a shutdown command.
    if (!internal_endpoint_ptr->got_shutdown) {
        internal_endpoint_ptr->got_new_command = true;
        if (kEndpointStateShutdown == command) {
            internal_endpoint_ptr->got_shutdown = true;
        }
        if (!CdiQueuePush(internal_endpoint_ptr->command_queue_handle, &command)) {
            CDI_LOG_THREAD(kLogInfo, "Add endpoint command queue[%s] full.",
                           CdiQueueGetName(internal_endpoint_ptr->command_queue_handle));
            internal_endpoint_ptr->got_new_command = false;
        } else {
            CdiOsSignalSet(mgr_ptr->new_command_signal);
        }
    }

    CdiOsCritSectionRelease(mgr_ptr->state_lock);
}

/**
 * Flush resources associated with the specified connection.
 *
 * @param endpoint_ptr Pointer to endpoint to free resources.
 */
static void FlushResources(InternalEndpointState* endpoint_ptr)
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
    CdiAdapterResetEndpoint(endpoint_ptr->cdi_endpoint.adapter_endpoint_ptr);
}

/**
 * Destroy an endpoint, closing its adapter endpoint and freeing resources used by it.
 *
 * @param handle Handle of endpoint to destroy.
 */
static void DestroyEndpoint(CdiEndpointHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle->connection_state_ptr->endpoint_manager_handle;

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
            InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(list_endpoint_handle);
            // Must protect access to the list when removing an entry.
            CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
            CdiListRemove(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
            CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
            break;
        }
        list_endpoint_handle = EndpointManagerGetNextEndpoint(list_endpoint_handle);
    }

    InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(handle);
    CdiQueueDestroy(internal_endpoint_ptr->command_queue_handle);
    CdiOsMemFree(internal_endpoint_ptr);
}

/**
 * Thread used to manage endpoint reset, start and shutdown events.
 *
 * @param ptr Pointer to Endpoint Manager state data.
 *
 * @return The return value is not used.
 */
static THREAD EndpointManagerThread(void* ptr)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)ptr;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(mgr_ptr->connection_state_ptr->log_handle);

    bool keep_alive = true;
    while (!CdiOsSignalGet(mgr_ptr->connection_state_ptr->shutdown_signal) && keep_alive) {
        // Wait for all registered threads to be waiting.
        CdiOsSignalWait(mgr_ptr->all_threads_waiting_signal, CDI_INFINITE, NULL);
        CdiOsSignalClear(mgr_ptr->all_threads_waiting_signal);

        // Walk through the list of endpoints associated with this Endpoint Manager and process commands in the
        // endpoint's queue.
        CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
        while (endpoint_handle) {
            InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(endpoint_handle);
            EndpointManagerCommand command;
            while (internal_endpoint_ptr && CdiQueuePop(internal_endpoint_ptr->command_queue_handle, &command)) {
                CDI_LOG_THREAD_COMPONENT(kLogDebug, kLogComponentProbe,
                                         "Endpoint Manager stream ID[%d] processing command[%s]",
                                         internal_endpoint_ptr->cdi_endpoint.stream_identifier,
                                         InternalUtilityKeyEnumToString(kKeyEndpointManagerCommand, command));
                switch (command) {
                    case kEndpointStateIdle:
                        // Nothing special to do.
                        break;
                    case kEndpointStateReset:
                        FlushResources(internal_endpoint_ptr);
                        break;
                    case kEndpointStateStart:
                        CdiAdapterStartEndpoint(endpoint_handle->adapter_endpoint_ptr);
                        break;
                    case kEndpointStateShutdown:
                        FlushResources(internal_endpoint_ptr);
                        keep_alive = false;
                        break;
                }
            }
            endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
        }
        // Commands have completed. Set signal to unblock registered connection threads that are blocked in
        // EndpointManagerThreadWait() so they can continue running.
        CdiOsSignalSet(mgr_ptr->command_done_signal);
    }

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
        CdiOsSignalClear(mgr_ptr->new_command_signal);
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
        if (!mgr_ptr->got_shutdown) {
            CdiOsSignalClear(mgr_ptr->command_done_signal);
        }
        CdiOsSignalSet(mgr_ptr->all_threads_running_signal);
    }
}

/**
 * Create resources common to both Tx and Rx endpoints.
 *
 * @param mgr_ptr Pointer to Endpoint Manager.
 * @param stream_identifier Stream identifier.
 * @param ret_internal_endpoint_ptr Address where to write returned endpoint handle.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus CreateEndpointCommonResources(EndpointManagerState* mgr_ptr, int stream_identifier,
                                                     InternalEndpointState** ret_internal_endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    InternalEndpointState* internal_endpoint_ptr = NULL;

    if (CdiListCount(&mgr_ptr->endpoint_list) >= MAX_ENDPOINTS_PER_CONNECTION) {
        CDI_LOG_THREAD(kLogError,
            "Failed to create stream ID[%d] endpoint. Already at the maximum[%d] allowed in a single connection.",
            stream_identifier, MAX_ENDPOINTS_PER_CONNECTION);
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
        internal_endpoint_ptr->cdi_endpoint.stream_identifier = stream_identifier;
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

    if (!CdiOsSignalCreate(&mgr_ptr->new_command_signal)) {
        rs = kCdiStatusNotEnoughMemory;
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
        if (!CdiQueueCreate("DestroyEndpoint Queue", MAX_ENDPOINTS_PER_CONNECTION, NO_GROW_SIZE, NO_GROW_COUNT,
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

        CdiOsMemFree(mgr_ptr);
    }
}

EndpointManagerHandle EndpointManagerConnectionToEndpointManager(CdiConnectionHandle handle)
{
    CdiConnectionState* con_ptr = (CdiConnectionState*)handle;
    return con_ptr->endpoint_manager_handle;
}

void EndpointManagerEndpointInfoSet(CdiEndpointHandle handle, int remote_stream_identifier,
                                    const char* remote_stream_name_str)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;

    if (remote_stream_name_str) {
        CdiOsStrCpy(endpoint_ptr->stream_name_str, sizeof(endpoint_ptr->stream_name_str),
                    remote_stream_name_str);
    } else {
        endpoint_ptr->stream_name_str[0] = '\0';
    }

    endpoint_ptr->stream_identifier = remote_stream_identifier;
}

const char* EndpointManagerEndpointStreamNameGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    if ('\0' == endpoint_ptr->stream_name_str[0]) {
        return NULL;
    }
    return endpoint_ptr->stream_name_str;
}

int EndpointManagerEndpointStreamIdGet(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;
    return endpoint_ptr->stream_identifier;
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

    CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
    while (endpoint_handle) {
        InternalEndpointState* internal_endpoint_ptr = CdiEndpointToInternalEndpoint(endpoint_handle);
        // Start the shutdown endpoint process.
        SetCommand(internal_endpoint_ptr, kEndpointStateShutdown);
        endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
    }

    // Now that shutdown command has been added to the queue for each endpoint, set shutdown flags so pull thread's
    // exit their main loop and start shutting down by invoking EndpointManagerPollThreadExit().
    mgr_ptr->got_shutdown = true;
    CdiOsSignalSet(handle->connection_state_ptr->adapter_connection_ptr->shutdown_signal);

    // If threads have started and the done signal is valid, wait for all threads associated with this connection to
    // process being shutdown.
    //
    // NOTE: The start_signal only gets set at the end of RxCreateInternal() and TxCreateInternal() if the connection
    // has been succesfully created. In the case where creation has failed, this function has already been called from
    // within those same functions, so no additional race-condition logic is required here.
    if (mgr_ptr->connection_state_ptr->start_signal && mgr_ptr->command_done_signal &&
        CdiOsSignalGet(mgr_ptr->connection_state_ptr->start_signal)) {
        // Ok to wait for the shutdown command to be processed.
        CdiOsSignalWait(mgr_ptr->command_done_signal, CDI_INFINITE, NULL);
    }

    if (mgr_ptr->poll_thread_exit_signal) {
        // Wait for the poll thread to exit.
        CdiOsSignalWait(mgr_ptr->poll_thread_exit_signal, CDI_INFINITE, NULL);
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

    // Only notify the application if the status code has changed or there is an error message.
    if ((status_code != adapter_endpoint_ptr->connection_status_code) || error_msg_str) {
        adapter_endpoint_ptr->connection_status_code = status_code;

        // If connected and all other endpoints related to this connection are also connected, then set the adapter's
        // connection state to connected.
        if (kCdiConnectionStatusConnected == status_code) {
            EndpointManagerState* mgr_ptr = CdiEndpointToInternalEndpoint(handle)->endpoint_manager_ptr;
            CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
            CdiEndpointHandle found_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
            while (found_handle) {
                if (kCdiConnectionStatusConnected != found_handle->adapter_endpoint_ptr->connection_status_code) {
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
                                 "Notifying app of connection stream ID[%d] state change[%s].",
                                 handle->stream_identifier,
                                 CdiUtilityKeyEnumToString(kKeyConnectionStatus, status_code));

        CdiCoreConnectionCbData cb_data = {
            .status_code = status_code,
            .err_msg_str = error_msg_str,
            .stream_identifier = handle->stream_identifier,
            .endpoint_handle = handle,
            .connection_user_cb_param = adapter_endpoint_ptr->adapter_con_state_ptr->data_state.connection_user_cb_param
        };

        if (kCdiConnectionStatusDisconnected == status_code) {
            adapter_endpoint_ptr->endpoint_stats_ptr->dropped_connection_count++;
            adapter_endpoint_ptr->endpoint_stats_ptr->connected = false;
        } else {
            adapter_endpoint_ptr->endpoint_stats_ptr->connected = true;
        }

        // Call the application's user registered connection function.
        (adapter_endpoint_ptr->adapter_con_state_ptr->data_state.connection_cb_ptr)(&cb_data);
    }
}

CdiReturnStatus EndpointManagerTxCreateEndpoint(EndpointManagerHandle handle, int stream_identifier,
                                                const char* dest_ip_addr_str, int dest_port,
                                                const char* stream_name_str,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);

    int stream_count = CdiListCount(&mgr_ptr->endpoint_list);
    if (stream_count > MAX_ENDPOINTS_PER_CONNECTION) {
        CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogError,
                       "[%d] streams exceeds the maximum[%d] allowed in a single connection.",
                       stream_count, MAX_ENDPOINTS_PER_CONNECTION);
        rs = kCdiStatusInvalidParameter;
    }

    InternalEndpointState* internal_endpoint_ptr = NULL;
    if (kCdiStatusOk == rs) {
        rs = CreateEndpointCommonResources(mgr_ptr, stream_identifier, &internal_endpoint_ptr);
    }

    CdiEndpointState* endpoint_ptr = NULL;
    if (kCdiStatusOk == rs) {
        endpoint_ptr = &internal_endpoint_ptr->cdi_endpoint;
    }

    if (kCdiStatusOk == rs) {
        if (!CdiOsCritSectionCreate(&endpoint_ptr->tx_state.payload_num_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        endpoint_ptr->stream_identifier = stream_identifier;

        if (NULL == stream_name_str || '\0' == stream_name_str[0]) {
            // Stream name was not provided, so generate one.
            snprintf(endpoint_ptr->stream_name_str, sizeof(endpoint_ptr->stream_name_str), "%s:%d",
                    con_ptr->saved_connection_name_str, stream_identifier);
        } else {
            // Make a copy of provided stream name.
            CdiOsStrCpy(endpoint_ptr->stream_name_str, sizeof(endpoint_ptr->stream_name_str), stream_name_str);
        }
    }

    if (kCdiStatusOk == rs) {
        // Open an endpoint to send packets to a remote host. Do this last since doing so will open the flood gates for
        // callbacks to begin.
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
        CdiAdapterStartEndpoint(endpoint_ptr->adapter_endpoint_ptr);   // Start adapter endpoint threads.
        CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo, "Successfully created Tx stream ID[%d] endpoint. Name[%s]",
                       stream_identifier, con_ptr->saved_connection_name_str);

        // Protect multi-threaded access to the list.
        CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
        CdiListAddTail(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
        CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
    } else {
        DestroyEndpoint(endpoint_ptr);
        endpoint_ptr = NULL;
        internal_endpoint_ptr = NULL; // DestroyEndpoint() frees this
    }

    if (ret_endpoint_handle_ptr) {
        *ret_endpoint_handle_ptr = endpoint_ptr;
    }

    CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);

    return rs;
}

CdiReturnStatus EndpointManagerRxCreateEndpoint(EndpointManagerHandle handle, int dest_port,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    InternalEndpointState* internal_endpoint_ptr = NULL;
    rs = CreateEndpointCommonResources(mgr_ptr, STREAM_IDENTIFIER_NOT_USED, &internal_endpoint_ptr);

    CdiEndpointState* endpoint_ptr = NULL;
    if (kCdiStatusOk == rs) {
        endpoint_ptr = &internal_endpoint_ptr->cdi_endpoint;

        if (!CdiQueueCreate("RxFreeBuffer CdiSgList Queue", MAX_PAYLOADS_PER_CONNECTION, FIXED_QUEUE_SIZE,
                            FIXED_QUEUE_SIZE, sizeof(CdiSgList), kQueueSignalNone,
                            &endpoint_ptr->rx_state.free_buffer_queue_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
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
        CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo, "Successfully created Rx stream endpoint. Name[%s]",
                       con_ptr->saved_connection_name_str);

        // Protect multi-threaded access to the list.
        CdiOsCritSectionReserve(mgr_ptr->endpoint_list_lock);
        CdiListAddTail(&mgr_ptr->endpoint_list, &internal_endpoint_ptr->list_entry);
        CdiOsCritSectionRelease(mgr_ptr->endpoint_list_lock);
    } else {
        DestroyEndpoint(endpoint_ptr);
        endpoint_ptr = NULL;
        internal_endpoint_ptr = NULL; // DestroyEndpoint() frees this
    }

    if (ret_endpoint_handle_ptr) {
        *ret_endpoint_handle_ptr = endpoint_ptr;
    }

    return rs;
}

void EndpointManagerEndpointDestroy(CdiEndpointHandle handle)
{
    EndpointManagerState* mgr_ptr = CdiEndpointToInternalEndpoint(handle)->endpoint_manager_ptr;
    CdiConnectionState* con_ptr = mgr_ptr->connection_state_ptr;

    // Protect access to the list, since mulitple threads may call this function.
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
        CDI_LOG_HANDLE(con_ptr->log_handle, kLogInfo, "Destroy endpoint stream ID[%d].",
                       handle->stream_identifier);

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
            } else if (CdiOsSignalReadState(mgr_ptr->command_done_signal)) {
                DecrementThreadWaitCount(mgr_ptr);
                // Even though this is a poll thread where we don't want to use OS resources, we need to use a critical
                // section here to synchronize an empty queue condition and the got_new_command variable. This logic only
                // executes while the connection state of an endpoint is changing.
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

void EndpointManagerPollThreadExit(EndpointManagerHandle handle)
{
    EndpointManagerState* mgr_ptr = (EndpointManagerState*)handle;

    // Stay in this loop until all pending Endpoint Manager commands are processed for all endpoints related to this
    // Endpoint Manager.
    bool do_poll = true;
    while (do_poll) {
        CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_ptr);
        // Walk through each endpoint.
        while (endpoint_handle) {
            EndpointManagerPoll(&endpoint_handle);
        }
        do_poll = mgr_ptr->poll_thread_waiting;
    }

    CdiOsSignalSet(mgr_ptr->poll_thread_exit_signal);
    CdiOsSignalWait(mgr_ptr->command_done_signal, CDI_INFINITE, NULL);
}
