// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of common functions that comprise the CDI adapter API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_api.h"

#include "endpoint_manager.h"
#include "internal.h"
#include "internal_log.h"
#include "internal_tx.h"
#include "internal_rx.h"
#include "internal_utility.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used to hold thread utilization state data.
 */
typedef struct {
    uint64_t top_time;          ///< Time at start of each poll loop.
    uint64_t busy_accumulator;  ///< Number of productive microseconds accumulated over an averaging period.
    uint64_t idle_accumulator;  ///< Number of idle microseconds accumulated over an averaging period.
    uint64_t start_time;        ///< Time to use for start of each averaging period.
} ThreadUtilizationState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Gets the next packet to transmit. Packets arrive as lists from a queue. When a list is removed from the queue, it is
 * stored in the endpoint state and the packets are removed from the list until it's empty. The function will block
 * until a packet is available if notification_signal is not NULL otherwise it will return NULL immediately instead of
 * the address of a packet.
 *
 * @param endpoint_ptr Pointer to the endpoint's state.
 * @param notification_signal The signal used to cancel waiting to pop a packet list from the incoming queue. Specify
 *                            NULL if the adapter is a polling type adapter in which case NULL will be returned when no
 *                            packets are waiting.
 * @param last_packet_ret_ptr Pointer to a bool which is set to true upon last packet currently available. This is only
 *                            modified in the case where NULL is not returned.
 *
 * @return The address of the Packet structure dequeued or NULL if no packet was waiting.
 */
static Packet* GetNextPacket(AdapterEndpointState* endpoint_ptr, CdiSignalType notification_signal,
                             bool* last_packet_ret_ptr)
{
    Packet* packet_ptr = NULL;

    // Check packet waiting list and if it's empty try to get some from the queue.
    if (CdiSinglyLinkedListIsEmpty(&endpoint_ptr->tx_packet_waiting_list)) {
        if (NULL != notification_signal) {
            CdiQueuePopWait(endpoint_ptr->tx_packet_queue_handle, CDI_INFINITE, notification_signal,
                            &endpoint_ptr->tx_packet_waiting_list);
        } else {
            CdiQueuePop(endpoint_ptr->tx_packet_queue_handle, &endpoint_ptr->tx_packet_waiting_list);
        }
    }

    // If the packet waiting list has anything in it, return the one at the head.
    CdiSinglyLinkedListEntry* entry_ptr = CdiSinglyLinkedListPopHead(&endpoint_ptr->tx_packet_waiting_list);
    if (NULL != entry_ptr) {
        packet_ptr = CONTAINER_OF(entry_ptr, Packet, list_entry);
        *last_packet_ret_ptr = CdiSinglyLinkedListIsEmpty(&endpoint_ptr->tx_packet_waiting_list);
    }

    return packet_ptr;
}

/**
 * Peform poll process for Tx endpoint. This Tx function may block as decribed here: If the Tx packet queue is empty,
 * the do work signal is not set, and there are no Tx packets in process (not waiting for any completion events), then
 * sleep until the queue gets an entry or the notification/abort signal gets set.
 *
 * @param endpoint_state_ptr Pointer to adapter endpoint state data.
 * @param is_poll If true, endpoint requires polling.
 * @param notification_signal Notification signal.
 *
 * @return true if useful work was done, false if the function did nothing productive.
 */
