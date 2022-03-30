// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

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
#include "list_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This enumeration is used in the notification signal array to indicate the index of specific OS signals.
 */
typedef enum {
    kSignalIndexConnectionList = 0, ///< Index of signal used for connection list changes.
    kSignalIndexArray = 1, ///< Starting index of array of signals used for other notifications.
} SignalIndex;

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
 * @param num_signals Number of signals in notification_signal_array
 * @param notification_signal_array Array of signals used to cancel waiting to pop a packet list from the incoming
 *                            queue. Specify NULL if the adapter is a polling type adapter in which case NULL will be
 *                            returned when no packets are waiting.
 * @param last_packet_ret_ptr Pointer to a bool which is set to true upon last packet currently available. This is only
 *                            modified in the case where NULL is not returned.
 *
 * @return The address of the Packet structure dequeued or NULL if no packet was waiting.
 */
static Packet* GetNextPacket(AdapterEndpointState* endpoint_ptr, int num_signals,
                             CdiSignalType* notification_signal_array, bool* last_packet_ret_ptr)
{
    Packet* packet_ptr = NULL;

    // Check packet waiting list and if it's empty try to get some from the queue.
    if (CdiSinglyLinkedListIsEmpty(&endpoint_ptr->tx_packet_waiting_list)) {
        if (notification_signal_array) {
            CdiQueuePopWaitMultiple(endpoint_ptr->tx_packet_queue_handle, CDI_INFINITE, notification_signal_array,
                                    num_signals, NULL, &endpoint_ptr->tx_packet_waiting_list);
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
 * Perform poll process for Rx endpoint.
 *
 * @param endpoint_state_ptr Pointer to adapter endpoint state data.
 *
 * @return true if useful work was done, false if the function did nothing productive.
 */
static bool RxPollProcess(AdapterEndpointState* endpoint_state_ptr)
{
    bool productive = false;

    // Free resources, if required. Probe manages freeing resources in ProcessPacket(), so this logic is not used.
    CdiSgList sgl_packet_buffers;
    if (RxPollFreeBuffer(endpoint_state_ptr->cdi_endpoint_handle, &sgl_packet_buffers)) {
        productive = true;
        // Free adapter Rx packet buffer resources.
        CdiAdapterFreeBuffer(endpoint_state_ptr, &sgl_packet_buffers);
    }

    return productive;
}

/**
 * Used directly by PollThread() to process polling for a control interface endpoint. NOTE: This logic may block until a
 * packet is received or a notification signal is set.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection state data.
 * @param num_signals Number of signals in notification_signal_array.
 * @param notification_signal_array Array of signals used to cancel waiting.
 */
static void ControlInterfacePoll(AdapterConnectionState* adapter_con_state_ptr, int num_signals,
                                 CdiSignalType* notification_signal_array)
{
    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(adapter_con_state_ptr->log_handle);

    AdapterEndpointState* adapter_endpoint_ptr = adapter_con_state_ptr->control_state.control_endpoint_handle;
    CdiSignalType notification_signal = adapter_con_state_ptr->shutdown_signal;

    if (kPollStart == adapter_con_state_ptr->poll_state) {
        adapter_con_state_ptr->poll_state = kPollRunning;
    }

    // The control interface does not use the Endpoint Manager and must rely on the shutdown_signal.
    if (!CdiOsSignalReadState(adapter_con_state_ptr->shutdown_signal)) {
        bool idle = true;
        if (adapter_con_state_ptr->can_transmit) {
            // Process transmit poll operation.
            Packet* packet_ptr = NULL;
            bool last_packet = false;
            EndpointTransmitQueueLevel queue_level = CdiAdapterGetTransmitQueueLevel(adapter_endpoint_ptr);
            bool got_packet = (kEndpointTransmitQueueFull != queue_level) &&
                (NULL != (packet_ptr = GetNextPacket(adapter_endpoint_ptr, 0, NULL, &last_packet)));
            if (!got_packet &&
                (kEndpointTransmitQueueEmpty == queue_level || kEndpointTransmitQueueNa == queue_level)) {
                // NOTE: This logic blocks until a packet is received or a notification signal is set.
                // The queue has been configured to support the pop wait signal (can use the queue wait API function).
                // This greatly simplifies the logic for threads pushing data into the queue, since the signaling is
                // built into the queue.
                got_packet = NULL != (packet_ptr = GetNextPacket(adapter_endpoint_ptr, num_signals,
                                                                 notification_signal_array, &last_packet));
            }
            if (got_packet) {
                // Get thread-safe access to endpoint resources. Users can free buffers via RxEnqueueFreeBuffer() or
                // internally an endpoint can be destroyed by the Endpoint Manager via DestroyEndpoint().
                CdiOsCritSectionReserve(adapter_con_state_ptr->endpoint_lock);

                // Use the adapter to send the packet.
                adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Send(adapter_endpoint_ptr, packet_ptr,
                                                                              last_packet);
                CdiOsCritSectionRelease(adapter_con_state_ptr->endpoint_lock);
                // NOTE: No need to generate any error or warnings here since, the Send() will normally fail if the
                // receiver is not connected (ie. during probe).
                idle = false;
            }
        }

        // If can receive, process receive poll operation.
        if (adapter_con_state_ptr->can_receive && RxPollProcess(adapter_endpoint_ptr)) {
            idle = false;
        }

        // No need to poll adapter endpoint if the notification signal is set.
        if (!CdiOsSignalReadState(notification_signal)) {
            // Perform adapter specific poll mode processing.
            if (kCdiStatusOk == CdiAdapterPollEndpoint(adapter_endpoint_ptr)) {
                idle = false;
            }
        }

        // Check if busy/idle state is different this time compared to last.
        UpdateThreadUtilizationStats(adapter_endpoint_ptr->endpoint_stats_ptr, idle,
                                     &adapter_con_state_ptr->load_state);
    } else {
        // Shutting down the connection. If a receiver, ensure receiver poll processing is performed to flush Rx queues.
        if (adapter_con_state_ptr->can_receive) {
            RxPollProcess(adapter_endpoint_ptr);
        }
        adapter_con_state_ptr->poll_state = kPollStopped;
    }
}

/**
 * Used directly by PollThread() to process polling for a data endpoint.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection state data.
 *
 * @return true if all endpoints for the specified connection are idle (no work to do).
 */
static bool DataPoll(AdapterConnectionState* adapter_con_state_ptr)
{
    bool all_idle = true;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(adapter_con_state_ptr->log_handle);

    assert(kEndpointDirectionBidirectional != adapter_con_state_ptr->direction); // Not supported

    EndpointManagerHandle mgr_handle = EndpointManagerConnectionToEndpointManager(
                                          adapter_con_state_ptr->data_state.cdi_connection_handle);

    if (kPollStart == adapter_con_state_ptr->poll_state) {
        // Register this thread with the Endpoint Manager as being part of this connection. Since this is a polling
        // thread, we use the non-blocking EndpointManagerPoll() function instead of the blocking
        // EndpointManagerThreadWait() function.
        EndpointManagerThreadRegister(mgr_handle,
                                      CdiOsThreadGetName(adapter_con_state_ptr->poll_thread_state_ptr->thread_id));
        adapter_con_state_ptr->poll_state = kPollRunning;
    } else if (kPollStopping == adapter_con_state_ptr->poll_state) {
        if (EndpointManagerPollThreadExit(mgr_handle)) {
            adapter_con_state_ptr->poll_state = kPollStopped;
        }
        return all_idle;
    }

    // The Endpoint Manager is used here to control suspend, restart and shutdown sequences.
    if (!CdiOsSignalReadState(adapter_con_state_ptr->shutdown_signal) &&
        !EndpointManagerIsConnectionShuttingDown(mgr_handle)) {
        // Walk through each endpoint that is part of this connection.
        CdiEndpointHandle cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_handle);

        if (cdi_endpoint_handle) {
            // Account for poll thread sleeping (idle time).
            AdapterEndpointState* adapter_endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
            UpdateThreadUtilizationStats(adapter_endpoint_ptr->endpoint_stats_ptr, true,
                                         &adapter_con_state_ptr->load_state);
        }

        while (cdi_endpoint_handle) {
            adapter_con_state_ptr->load_state.top_time = CdiOsGetMicroseconds();
            bool idle = true;
            AdapterEndpointState* adapter_endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
            // Get Endpoint Manager notification signal.
            if (EndpointManagerPoll(&cdi_endpoint_handle) && adapter_endpoint_ptr) {
                if (adapter_con_state_ptr->can_transmit) {
                    Packet* packet_ptr = NULL;
                    bool last_packet = false;
                    EndpointTransmitQueueLevel queue_level = CdiAdapterGetTransmitQueueLevel(adapter_endpoint_ptr);
                    bool got_packet = (kEndpointTransmitQueueFull != queue_level) &&
                        (NULL != (packet_ptr = GetNextPacket(adapter_endpoint_ptr, 0, NULL, &last_packet)));
                    if (got_packet) {
                        idle = false;
                        // Use the adapter to send the packet.
                        adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Send(adapter_endpoint_ptr, packet_ptr,
                                                                                      last_packet);
                        // NOTE: No need to generate any error or warnings here since, the Send() will normally fail if
                        // the receiver is not connected (ie. during probe).
                    }
                    if (kEndpointTransmitQueueEmpty != CdiAdapterGetTransmitQueueLevel(adapter_endpoint_ptr)) {
                        // Packets are in flight (have not received ACKs back), so don't want this poll thread to sleep.
                        all_idle = false;
                    }
                } else {
                    if (RxPollProcess(adapter_endpoint_ptr)) {
                        idle = false;
                        all_idle = false;
                    }
                }

                // Perform adapter specific poll mode processing.
                if (kCdiStatusOk == CdiAdapterPollEndpoint(adapter_endpoint_ptr)) {
                    idle = false;
                }

                // Check if busy/idle state is different this time compared to last.
                UpdateThreadUtilizationStats(adapter_endpoint_ptr->endpoint_stats_ptr, idle,
                                             &adapter_con_state_ptr->load_state);
            }
        }
        // Set top time to account for poll thread idle time performed outside of this function (ie. mostly sleep time).
        adapter_con_state_ptr->load_state.top_time = CdiOsGetMicroseconds();
    } else {
        // Connection is shutting down. If a receiver, ensure receiver poll processing is performed for all endpoints
        // related to this connection to flush Rx queues.
        if (!adapter_con_state_ptr->can_transmit) {
            CdiEndpointHandle cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_handle);
            while (cdi_endpoint_handle) {
                AdapterEndpointState* adapter_endpoint_ptr = EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
                RxPollProcess(adapter_endpoint_ptr);
                cdi_endpoint_handle = EndpointManagerGetNextEndpoint(cdi_endpoint_handle);
            }
        }
        if (kPollRunning == adapter_con_state_ptr->poll_state) {
            adapter_con_state_ptr->poll_state = kPollStopping;
        }
    }

    return all_idle;
}

/**
 * Thread used to process polling for an endpoint.
 *
 * @param ptr Pointer to adapter endpoint state data (AdapterConnectionState).
 *
 * @return The return value is not used.
 */
static CDI_THREAD PollThread(void* ptr)
{
    PollThreadState* poll_thread_state_ptr = (PollThreadState*)ptr;
    AdapterConnectionState* adapter_con_ptr_array[CDI_MAX_SIMULTANEOUS_CONNECTIONS] = {0};
    int num_of_connections = 0;
    int connection_index = 0;

    // Allocate array of signals used to wake-up a poll thread used by a Tx connection. First signal is
    // connection_list_changed_signal. Next is an array of signals, grouped by connection.
    CdiSignalType tx_signal_array[1+3*CDI_MAX_SIMULTANEOUS_CONNECTIONS] = {0};
    tx_signal_array[kSignalIndexConnectionList] = poll_thread_state_ptr->connection_list_changed_signal;
    int num_signals = kSignalIndexArray;

    bool all_idle = true;
    while (true) {
        if (CdiOsSignalReadState(poll_thread_state_ptr->connection_list_changed_signal) && (0 == connection_index)) {
            // Make local copy of the connection list for this poll thread. This allows the connection list to be
            // externally updated without affecting the poll thread.
            CdiOsCritSectionReserve(poll_thread_state_ptr->connection_list_lock);

            CdiListIterator list_iterator;
            CdiListIteratorInit(&poll_thread_state_ptr->connection_list, &list_iterator);
            num_of_connections = 0;
            all_idle = true;
            num_signals = kSignalIndexArray;
            AdapterConnectionState* entry_ptr = NULL;
            poll_thread_state_ptr->only_transmit = true; // Default to only transmit. State is updated below.
            while (NULL != (entry_ptr = (AdapterConnectionState*)CdiListIteratorGetNext(&list_iterator))) {
                adapter_con_ptr_array[num_of_connections++] = entry_ptr;
                // If receiver or bi-directional then clear the only_transmit flag used to determine if poll thread can
                // sleep.
                if (kEndpointDirectionReceive == entry_ptr->direction ||
                    kEndpointDirectionBidirectional == entry_ptr->direction) {
                    poll_thread_state_ptr->only_transmit = false;
                }

                // If the Tx poll do work signal exists, add it to the array.
                if (entry_ptr->tx_poll_do_work_signal) {
                    tx_signal_array[num_signals++] = entry_ptr->tx_poll_do_work_signal;
                }

                if (kEndpointTypeControl == poll_thread_state_ptr->data_type) {
                    // Control interface uses Tx packet queue for notification signals.
                    if (!poll_thread_state_ptr->is_poll && entry_ptr->can_transmit) {
                        AdapterEndpointState* adapter_endpoint_ptr = entry_ptr->control_state.control_endpoint_handle;
                        tx_signal_array[num_signals++] =
                            CdiQueueGetPopWaitSignal(adapter_endpoint_ptr->tx_packet_queue_handle);
                    }
                } else {
                    // Data interface uses Endpoint Manager notification signals.
                    EndpointManagerHandle mgr_handle = EndpointManagerConnectionToEndpointManager(
                                        entry_ptr->data_state.cdi_connection_handle);
                    tx_signal_array[num_signals++] = EndpointManagerGetNotificationSignal(mgr_handle);
                }
            }
            CdiOsSignalClear(poll_thread_state_ptr->connection_list_changed_signal);
            CdiOsSignalSet(poll_thread_state_ptr->connection_list_processed_signal);
            CdiOsCritSectionRelease(poll_thread_state_ptr->connection_list_lock);
        }

        if (0 == num_of_connections) {
            // No connections, so exit the loop and the thread.
            break;
        }

        AdapterConnectionState* adapter_con_state_ptr = adapter_con_ptr_array[connection_index];

        if (kPollStart == adapter_con_state_ptr->poll_state) {
            // This connection is just starting to use the poll thread, so update initial data.
            adapter_con_state_ptr->load_state.top_time = CdiOsGetMicroseconds();
            adapter_con_state_ptr->load_state.start_time = adapter_con_state_ptr->load_state.top_time;
            adapter_con_state_ptr->load_state.idle_accumulator = 0;
            adapter_con_state_ptr->load_state.busy_accumulator = 0;
        }

        if (kPollStopped != adapter_con_state_ptr->poll_state) {
            // Connection is active, so allow poll thread to poll it.
            if (kEndpointTypeControl == poll_thread_state_ptr->data_type) {
                // Control interface (SDK probe/control protocol).
                ControlInterfacePoll(adapter_con_state_ptr, num_signals, tx_signal_array);
                // Control interface adapter's don't require polling. If the the Tx packet queue is empty, then wait
                // for a notification. When a packet is pushed onto the queue the pop wait signal is set. See use of
                // CdiQueueGetPopWaitSignal() above.
                assert(!poll_thread_state_ptr->is_poll);
                AdapterEndpointState* adapter_endpoint_ptr = adapter_con_state_ptr->control_state.control_endpoint_handle;
                if (CdiQueueIsEmpty(adapter_endpoint_ptr->tx_packet_queue_handle)) {
                    CdiOsSignalsWait(tx_signal_array, num_signals, false, CDI_INFINITE, NULL);
                }
            } else {
                // Data interface (user data payloads/packets and probe EFA packets).
                if (!DataPoll(adapter_con_state_ptr)) {
                    all_idle = false;
                }
                // For transmitter, If tx_poll_do_work_signal is set and all endpoints are idle then clear the signal,
                // ensuring that was ok to clear it.
                if (adapter_con_state_ptr->can_transmit &&
                    CdiOsSignalReadState(adapter_con_state_ptr->tx_poll_do_work_signal) && all_idle) {
                    CdiOsSignalClear(adapter_con_state_ptr->tx_poll_do_work_signal);
                    // To avoid the use of critical sections here, use atomic operations to ensure it was safe to clear
                    // the signal. If tx_in_flight_ref_count was incremented outside the scope of this function after we
                    // just cleared tx_poll_do_work_signal, then restore the signal's state (set it).
                    EndpointManagerHandle mgr_handle = EndpointManagerConnectionToEndpointManager(
                                                          adapter_con_state_ptr->data_state.cdi_connection_handle);
                    CdiEndpointHandle cdi_endpoint_handle = EndpointManagerGetFirstEndpoint(mgr_handle);
                    while (cdi_endpoint_handle) {
                        AdapterEndpointState* adapter_endpoint_ptr =
                            EndpointManagerEndpointToAdapterEndpoint(cdi_endpoint_handle);
                        if (CdiOsAtomicLoad32(&adapter_endpoint_ptr->tx_in_flight_ref_count)) {
                            CdiOsSignalSet(adapter_con_state_ptr->tx_poll_do_work_signal);
                            break;
                        }
                        cdi_endpoint_handle = EndpointManagerGetNextEndpoint(cdi_endpoint_handle);
                    }
                }
            }
        }

        // Advance to next connection.
        connection_index++;
        if (connection_index >= num_of_connections) {
            connection_index = 0;
            // If the poll thread is data type, only contains transmitters, uses a polling adapter and all endpoints for
            // all connections are currently idle then sleep until there is a notification.
            if (kEndpointTypeData == poll_thread_state_ptr->data_type && poll_thread_state_ptr->only_transmit &&
                poll_thread_state_ptr->is_poll && all_idle) {
#ifdef DEBUG_POLL_THREAD_SLEEP_TIME
                uint64_t start_time = CdiOsGetMicroseconds();
#endif
                uint32_t index = 0;
                CdiOsSignalsWait(tx_signal_array, num_signals, false, CDI_INFINITE, &index);
#ifdef DEBUG_POLL_THREAD_SLEEP_TIME
                CDI_LOG_THREAD(kLogInfo, "SigIdx=%d slept=%lu", index, CdiOsGetMicroseconds() - start_time);
#endif
            }
            all_idle = true;
        }
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

/**
 * Add an adapter connection to the specified poll thread.
 *
 * @param poll_thread_state_ptr Pointer to poll thread state data.
 * @param adapter_con_state_ptr Pointer to adapter connection to add to the poll thread.
 */
static void PollThreadConnectionAdd(PollThreadState* poll_thread_state_ptr,
                                    AdapterConnectionState* adapter_con_state_ptr)
{
    adapter_con_state_ptr->poll_thread_state_ptr = poll_thread_state_ptr;

    CdiOsCritSectionReserve(poll_thread_state_ptr->connection_list_lock);
    CdiListAddTail(&poll_thread_state_ptr->connection_list, &adapter_con_state_ptr->list_entry);
    CdiOsSignalSet(poll_thread_state_ptr->connection_list_changed_signal);
    CdiOsCritSectionRelease(poll_thread_state_ptr->connection_list_lock);
}

/**
 * Destroy poll thread.
 *
 * @param poll_thread_state_ptr Pointer to poll thread state data.
 * @param shutdown_signal Shutdown signal.
 */
static void PollThreadDestroy(PollThreadState* poll_thread_state_ptr, CdiSignalType shutdown_signal)
{
    if (poll_thread_state_ptr) {
        if (poll_thread_state_ptr->thread_id) {
            // Clean-up thread resources. We will wait for it to exit using thread join.
            SdkThreadJoin(poll_thread_state_ptr->thread_id, shutdown_signal);
            poll_thread_state_ptr->thread_id = NULL;
        }

        CdiOsSignalDelete(poll_thread_state_ptr->start_signal);
        CdiOsCritSectionDelete(poll_thread_state_ptr->connection_list_lock);
        CdiOsSignalDelete(poll_thread_state_ptr->connection_list_processed_signal);
        CdiOsSignalDelete(poll_thread_state_ptr->connection_list_changed_signal);
        CdiOsMemFree(poll_thread_state_ptr);
    }
}

/**
 * Remove the specified adapter connection from the poll thread.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection to remove.
 */
static void PollThreadConnectionRemove(AdapterConnectionState* adapter_con_state_ptr)
{
    PollThreadState* poll_thread_state_ptr = adapter_con_state_ptr->poll_thread_state_ptr;
    if (poll_thread_state_ptr) {
        // Get exclusive lock to access connection list and related signals.
        CdiOsCritSectionReserve(poll_thread_state_ptr->connection_list_lock);

        // Remove entry from the connection list.
        CdiListRemove(&poll_thread_state_ptr->connection_list, &adapter_con_state_ptr->list_entry);

        // Ensure processed signal is clear. It is used below to determine when the poll thread has completed processing
        // the revised connection list.
        CdiOsSignalClear(poll_thread_state_ptr->connection_list_processed_signal);

        // Notify poll thread that the connection list has changed.
        CdiOsSignalSet(poll_thread_state_ptr->connection_list_changed_signal);

        // Release lock to connection list and related signals.
        CdiOsCritSectionRelease(poll_thread_state_ptr->connection_list_lock);

        // If poll thread exist and is started, wait for it to process the revised connection list.
        if (poll_thread_state_ptr->thread_id && CdiOsSignalGet(poll_thread_state_ptr->start_signal)) {
            // Wait for poll thread to process the new connection list.
            CdiOsSignalWait(poll_thread_state_ptr->connection_list_processed_signal, CDI_INFINITE, NULL);
        }
        // Now safe to clear the poll thread state for the connection.
        adapter_con_state_ptr->poll_thread_state_ptr = NULL;

        // If the poll thread's connection list is empty, shutdown the poll thread and wait for it to exit.
        if (CdiListIsEmpty(&poll_thread_state_ptr->connection_list)) {
            // Remove the entry from the adapter's poll thread list.
            CdiOsCritSectionReserve(adapter_con_state_ptr->adapter_state_ptr->adapter_lock);
            CdiListRemove(&adapter_con_state_ptr->adapter_state_ptr->poll_thread_list,
                          &poll_thread_state_ptr->list_entry);
            adapter_con_state_ptr->poll_thread_state_ptr = NULL;
            CdiOsCritSectionRelease(adapter_con_state_ptr->adapter_state_ptr->adapter_lock);
            PollThreadDestroy(poll_thread_state_ptr, adapter_con_state_ptr->shutdown_signal);
        }
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiAdapterCreateConnection(CdiAdapterConnectionConfigData* config_data_ptr,
                                           AdapterConnectionHandle* return_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    CdiOsCritSectionReserve(config_data_ptr->cdi_adapter_handle->adapter_lock);

    // Allocate a generic connection state structure.
    AdapterConnectionState* adapter_con_state_ptr = CdiOsMemAllocZero(sizeof(*adapter_con_state_ptr));
    if (adapter_con_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&adapter_con_state_ptr->shutdown_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiOsCritSectionCreate(&adapter_con_state_ptr->endpoint_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Link endpoint to its adapter, queue message function and log.
        adapter_con_state_ptr->adapter_state_ptr = config_data_ptr->cdi_adapter_handle;
        adapter_con_state_ptr->data_state.cdi_connection_handle = config_data_ptr->cdi_connection_handle;
        adapter_con_state_ptr->log_handle = config_data_ptr->log_handle;
        adapter_con_state_ptr->data_state.connection_cb_ptr = config_data_ptr->connection_cb_ptr;
        adapter_con_state_ptr->data_state.connection_user_cb_param = config_data_ptr->connection_user_cb_param;

        // Remember what kind of endpoint this is.
        adapter_con_state_ptr->direction = config_data_ptr->direction;
        adapter_con_state_ptr->can_transmit = (kEndpointDirectionSend == adapter_con_state_ptr->direction ||
                                                kEndpointDirectionBidirectional == adapter_con_state_ptr->direction);
        adapter_con_state_ptr->can_receive = (kEndpointDirectionReceive == adapter_con_state_ptr->direction ||
                                                kEndpointDirectionBidirectional == adapter_con_state_ptr->direction);

        if (adapter_con_state_ptr->can_transmit) {
            if (!CdiOsSignalCreate(&adapter_con_state_ptr->tx_poll_do_work_signal)) {
                rs = kCdiStatusAllocationFailed;
            }
        }
    }

    if (kCdiStatusOk == rs) {
        // Create/setup poll thread prior to creating the connection. This ensures the thread will be started properly
        // by CreateConnection().
        PollThreadState* poll_thread_state_ptr = NULL;

        // Only share the poll thread if the ID is greater than zero.
        if (config_data_ptr->shared_thread_id > 0) {
            // Check if poll thread with this ID already exists.
            CdiListIterator list_iterator;
            // NOTE: Must have acquired adapter_lock before using poll_thread_list.
            CdiListIteratorInit(&config_data_ptr->cdi_adapter_handle->poll_thread_list, &list_iterator);
            while (NULL != (poll_thread_state_ptr = (PollThreadState*)CdiListIteratorGetNext(&list_iterator))) {
                if (poll_thread_state_ptr->shared_thread_id == config_data_ptr->shared_thread_id) {
                    break;
                }
            }
        }

        if (poll_thread_state_ptr) {
            // Use poll thread from existing connection.
            if (poll_thread_state_ptr->thread_core_num != config_data_ptr->thread_core_num) {
                CDI_LOG_THREAD(kLogError, "Poll thread cannot use a mix of thread_core_num. Shared thread ID[%d].",
                               config_data_ptr->shared_thread_id);
                rs = kCdiStatusInvalidParameter;
            } else if (poll_thread_state_ptr->data_type != config_data_ptr->data_type) {
                CDI_LOG_THREAD(kLogError, "Poll thread cannot use a mix of endpoint types. Shared thread ID[%d].",
                               config_data_ptr->shared_thread_id);
                rs = kCdiStatusInvalidParameter;
            } else if (poll_thread_state_ptr->is_poll !=
                       (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll)) {
                CDI_LOG_THREAD(kLogError,
                    "Poll thread cannot use a mix of polling and non-polling adapters. Shared thread ID[%d].",
                    config_data_ptr->shared_thread_id);
                rs = kCdiStatusFatal;
            } else {
                if (adapter_con_state_ptr->can_receive) {
                    poll_thread_state_ptr->only_transmit = false;
                }
                PollThreadConnectionAdd(poll_thread_state_ptr, adapter_con_state_ptr);
            }
        } else {
            // Create a new poll thread for this connection.
            const char* thread_name_prefix_str = NULL;
            switch (config_data_ptr->direction) {
                case kEndpointDirectionSend:
                    thread_name_prefix_str = "PollTx";
                break;
                case kEndpointDirectionReceive:
                    thread_name_prefix_str = "PollRx";
                break;
                case kEndpointDirectionBidirectional:
                    thread_name_prefix_str = "PollBx";
                break;
            }
            char thread_name_str[CDI_MAX_THREAD_NAME];
            snprintf(thread_name_str, sizeof(thread_name_str), "%s%s%d", thread_name_prefix_str,
                    CdiUtilityKeyEnumToString(kKeyAdapterType,
                                            config_data_ptr->cdi_adapter_handle->adapter_data.adapter_type),
                    config_data_ptr->shared_thread_id);

            // Create new poll thread state data.
            PollThreadState* poll_thread_state_ptr = CdiOsMemAllocZero(sizeof(PollThreadState));

            poll_thread_state_ptr->shared_thread_id = config_data_ptr->shared_thread_id;
            poll_thread_state_ptr->thread_core_num = config_data_ptr->thread_core_num;
            poll_thread_state_ptr->data_type = config_data_ptr->data_type;
            poll_thread_state_ptr->is_poll = (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll);
            if (!adapter_con_state_ptr->can_receive) {
                poll_thread_state_ptr->only_transmit = true;
            }
            CdiListInit(&poll_thread_state_ptr->connection_list);

            if (!CdiOsSignalCreate(&poll_thread_state_ptr->connection_list_changed_signal)) {
                rs = kCdiStatusNotEnoughMemory;
            } else if (!CdiOsSignalCreate(&poll_thread_state_ptr->connection_list_processed_signal)) {
                rs = kCdiStatusNotEnoughMemory;
            } else if (!CdiOsCritSectionCreate(&poll_thread_state_ptr->connection_list_lock)) {
                rs = kCdiStatusNotEnoughMemory;
            } else if (!CdiOsSignalCreate(&poll_thread_state_ptr->start_signal)) {
                rs = kCdiStatusNotEnoughMemory;
            } else {
                // Add the connection to the poll thread state data so when the thread starts running it will have a
                // connection to use.
                PollThreadConnectionAdd(poll_thread_state_ptr, adapter_con_state_ptr);

                // Create poll worker thread.
                if (!CdiOsThreadCreatePinned(PollThread, &poll_thread_state_ptr->thread_id, thread_name_str,
                                            poll_thread_state_ptr, poll_thread_state_ptr->start_signal,
                                            config_data_ptr->thread_core_num)) {
                    rs = kCdiStatusCreateThreadFailed;
                }
            }

            if (kCdiStatusOk == rs) {
                // Add poll thread state data to list held by adapter.
                // NOTE: Must have acquired adapter_lock before using poll_thread_list.
                CdiListAddTail(&config_data_ptr->cdi_adapter_handle->poll_thread_list, &poll_thread_state_ptr->list_entry);
            } else {
                PollThreadDestroy(poll_thread_state_ptr, adapter_con_state_ptr->shutdown_signal);
                poll_thread_state_ptr = NULL;
            }
        }
    }

    if (kCdiStatusOk == rs) {
        if (adapter_con_state_ptr->can_receive) {
            adapter_con_state_ptr->rx_state = config_data_ptr->rx_state;
        }

        adapter_con_state_ptr->port_number = config_data_ptr->port_number;

        // Set this prior to opening the endpoint. Receive packets may start flowing before Open() returns and the
        // connection must have a valid endpoint pointer set.
        *return_handle_ptr = adapter_con_state_ptr;

        // Do adapter specific open connection actions. NOTE> This will also start the poll-thread if it was just
        // created above.
        rs = config_data_ptr->cdi_adapter_handle->functions_ptr->CreateConnection(adapter_con_state_ptr,
                                                                                  config_data_ptr->port_number);
    }


    if (kCdiStatusOk != rs) {
        CdiAdapterDestroyConnection(adapter_con_state_ptr);
        adapter_con_state_ptr = NULL;
        *return_handle_ptr = NULL;
    }

    CdiOsCritSectionRelease(config_data_ptr->cdi_adapter_handle->adapter_lock);

    return rs;
}

CdiReturnStatus CdiAdapterStopConnection(AdapterConnectionHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    AdapterConnectionState* adapter_con_state_ptr = (AdapterConnectionState*)handle;
    if (adapter_con_state_ptr) {
        PollThreadConnectionRemove(adapter_con_state_ptr);
    }

    return kCdiStatusOk;
}

CdiReturnStatus CdiAdapterDestroyConnection(AdapterConnectionHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    AdapterConnectionState* adapter_con_state_ptr = (AdapterConnectionState*)handle;

    if (adapter_con_state_ptr) {
        CdiOsCritSectionReserve(adapter_con_state_ptr->adapter_state_ptr->adapter_lock);

        // Ensure connection has been stopped.
        rs = CdiAdapterStopConnection(handle);

        adapter_con_state_ptr->adapter_state_ptr->functions_ptr->DestroyConnection(adapter_con_state_ptr);

        CdiOsCritSectionRelease(adapter_con_state_ptr->adapter_state_ptr->adapter_lock);

        // Now that the threads have stopped, it is safe to clean up the remaining resources.
        CdiOsSignalDelete(adapter_con_state_ptr->tx_poll_do_work_signal);
        CdiOsSignalDelete(adapter_con_state_ptr->shutdown_signal);

        CdiOsCritSectionDelete(adapter_con_state_ptr->endpoint_lock);
        adapter_con_state_ptr->endpoint_lock = NULL;

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
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&endpoint_state_ptr->start_signal)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiOsSignalCreate(&endpoint_state_ptr->shutdown_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Link endpoint to its adapter, queue message function and log.
        endpoint_state_ptr->adapter_con_state_ptr = adapter_con_state_ptr;
        endpoint_state_ptr->cdi_endpoint_handle = config_data_ptr->cdi_endpoint_handle;
        endpoint_state_ptr->msg_from_endpoint_func_ptr = config_data_ptr->msg_from_endpoint_func_ptr;
        endpoint_state_ptr->msg_from_endpoint_param_ptr = config_data_ptr->msg_from_endpoint_param_ptr;

        if (kEndpointDirectionSend == adapter_con_state_ptr->direction ||
            kEndpointDirectionBidirectional == adapter_con_state_ptr->direction) {
            // If the adapter does not support poll mode, then use a queue that supports a wait signal so PollThread()
            // can sleep whenever the queue is empty.
            bool is_poll = (NULL != adapter_con_state_ptr->adapter_state_ptr->functions_ptr->Poll);
            CdiQueueSignalMode queue_signal_mode = (is_poll) ? kQueueSignalNone : kQueueSignalPopWait;

            if (kCdiStatusOk == rs) {
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

    if (kCdiStatusOk == rs) {
        // Set this prior to opening the endpoint. Receive packets may start flowing before Open() returns and the
        // connection must have a valid endpoint pointer set.
        *return_handle_ptr = endpoint_state_ptr;

        endpoint_state_ptr->endpoint_stats_ptr = config_data_ptr->endpoint_stats_ptr;

        // Do adapter specific open actions.
        CdiAdapterState* adapter_state_ptr = config_data_ptr->connection_handle->adapter_state_ptr;
        rs = adapter_state_ptr->functions_ptr->Open(endpoint_state_ptr, config_data_ptr->remote_address_str,
                                                    config_data_ptr->port_number);
    }

    if (kCdiStatusOk != rs) {
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
        if (handle->adapter_con_state_ptr->poll_thread_state_ptr &&
            handle->adapter_con_state_ptr->poll_thread_state_ptr->start_signal) {
            CdiOsSignalSet(handle->adapter_con_state_ptr->poll_thread_state_ptr->start_signal);
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

CdiReturnStatus CdiAdapterResetEndpoint(AdapterEndpointHandle handle, bool reopen)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (handle) {
        CdiAdapterState* adapter_state_ptr = handle->adapter_con_state_ptr->adapter_state_ptr;
        if (adapter_state_ptr->functions_ptr->Reset) {
            rs = adapter_state_ptr->functions_ptr->Reset(handle, reopen);
        }
        if (handle->adapter_con_state_ptr->tx_poll_do_work_signal) {
            if (handle->tx_in_flight_ref_count) {
                CDI_LOG_THREAD(kLogWarning, "Resetting endpoint while [%d] Tx packets in flight", handle->tx_in_flight_ref_count);
            }
            CdiOsAtomicStore32(&handle->tx_in_flight_ref_count, 0);
            CdiOsSignalClear(handle->adapter_con_state_ptr->tx_poll_do_work_signal);
        }
    } else {
        rs = kCdiStatusInvalidHandle;
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

        if (adapter_con_state_ptr && adapter_con_state_ptr->adapter_state_ptr) {
            CdiAdapterState* adapter_state_ptr = adapter_con_state_ptr->adapter_state_ptr;
            rs = adapter_state_ptr->functions_ptr->Close(handle);
        }

        CdiQueueDestroy(handle->tx_packet_queue_handle);
        CdiOsSignalDelete(handle->shutdown_signal);
        CdiOsSignalDelete(handle->start_signal);
        ProtocolVersionDestroy(handle->protocol_handle);
        CdiOsMemFree(handle);
    }

    return rs;
}

EndpointTransmitQueueLevel CdiAdapterGetTransmitQueueLevel(AdapterEndpointHandle handle)
{
    return handle->adapter_con_state_ptr->adapter_state_ptr->functions_ptr->GetTransmitQueueLevel(handle);
}

CdiReturnStatus CdiAdapterEnqueueSendPackets(const AdapterEndpointHandle handle,
                                             const CdiSinglyLinkedList* packet_list_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    assert(handle->adapter_con_state_ptr->direction == kEndpointDirectionSend ||
           handle->adapter_con_state_ptr->direction == kEndpointDirectionBidirectional);

    // Add the packet message into the Tx packet queue for processing by the PollThread() thread.
    if (!CdiQueuePush(handle->tx_packet_queue_handle, packet_list_ptr)) {
        BACK_PRESSURE_ERROR(handle->cdi_endpoint_handle->connection_state_ptr->back_pressure_state,
                            kLogInfo, "Tx packet queue[%s] full.", CdiQueueGetName(handle->tx_packet_queue_handle));
        rs = kCdiStatusQueueFull;
    }

    return rs;
}

CdiReturnStatus CdiAdapterEnqueueSendPacket(const AdapterEndpointHandle handle,
                                            const struct sockaddr_in* destination_address_ptr, Packet* packet_ptr)
{
    CdiSinglyLinkedList packet_list;
    CdiSinglyLinkedListInit(&packet_list);
    CdiSinglyLinkedListPushTail(&packet_list, &packet_ptr->list_entry);
    packet_ptr->socket_adapter_state.address = *destination_address_ptr;
    return CdiAdapterEnqueueSendPackets(handle, &packet_list);
}

CdiReturnStatus CdiAdapterFreeBuffer(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    AdapterConnectionState* adapter_con_state_ptr = handle->adapter_con_state_ptr;
    CdiAdapterState* adapter_state_ptr = adapter_con_state_ptr->adapter_state_ptr;
    if (adapter_con_state_ptr->direction == kEndpointDirectionSend) {
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

void CdiAdapterTxPacketComplete(AdapterEndpointHandle handle, const Packet* packet_ptr)
{
    // Decrement in-flight count whenever a packet has been ACKed. Decrement additionally once when the last packet of a
    // payload has been ACKed.
    assert(0 != CdiOsAtomicLoad32(&handle->tx_in_flight_ref_count));
    CdiOsAtomicDec32(&handle->tx_in_flight_ref_count);

    if (packet_ptr->payload_last_packet) {
        // Decrement counter again for last packet.
        assert(0 != CdiOsAtomicLoad32(&handle->tx_in_flight_ref_count));
        CdiOsAtomicDec32(&handle->tx_in_flight_ref_count);
    }
}