static bool TxPollProcess(AdapterEndpointState* endpoint_state_ptr, bool is_poll, CdiSignalType notification_signal)
{
    bool productive = false;
    Packet* packet_ptr = NULL;
    bool last_packet = false;
    AdapterConnectionState* adapter_con_state_ptr = endpoint_state_ptr->adapter_con_state_ptr;

    EndpointTransmitQueueLevel queue_level = CdiAdapterGetTransmitQueueLevel(endpoint_state_ptr);
    bool got_packet = (kEndpointTransmitQueueFull != queue_level) &&
        (NULL != (packet_ptr = GetNextPacket(endpoint_state_ptr, NULL, &last_packet)));
    if (!got_packet && !CdiOsSignalReadState(adapter_con_state_ptr->poll_do_work_signal) &&
        (kEndpointTransmitQueueEmpty == queue_level || kEndpointTransmitQueueNa == queue_level)) {
        if (is_poll) {
            // Adapter requires polling, so the queue has been configured not to use any signals. Must use a separate
            // signal to wait on, which is normally set/cleared at the "payload" level.

            // Setup an array of signals to wait on.
            CdiSignalType signal_array[2];
            signal_array[0] = adapter_con_state_ptr->poll_do_work_signal;
            signal_array[1] = notification_signal;
            CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, NULL);
            // NOTE: No need to pull anything off the queue here, since it is most likely still empty. Can just pop it
            // next time through this function.
        } else {
            // Adapter does not require polling, so the queue has been configured to support the pop wait signal (can
            // use the queue wait API function). This greatly simplifies the logic for threads pushing data into the
            // queue, since the signaling is built in to the queue.
            got_packet = NULL != (packet_ptr = GetNextPacket(endpoint_state_ptr, notification_signal, &last_packet));
        }
    }

    if (got_packet) {
        productive = true;

        // Use the adapter to send the packet.
        adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Send(endpoint_state_ptr, packet_ptr, last_packet);
        // NOTE: No need to generate any error or warnings here since, the Send() will normally fail if the receiver is
        // not connected (ie. during probe).
    }

    return productive;
}

/**
 * Update thread utilization statistics.
 *
 * @param endpoint_stats_ptr Pointer to adapter endpoint statistics data.
 * @param idle If true thread was idle, otherwise thread was utilized.
 * @param utilization_ptr Pointer to thread utilization data to update.
 */
static void UpdateThreadUtilizationStats(CdiAdapterEndpointStats* endpoint_stats_ptr, bool idle,
                                         ThreadUtilizationState* utilization_ptr)
{
    const unsigned int microseconds_per_period = 5 * 1000 * 1000;

    // Check if busy/idle state is different this time compared to last.
    if (endpoint_stats_ptr) {
        const uint64_t now = CdiOsGetMicroseconds();
        if (idle) {
            // was idle, now busy
            utilization_ptr->idle_accumulator += now - utilization_ptr->top_time;
        } else {
            // was busy, now idle
            utilization_ptr->busy_accumulator += now - utilization_ptr->top_time;
        }

        // Update the load average over a period. It's a snapshot of the utilization during the past 5 seconds, not
        // a running average.
        if (now - utilization_ptr->start_time > microseconds_per_period) {
            const uint64_t total_time = utilization_ptr->busy_accumulator + utilization_ptr->idle_accumulator;
            if (total_time == 0 || total_time > microseconds_per_period) {
                endpoint_stats_ptr->poll_thread_load = -1;  // indicates an error
            } else {
                endpoint_stats_ptr->poll_thread_load = utilization_ptr->busy_accumulator * 10000 / total_time;
            }

            // New period starts now.
            utilization_ptr->busy_accumulator = 0;
            utilization_ptr->idle_accumulator = 0;
            utilization_ptr->start_time = now;
        }
    }
}

/**
 * Peform poll process for Rx endpoint. This Rx function may block as described here: If the Adapter does not require
 * polling and the do work signal is not set (there are no resources to free) then sleep until the do work or
 * notifcation/abort signal gets set.
 *
 * @param endpoint_state_ptr Pointer to adapter endpoint state data.
 * @param is_poll If true, endpoint requires polling.
 * @param notification_signal Notification signal.
 *
 * @return true if useful work was done, false if the function did nothing productive.
 */
static bool RxPollProcess(AdapterEndpointState* endpoint_state_ptr, bool is_poll, CdiSignalType notification_signal)
{
    bool productive = false;
    AdapterConnectionState* adapter_con_state_ptr = endpoint_state_ptr->adapter_con_state_ptr;

    if (!is_poll && !CdiOsSignalReadState(adapter_con_state_ptr->poll_do_work_signal)) {
        // Adapter does not require polling and the do work signal is not set, so wait here. The signal is normally set
        // at the "payload" level.
        CdiSignalType signal_array[2];
        signal_array[0] = adapter_con_state_ptr->poll_do_work_signal;
        signal_array[1] = notification_signal;
        uint32_t signal_index = 0;
        CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, &signal_index);
        if (0 == signal_index) {
            // Got the do work signal, clear it since we are going to free all buffers in the free buffer queue below.
            CdiOsSignalClear(adapter_con_state_ptr->poll_do_work_signal);
        }
    }

    // Free resources, if required. Probe manages freeing resources in ProbeControlProcessPacket(), so this logic is not
    // used.
    CdiSgList sgl_packet_buffers;
    if (RxPollFreeBuffer(endpoint_state_ptr->cdi_endpoint_handle, &sgl_packet_buffers)) {
        productive = true;
        // Free adapter Rx packet buffer resources.
        CdiAdapterFreeBuffer(endpoint_state_ptr, &sgl_packet_buffers);
    }

    return productive;
}

/**
 * Used directly by PollThread() to process polling for a control interface endpoint.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection state data.
 */
static void ControlInterfacePoll(AdapterConnectionState* adapter_con_state_ptr)
{
    bool is_transmitter = (kEndpointDirectionSend == adapter_con_state_ptr->direction);
    bool is_poll = (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll);

    AdapterEndpointState* adapter_endpoint_ptr = adapter_con_state_ptr->control_state.control_endpoint_handle;
    CdiSignalType notification_signal = adapter_con_state_ptr->shutdown_signal;

    // These variables are used for computing the CPU utilization of this thread.
    CdiAdapterEndpointStats* endpoint_stats_ptr = adapter_endpoint_ptr->endpoint_stats_ptr;
    ThreadUtilizationState load_state = {
        .top_time = 0,
        .start_time = CdiOsGetMicroseconds(),
        .idle_accumulator = 0,
        .busy_accumulator = 0
    };

    // The control interface does not use the Endpoint Manager and must rely on the shutdown_signal. For other endpoint
    // types that use the Endpoint Manager, they will use the EndpointManagerThreadWait() below and be blocked. As part
    // of the Endpoint Manager's shutdown sequence, the shutdown_signal gets set, so when this thread starts running
    // again, it will exit.
    while (!CdiOsSignalReadState(adapter_con_state_ptr->shutdown_signal)) {
        load_state.top_time = CdiOsGetMicroseconds();
        bool idle = true;

        if (is_transmitter) {
            if (TxPollProcess(adapter_endpoint_ptr, is_poll, notification_signal)) {
                idle = false;
            }
        } else {
            if (RxPollProcess(adapter_endpoint_ptr, is_poll, notification_signal)) {
                idle = false;
            }
        }

        // No need to poll adapter endpoint if the notification signal is set.
        if (!CdiOsSignalReadState(notification_signal)) {
            // Perform adapter specific poll mode processing.
            if (CdiAdapterPollEndpoint(adapter_endpoint_ptr) == kCdiStatusOk) {
                idle = false;
            }
        }

        // Check if busy/idle state is different this time compared to last.
        UpdateThreadUtilizationStats(endpoint_stats_ptr, idle, &load_state);
    }

    // If a receiver, ensure receiver poll processing is performed to flush Rx queues.
    if (!is_transmitter) {
        RxPollProcess(adapter_endpoint_ptr, is_poll, adapter_con_state_ptr->shutdown_signal);
    }
}

/**
 * Used directly by PollThread() to process polling for a data endpoint.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection state data.
 *
 * @return The return value is not used.
 */
static void DataPoll(AdapterConnectionState* adapter_con_state_ptr)
{
    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(adapter_con_state_ptr->log_handle);

    bool is_transmitter = (kEndpointDirectionSend == adapter_con_state_ptr->direction);
    bool is_poll = (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll);
    EndpointManagerHandle mgr_handle = EndpointManagerConnectionToEndpointManager(
                                            adapter_con_state_ptr->data_state.cdi_connection_handle);
    CdiSignalType notification_signal = EndpointManagerGetNotificationSignal(mgr_handle);

    // Register this thread with the Endpoint Manager as being part of this connection. Since this is a polling thread,
    // we use the non-blocking EndpointManagerPoll() API instead of the blocking EndpointManagerThreadWait() API.
    EndpointManagerThreadRegister(mgr_handle, CdiOsThreadGetName(adapter_con_state_ptr->poll_thread_id));

    // These variables are used for computing the CPU utilization of this thread.
    ThreadUtilizationState load_state = {
        .top_time = 0,
        .start_time = CdiOsGetMicroseconds(),
        .idle_accumulator = 0,
        .busy_accumulator = 0
    };

    // The control interface does not use the Endpoint Manager and must rely on the shutdown_signal. For other endpoint
    // types that use the Endpoint Manager, they will use the EndpointManagerThreadWait() below and be blocked. As part
    // of the Endpoint Manager's shutdown sequence, the shutdown_signal gets set, so when this thread starts running
    // again, it will exit.
    while (!CdiOsSignalReadState(adapter_con_state_ptr->shutdown_signal) &&
           !EndpointManagerIsConnectionShuttingDown(mgr_handle)) {
        // Walk through each endpoint that is part of this connection.
        CdiEndpointHandle cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_handle);
        while (cdi_endpoint_handle) {
            load_state.top_time = CdiOsGetMicroseconds();
            bool idle = true;

            AdapterEndpointState* adapter_endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
            // Get Endpoint Manager notification signal.
            if (EndpointManagerPoll(&cdi_endpoint_handle) && adapter_endpoint_ptr) {
                if (is_transmitter) {
                    if (TxPollProcess(adapter_endpoint_ptr, is_poll, notification_signal)) {
                        idle = false;
                    }
                } else {
                    if (RxPollProcess(adapter_endpoint_ptr, is_poll, notification_signal)) {
                        idle = false;
                    }
                }
                // No need to poll adapter endpoint if the notification signal is set.
                if (!CdiOsSignalReadState(notification_signal)) {
                    // Perform adapter specific poll mode processing.
                    if (CdiAdapterPollEndpoint(adapter_endpoint_ptr) == kCdiStatusOk) {
                        idle = false;
                    }
                }
                // Check if busy/idle state is different this time compared to last.
                UpdateThreadUtilizationStats(adapter_endpoint_ptr->endpoint_stats_ptr, idle, &load_state);
            }
        }
    }

    // If a receiver, ensure receiver poll processing is performed for all endpoints related to this connection to flush
    // Rx queues.
    if (!is_transmitter) {
        CdiEndpointHandle cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_handle);
        while (cdi_endpoint_handle) {
            AdapterEndpointState* adapter_endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
            RxPollProcess(adapter_endpoint_ptr, is_poll, adapter_con_state_ptr->shutdown_signal);
            cdi_endpoint_handle = EndpointManagerGetNextEndpoint(cdi_endpoint_handle);
        }
    }

    EndpointManagerPollThreadExit(mgr_handle);
}

/**
 * Thread used to process polling for an endpoint.
 *
 * @param ptr Pointer to adapter endpoint state data (AdapterConnectionState).
 *
 * @return The return value is not used.
 */
static THREAD PollThread(void* ptr)
{
    AdapterConnectionState* adapter_con_state_ptr = (AdapterConnectionState*)ptr;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(adapter_con_state_ptr->log_handle);

    if (kEndpointTypeControl == adapter_con_state_ptr->data_type) {
        ControlInterfacePoll(adapter_con_state_ptr);
    } else {
        DataPoll(adapter_con_state_ptr);
    }

    CdiLoggerThreadLogUnset();
    return 0; // Return code not used.
}

#ifdef DEBUG_ENABLE_QUEUE_DEBUGGING
static void QueueDebugCallback(const CdiQueueCbData* cb_ptr)
{
    Packet* item_ptr = (Packet*)cb_ptr->item_data_ptr;

    if (cb_ptr->is_pop) {
        CDI_LOG_THREAD(kLogDebug, "QR H[%p] T[%p] SGL[%d]", cb_ptr->read_ptr, cb_ptr->write_ptr,
                       item_ptr->sg_list.total_data_size);
    } else {
        CDI_LOG_THREAD(kLogDebug, "QW H[%p] T[%p] SGL{%d]", cb_ptr->read_ptr, cb_ptr->write_ptr,
                       item_ptr->sg_list.total_data_size);
    }
}
#endif

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiAdapterCreateConnection(CdiAdapterConnectionConfigData* config_data_ptr,
                                           AdapterConnectionHandle* return_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    // Allocate a generic connection state structure.
    AdapterConnectionState* adapter_con_state_ptr = CdiOsMemAllocZero(sizeof(*adapter_con_state_ptr));
    if (adapter_con_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    // Create signal for starting endpoint threads. Must create this before CreateConnection() is used, since it can
    // create threads that use it.
    if (rs == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&adapter_con_state_ptr->start_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (rs == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&adapter_con_state_ptr->shutdown_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (rs == kCdiStatusOk) {
        // Link endpoint to its adapter, queue message function and log.
        adapter_con_state_ptr->adapter_state_ptr = config_data_ptr->cdi_adapter_handle;
        adapter_con_state_ptr->data_state.cdi_connection_handle = config_data_ptr->cdi_connection_handle;
        adapter_con_state_ptr->log_handle = config_data_ptr->log_handle;
        adapter_con_state_ptr->data_state.connection_cb_ptr = config_data_ptr->connection_cb_ptr;
        adapter_con_state_ptr->data_state.connection_user_cb_param = config_data_ptr->connection_user_cb_param;

        // Remember what kind of endpoint this is.
        adapter_con_state_ptr->direction = config_data_ptr->direction;
        if (kEndpointDirectionReceive == config_data_ptr->direction) {
            adapter_con_state_ptr->rx_state = config_data_ptr->rx_state;
        }

        adapter_con_state_ptr->port_number = config_data_ptr->port_number;

        // Set this prior to opening the endpoint. Receive packets may start flowing before Open() returns and the
        // connection must have a valid endpoint pointer set.
        *return_handle_ptr = adapter_con_state_ptr;

        adapter_con_state_ptr->data_type = config_data_ptr->data_type;

        // Do adapter specific open connection actions.
        rs = config_data_ptr->cdi_adapter_handle->functions_ptr->CreateConnection(adapter_con_state_ptr,
                                                                                  config_data_ptr->port_number);
    }

    if (rs == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&adapter_con_state_ptr->poll_do_work_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    const char* thread_name_prefix_str = "PollRx";
    if (rs == kCdiStatusOk) {
        if (kEndpointDirectionSend == config_data_ptr->direction) {
            // Create Tx resources.
            thread_name_prefix_str = "PollTx";
        }
    }

    if (rs == kCdiStatusOk) {
        char thread_name_str[MAX_THREAD_NAME];
        snprintf(thread_name_str, sizeof(thread_name_str), "%s%s", thread_name_prefix_str,
                 CdiUtilityKeyEnumToString(kKeyAdapterType,
                                           config_data_ptr->cdi_adapter_handle->adapter_data.adapter_type));

        // Create poll worker thread.
        if (!CdiOsThreadCreatePinned(PollThread, &adapter_con_state_ptr->poll_thread_id, thread_name_str,
                                     adapter_con_state_ptr, adapter_con_state_ptr->start_signal,
                                     config_data_ptr->thread_core_num)) {
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    if (rs != kCdiStatusOk) {
        CdiAdapterDestroyConnection(adapter_con_state_ptr);
        adapter_con_state_ptr = NULL;
        *return_handle_ptr = NULL;
    }

    return rs;
}

CdiReturnStatus CdiAdapterStopConnection(AdapterConnectionHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    AdapterConnectionState* adapter_con_state_ptr = (AdapterConnectionState*)handle;

    if (adapter_con_state_ptr) {
        // Clean-up thread resources. We will wait for it to exit using thread join.
        SdkThreadJoin(adapter_con_state_ptr->poll_thread_id, adapter_con_state_ptr->shutdown_signal);
        adapter_con_state_ptr->poll_thread_id = NULL;
    }

    return kCdiStatusOk;
}

CdiReturnStatus CdiAdapterDestroyConnection(AdapterConnectionHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterConnectionState* adapter_con_state_ptr = (AdapterConnectionState*)handle;

    if (adapter_con_state_ptr) {
        // Ensure connection has been stopped.
        rs = CdiAdapterStopConnection(handle);

        adapter_con_state_ptr->adapter_state_ptr->functions_ptr->DestroyConnection(adapter_con_state_ptr);

        // Now that the threads have stopped, it is safe to clean up the remaining resources.
        CdiOsSignalDelete(adapter_con_state_ptr->poll_do_work_signal);
        CdiOsSignalDelete(adapter_con_state_ptr->shutdown_signal);
        CdiOsSignalDelete(adapter_con_state_ptr->start_signal);

        // Free the memory allocated for this connection.
        CdiOsMemFree(adapter_con_state_ptr);
    }

    return rs;
}

CdiReturnStatus CdiAdapterOpenEndpoint(CdiAdapterEndpointConfigData* config_data_ptr,
                                       AdapterEndpointHandle* return_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterConnectionState* adapter_con_state_ptr = config_data_ptr->connection_handle;

    // Allocate a generic endpoint state structure.
    AdapterEndpointState* endpoint_state_ptr = CdiOsMemAllocZero(sizeof(AdapterEndpointState));
    if (endpoint_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    // Create signal for starting endpoint threads. Must create this before Open() is used, since Open() can create
    // threads that use it.
    if (rs == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&endpoint_state_ptr->start_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (rs == kCdiStatusOk) {
        if (!CdiOsSignalCreate(&endpoint_state_ptr->shutdown_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (rs == kCdiStatusOk) {
        // Link endpoint to its adapter, queue message function and log.
        endpoint_state_ptr->adapter_con_state_ptr = adapter_con_state_ptr;
        endpoint_state_ptr->cdi_endpoint_handle = config_data_ptr->cdi_endpoint_handle;
        endpoint_state_ptr->msg_from_endpoint_func_ptr = config_data_ptr->msg_from_endpoint_func_ptr;
        endpoint_state_ptr->msg_from_endpoint_param_ptr = config_data_ptr->msg_from_endpoint_param_ptr;

        if (kEndpointDirectionSend == adapter_con_state_ptr->direction) {
            // If the adapter does not support poll mode, then use a queue that supports a wait signal so PollThread()
            // can sleep whenever the queue is empty.
            bool is_poll = (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll);
            CdiQueueSignalMode queue_signal_mode = (is_poll) ? kQueueSignalNone : kQueueSignalPopWait;

            if (rs == kCdiStatusOk) {
                // NOTE: This queue is designed to not be growable, so CdiAdapterEnqueueSendPackets() can return a
                // queue full error. Caller should use logic to retry in this case.
                if (!CdiQueueCreate("Tx Packet CdiSinglyLinkedList Queue", MAX_TX_PACKET_BATCHES_PER_CONNECTION,
                                    NO_GROW_SIZE, NO_GROW_COUNT, sizeof(CdiSinglyLinkedList),
                                    queue_signal_mode, &endpoint_state_ptr->tx_packet_queue_handle)) {
                    rs = kCdiStatusAllocationFailed;
                }
#ifdef DEBUG_ENABLE_QUEUE_DEBUGGING
                CdiQueueDebugEnable(endpoint_state_ptr->tx_packet_queue_handle, QueueDebugCallback);
#endif
                CdiSinglyLinkedListInit(&endpoint_state_ptr->tx_packet_waiting_list);
            }
        }
    }

    if (rs == kCdiStatusOk) {
        // Set this prior to opening the endpoint. Receive packets may start flowing before Open() returns and the
        // connection must have a valid endpoint pointer set.
        *return_handle_ptr = endpoint_state_ptr;

        endpoint_state_ptr->endpoint_stats_ptr = config_data_ptr->endpoint_stats_ptr;

        // Do adapter specific open actions.
        CdiAdapterState* adapter_state_ptr = config_data_ptr->connection_handle->adapter_state_ptr;
        rs = adapter_state_ptr->functions_ptr->Open(endpoint_state_ptr, config_data_ptr->remote_address_str,
                                                    config_data_ptr->port_number);
    }

    if (rs != kCdiStatusOk) {
        CdiAdapterCloseEndpoint(endpoint_state_ptr);
        endpoint_state_ptr = NULL;
        *return_handle_ptr = NULL;
    }

    return rs;
}

CdiReturnStatus CdiAdapterPollEndpoint(AdapterEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (NULL == handle) {
        rs = kCdiStatusInvalidHandle;
    } else if (handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll) {
        rs = handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll(handle);
    }

    return rs;
}

CdiReturnStatus CdiAdapterStartEndpoint(AdapterEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (NULL == handle) {
        rs = kCdiStatusInvalidHandle;
    } else {
        if (handle->adapter_con_state_ptr->start_signal) {
            CdiOsSignalSet(handle->adapter_con_state_ptr->start_signal);
        }
        if (handle->start_signal) {
            CdiOsSignalSet(handle->start_signal);
        }
        if (handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Start) {
            rs = handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Start(handle);
        }
    }

    return rs;
}

CdiReturnStatus CdiAdapterResetEndpoint(AdapterEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    CdiAdapterState* adapter_state_ptr = handle->adapter_con_state_ptr->adapter_state_ptr;

    if (NULL == handle) {
        rs = kCdiStatusInvalidHandle;
    } else if (adapter_state_ptr->functions_ptr->Reset) {
        rs = adapter_state_ptr->functions_ptr->Reset(handle);
    }

    return rs;
}

CdiReturnStatus CdiAdapterCloseEndpoint(AdapterEndpointHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterConnectionState* adapter_con_state_ptr = handle->adapter_con_state_ptr;

    if (handle) {
        // Set signal to shutdown adapter endpoint threads.
        if (handle->shutdown_signal) {
            CdiOsSignalSet(handle->shutdown_signal);
        }

        CdiAdapterState* adapter_state_ptr = adapter_con_state_ptr->adapter_state_ptr;
        rs = adapter_state_ptr->functions_ptr->Close(handle);

        CdiQueueDestroy(handle->tx_packet_queue_handle);
        CdiOsSignalDelete(handle->shutdown_signal);
        CdiOsSignalDelete(handle->start_signal);
        CdiOsMemFree(handle);
    }

    return rs;
}

EndpointTransmitQueueLevel CdiAdapterGetTransmitQueueLevel(AdapterEndpointHandle handle)
{
    return handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->GetTransmitQueueLevel(handle);
}

CdiReturnStatus CdiAdapterEnqueueSendPackets(const AdapterEndpointHandle handle,
                                             const CdiSinglyLinkedList *packet_list_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    assert(handle->adapter_con_state_ptr->direction == kEndpointDirectionSend);

    // Add the packet message into the Tx packet queue for processing by the PollThread() thread.
    if (!CdiQueuePush(handle->tx_packet_queue_handle, packet_list_ptr)) {
        CDI_LOG_THREAD(kLogInfo, "Tx packet queue[%s] full.", CdiQueueGetName(handle->tx_packet_queue_handle));
        rs = kCdiStatusQueueFull;
    }

    return rs;
}

CdiReturnStatus CdiAdapterEnqueueSendPacket(const AdapterEndpointHandle handle, Packet* packet_ptr)
{
    CdiSinglyLinkedList packet_list;
    CdiSinglyLinkedListInit(&packet_list);
    CdiSinglyLinkedListPushTail(&packet_list, &packet_ptr->list_entry);
    return CdiAdapterEnqueueSendPackets(handle, &packet_list);
}

CdiReturnStatus CdiAdapterFreeBuffer(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    AdapterConnectionState* adapter_con_state_ptr = handle->adapter_con_state_ptr;
    CdiAdapterState* adapter_state_ptr = adapter_con_state_ptr->adapter_state_ptr;
    if (adapter_con_state_ptr->direction != kEndpointDirectionReceive) {
        return kCdiStatusWrongDirection;
    } else {
        return adapter_state_ptr->functions_ptr->RxBuffersFree(handle, sgl_ptr);
    }
}

CdiReturnStatus CdiAdapterGetPort(const AdapterEndpointHandle handle, int* port_number_ptr)
{
    CdiAdapterState* adapter_state_ptr = handle->adapter_con_state_ptr->adapter_state_ptr;
    if (!adapter_state_ptr->functions_ptr->GetPort) {
        return kCdiStatusGetPortFailed;
    }

    return adapter_state_ptr->functions_ptr->GetPort(handle, port_number_ptr);
}

CdiReturnStatus CdiAdapterShutdown(CdiAdapterHandle adapter)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    assert(adapter != NULL);

    return adapter->functions_ptr->Shutdown(adapter);
}

void CdiAdapterPollThreadFlushResources(AdapterEndpointHandle handle)
{
    if (handle) {
        CdiQueueFlush(handle->tx_packet_queue_handle);
        CdiSinglyLinkedListInit(&handle->tx_packet_waiting_list);
    }
}
