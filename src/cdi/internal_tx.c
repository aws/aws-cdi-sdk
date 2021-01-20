// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used with the SDK that is not part of the API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "internal_tx.h"

#include <string.h>

#include "cdi_queue_api.h"
#include "endpoint_manager.h"
#include "internal.h"
#include "payload.h"
#include "private.h"
#include "statistics.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Return the next payload number to use for the specified connection. This is an incrementing value.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 *
 * @return The payload number.
 */
static uint8_t GetNextPayloadNum(CdiEndpointState* endpoint_ptr)
{
    CdiOsCritSectionReserve(endpoint_ptr->tx_state.payload_num_lock);
    uint8_t payload_num = endpoint_ptr->tx_state.payload_num++;
    CdiOsCritSectionRelease(endpoint_ptr->tx_state.payload_num_lock);

    return payload_num;
}

#ifdef DEBUG_TX_PACKET_SGL_ENTRIES
/**
 * Dump Tx packet SGL entries to log or stdout.
 *
 * @param work_request_ptr Pointer to work request state data.
 */
static void DebugTxPacketSglEntries(TxPacketWorkRequest* work_request_ptr)
{
    CdiLogMultilineState m_state;
    CDI_LOG_THREAD_MULTILINE_BEGIN(kLogInfo, &m_state);

    CdiCDIPacketCommonHeader* common_header_ptr =
        (CdiCDIPacketCommonHeader*)work_request_ptr->packet.sg_list.sgl_head_ptr->address_ptr;

    // The payload_data_offset value is not used for packet sequence number zero, since the offset is always
    // zero.
    if (0 != common_header_ptr->packet_sequence_num &&
        kPayloadTypeDataOffset == common_header_ptr->payload_type) {
        CdiCDIPacketDataOffsetHeader *ptr = (CdiCDIPacketDataOffsetHeader*)common_header_ptr;
        CDI_LOG_MULTILINE(&m_state, "Tx Total Packet Size[%d]. Packet Type[%d] Packet[%d] Payload[%d] Offset[%d] Entries:",
                          work_request_ptr->packet.sg_list.total_data_size, ptr->hdr.payload_type,
                          ptr->hdr.packet_sequence_num, ptr->hdr.payload_num, ptr->payload_data_offset);
    } else {
        CDI_LOG_MULTILINE(&m_state, "Tx Total Packet Size[%d]. Packet Type[%d] Packet[%d] Payload[%d] Entries:",
                          work_request_ptr->packet.sg_list.total_data_size, common_header_ptr->payload_type,
                          common_header_ptr->packet_sequence_num, common_header_ptr->payload_num);
    }

    CdiSglEntry *packet_entry_ptr = work_request_ptr->packet.sg_list.sgl_head_ptr;
    while (packet_entry_ptr) {
        CDI_LOG_MULTILINE(&m_state, "Size[%4d] Addr[%p]", packet_entry_ptr->size_in_bytes,
                            packet_entry_ptr->address_ptr);
        packet_entry_ptr = packet_entry_ptr->next_ptr;
    }
    CDI_LOG_MULTILINE_END(&m_state);
}
#endif

/**
 * Pop all items in the work request completion queue freeing resources associated with each one.
 *
 * @param con_state_ptr Pointer to connection state data.
 */
static void ProcessWorkRequestCompletionQueue(CdiConnectionState* con_state_ptr)
{
    CdiSinglyLinkedList packet_list = { 0 };
    while (CdiQueuePop(con_state_ptr->tx_state.work_req_comp_queue_handle, (void*)&packet_list)) {
        // Free resources used by the packets that are no longer needed.

        for (void* item_ptr = CdiSinglyLinkedListPopHead(&packet_list) ; NULL != item_ptr ;
             item_ptr = CdiSinglyLinkedListPopHead(&packet_list)) {
            Packet* packet_ptr = CONTAINER_OF(item_ptr, Packet, list_entry);
            TxPacketWorkRequest* work_request_ptr = (TxPacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;

            CdiSglEntry* packet_entry_hdr_ptr = work_request_ptr->packet.sg_list.sgl_head_ptr;
#ifdef USE_MEMORY_POOL_APPENDED_LISTS
            // Since we used CdiPoolGetAppend(), all the pool entries are linked to the first entry and are freed with a
            // single call to CdiPoolPut().
            CdiPoolPut(con_state_ptr->tx_state.packet_sgl_entry_pool_handle, packet_entry_hdr_ptr);
#else
            // Put back SGL entry for each one in the list.
            FreeSglEntries(con_state_ptr->tx_state.packet_sgl_entry_pool_handle, packet_entry_hdr_ptr);
#endif

            // Put back work request into the pool.
            // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
            CdiPoolPut(con_state_ptr->tx_state.work_request_pool_handle, work_request_ptr);
        }
    }
}

/**
 * Payload thread used to transmit a payload.
 *
 * @param ptr Pointer to thread specific data. In this case, a pointer to CdiConnectionState.
 *
 * @return The return value is not used.
 */
static THREAD TxPayloadThread(void* ptr)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)ptr;

    // Get a state tracker object for the packetizer.
    CdiPacketizerStateHandle packetizer_state_handle = CdiPacketizerStateCreate();
    if (NULL == packetizer_state_handle) {
        CDI_LOG_THREAD(kLogError, "Failed to create packetizer state.");
        return 0;
    }

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(con_state_ptr->log_handle);

    EndpointManagerHandle mgr_handle = con_state_ptr->endpoint_manager_handle;

    // Register this thread with the Endpoint Manager as being part of this connection.
    CdiSignalType notification_signal = EndpointManagerThreadRegister(mgr_handle,
                                            CdiOsThreadGetName(con_state_ptr->payload_thread_id));

    CdiSignalType comp_queue_signal = CdiQueueGetPopWaitSignal(con_state_ptr->tx_state.work_req_comp_queue_handle);

    CdiSignalType signal_array[2];
    signal_array[0] = notification_signal;
    signal_array[1] = comp_queue_signal;

    // Packets are sent to the endpoint in batches starting with a single packet. The number is doubled with each
    // batch. This gives a quick start but as the queue backs up, the larger batch sizes lead to higher efficiency
    // per batch.
    int batch_size = 1;

    // These variable are used only within the scope of the while loop below but they must be declared outside of it
    // since their values need to start initialized but not get reinitialized every time through.
    bool last_packet = false;
    TxPacketWorkRequest* work_request_ptr = NULL;
    CdiSinglyLinkedList packet_list;
    CdiSinglyLinkedListInit(&packet_list);

    // The state machine goes through the states like:
    //
    //   +-----> idle -+
    //   |             |
    //   |     +-------+
    //   |     |
    //   |     +-> work received ->+
    //   |                         |
    //   |     +-------------------+
    //   |     |
    //   |  +->+-> get work request ->+
    //   |  |                         |
    //   |  |     +-------------------+
    //   |  |     |
    //   |  |     +-> packetizing ->+
    //   |  |                       |
    //   |  +<----------------------+  <-- list of packets to enqueue is incomplete
    //   |  ^                       |
    //   |  |  +--------------------+  <-- list of packets to enqueue is complete
    //   |  |  |
    //   |  |  +-> enqueueing ->+
    //   |  |                   |
    //   |  +-------------------+  <-- not last packet of payload
    //   |                      |
    //   +----------------------+  <-- last packet of the payload has been successfully queued
    enum {
        kPayloadStateIdle,           // No payload is in process: wait for payload from queue.
        kPayloadStateWorkReceived,   // A payload was received to be transmitted: initialize for first packet.
        kPayloadStateGetWorkRequest, // Payload and packetizer initialized: get a work request from pool.
        kPayloadStatePacketizing,    // Have work request: build SGL.
        kPayloadStateEnqueuing       // Have completed list of work requests: queued to the adapter.
    } payload_processing_state = kPayloadStateIdle;

    // This loop should only block at the call to CdiQueuePopWaitMultiple(). If a pool runs dry or the output queue is
    // full, the logic inside of the loop should maintain enough state to suspend the process of packetizing the current
    // payload and resume when resources are available.
    TxPayloadState* payload_state_ptr = NULL;
    while (!CdiOsSignalGet(con_state_ptr->shutdown_signal) && !EndpointManagerIsConnectionShuttingDown(mgr_handle)) {

        // If connected and queue is empty, then clear enqueue active flag so PollThread() can sleep. While not
        // connected, Probe controls use of the do_work flag.
        if (kCdiConnectionStatusConnected == con_state_ptr->adapter_connection_ptr->connection_status_code &&
            CdiQueueIsEmpty(con_state_ptr->tx_state.payload_queue_handle)) {
            CdiOsSignalClear(con_state_ptr->adapter_connection_ptr->poll_do_work_signal);
        }

        uint32_t signal_index = 0;
        bool payload_received = false;
        if (kPayloadStateIdle == payload_processing_state) {
            // Wait for work from the payload queue, the work request complete queue, or a signal from the endpoint
            // manager.
            payload_received = CdiQueuePopWaitMultiple(con_state_ptr->tx_state.payload_queue_handle, CDI_INFINITE,
                                                       signal_array, 2, &signal_index, (void**)&payload_state_ptr);
        } else {
            // A payload is currently in process. Wait for completion requests or a signal from the Endpoint Manager.
            CdiOsSignalsWait(signal_array, 2, false, CDI_INFINITE, &signal_index);
        }
        if (!payload_received) {
            // Either processing an existing payload or did not get a new one. Got a signal from either the Endpoint
            // Manager or work_req_comp_queue_handle (the queue contains data).
            if (0 == signal_index) {
                // Got a notification_signal. The endpoint state has changed, so wait until it has completed.
                EndpointManagerThreadWait(mgr_handle);
                // An Endpoint Manager state change means that Tx resources have been flushed or queued to be flushed,
                // including the current Tx payload that we could be processing. Reset our current payload state back to
                // idle. Allow the logic to drop below so if needed ProcessWorkRequestCompletionQueue() is invoked.
                payload_processing_state = kPayloadStateIdle;
                payload_state_ptr = NULL;
            }
        } else {
            payload_processing_state = kPayloadStateWorkReceived;
        }

        // Always check the completion queue here. Don't want to starve it in case either several Endpoint Manager
        // notifications are received or the payload_queue_handle doesn't go empty.
        if (CdiOsSignalReadState(comp_queue_signal)) {
            ProcessWorkRequestCompletionQueue(con_state_ptr);
        }

        // Either resume work on a payload in progress or start a new one.
        if (kPayloadStateWorkReceived == payload_processing_state) {
            // No packet was in progress so start by initializing for the first one.

            // Increment payload number. NOTE: This is done here on the read side of the queue rather than on the write
            // side of the queue because the write side fails if the queue is full. This would cause payload_num to
            // increment erroneously. By incrementing here on the read side, this problem is avoided.
            payload_state_ptr->payload_packet_state.payload_num =
                GetNextPayloadNum(payload_state_ptr->cdi_endpoint_handle);

            if (CdiLogComponentIsEnabled(con_state_ptr, kLogComponentPayloadConfig)) {
                // Dump payload configuration to log or stdout.
                DumpPayloadConfiguration(&payload_state_ptr->app_payload_cb_data.core_extra_data,
                                         payload_state_ptr->app_payload_cb_data.extra_data_size,
                                         payload_state_ptr->app_payload_cb_data.extra_data_array,
                                         con_state_ptr->protocol_type);
            }

            // Set flag/signal that we are going to start queueing a payload of packets. This will keep the PollThread()
            // working as long as we have these packets and more payloads in payload_queue_handle to send.
            CdiOsSignalSet(con_state_ptr->adapter_connection_ptr->poll_do_work_signal);

            // Prepare packetizer for first packet.
            CdiPacketizerStateInit(packetizer_state_handle);

            CdiSinglyLinkedListInit(&packet_list);
            batch_size = 1;
            last_packet = false;

            payload_processing_state = kPayloadStateGetWorkRequest;  // Advance the state machine.
        }

        bool keep_going = kPayloadStateGetWorkRequest == payload_processing_state ||
                          kPayloadStatePacketizing == payload_processing_state ||
                          kPayloadStateEnqueuing == payload_processing_state;
        while (keep_going) {
            // When the connection goes down, no need to use resources to continue creating packets or adding them to
            // the adapter's queue. If the adapter's queue gets full it will start generating queue full log message
            // errors.
            AdapterEndpointHandle adapter_endpoint_handle =
                EndpointManagerEndpointToAdapterEndpoint(payload_state_ptr->cdi_endpoint_handle);
            if (kCdiConnectionStatusConnected != adapter_endpoint_handle->connection_status_code) {
                break;
            }
            if (kPayloadStateGetWorkRequest == payload_processing_state) {
                // NOTE: This pool is not thread-safe, so must ensure that only one thread is accessing it at a time.
                if (!CdiPoolGet(con_state_ptr->tx_state.work_request_pool_handle, (void**)&work_request_ptr)) {
                    keep_going = false;
                } else {
                    payload_processing_state = kPayloadStatePacketizing;
                }
            }

            if (kPayloadStatePacketizing == payload_processing_state) {
                // NOTE: These pools are not thread-safe, so must ensure that only one thread is accessing them at a time.
                if (!CdiPayloadGetPacket(packetizer_state_handle,
                                         &work_request_ptr->header,
                                         con_state_ptr->tx_state.packet_sgl_entry_pool_handle,
                                         payload_state_ptr,
                                         &work_request_ptr->packet.sg_list,
                                         &last_packet))
                {
                    // Pool is empty; suspend processing the payload for now, retry after resources are freed.
                    keep_going = false;
                } else {
#ifdef DEBUG_TX_PACKET_SGL_ENTRIES
                    DebugTxPacketSglEntries(work_request_ptr);
#endif
                    // Fill in the work request with the specifics of the packet.
                    work_request_ptr->payload_state_ptr = payload_state_ptr;
                    work_request_ptr->payload_num = payload_state_ptr->payload_packet_state.payload_num;
                    work_request_ptr->packet_sequence_num = payload_state_ptr->payload_packet_state.packet_sequence_num - 1;
                    work_request_ptr->packet_payload_size =
                        payload_state_ptr->payload_packet_state.packet_payload_data_size;

                    // This pointer will be used later by TxPacketWorkRequestComplete() to get access to work_request_ptr (a
                    // pointer to a TxPacketWorkRequest structure).
                    work_request_ptr->packet.sg_list.internal_data_ptr = work_request_ptr;

                    // Add the packet to a list to be enqueued to the adapter.
                    CdiSinglyLinkedListPushTail(&packet_list, &work_request_ptr->packet.list_entry);

                    payload_processing_state = (last_packet || CdiSinglyLinkedListSize(&packet_list) >= batch_size) ?
                        kPayloadStateEnqueuing : kPayloadStateGetWorkRequest;
                }
            }

            if (kPayloadStateEnqueuing == payload_processing_state) {
                // Enqueue packets. packet_list is copied so it can simply be initialized here to start fresh.
                if (kCdiStatusOk != CdiAdapterEnqueueSendPackets(
                        EndpointManagerEndpointToAdapterEndpoint(payload_state_ptr->cdi_endpoint_handle),
                        &packet_list)) {
                    keep_going = false;
                } else {
                    CdiSinglyLinkedListInit(&packet_list);
                    batch_size *= 2;

                    if (last_packet) {
                        // The last packet of the payload has been sent; reset to start a new one.
                        payload_processing_state = kPayloadStateIdle;
                        payload_state_ptr = NULL;
                        keep_going = false;
                    } else {
                        payload_processing_state = kPayloadStateGetWorkRequest;
                    }
                }
            }
        }
    }

    CdiPacketizerStateDestroy(packetizer_state_handle);
    if (EndpointManagerIsConnectionShuttingDown(mgr_handle)) {
        // Since this thread was registered with the Endpoint Manager using EndpointManagerThreadRegister(), need to
        // wait for the Endpoint Manager to complete the shutdown.
        EndpointManagerThreadWait(mgr_handle);
    }

    return 0; // Return code not used.
}

/**
 * Create an instance of a connection.
 *
 * @param protocol_type Connection protocol type.
 * @param config_data_ptr Pointer to transmitter configuration data.
 * @param tx_cb_ptr Address of the user function to call whenever a payload being transmitted has been received by the
 *                  receiver.
 * @param ret_handle_ptr Pointer to returned connection handle.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus TxCreateConnection(ConnectionProtocolType protocol_type, CdiTxConfigData* config_data_ptr,
                                          CdiCallback tx_cb_ptr, CdiConnectionHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    int max_tx_payloads = MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION;

    // If max_simultaneous_tx_payloads has been set use that value otherwise use
    // MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION
    if (config_data_ptr->max_simultaneous_tx_payloads) {
        max_tx_payloads = config_data_ptr->max_simultaneous_tx_payloads;
    }

    int max_tx_payload_sgl_entries = MAX_SIMULTANEOUS_TX_PAYLOAD_SGL_ENTRIES_PER_CONNECTION;

    // If max_simultaneous_tx_payload_sgl_entries has been set use that value otherwise use
    // MAX_SIMULTANEOUS_TX_PAYLOAD_SGL_ENTRIES_PER_CONNECTION
    if (config_data_ptr->max_simultaneous_tx_payload_sgl_entries) {
        max_tx_payload_sgl_entries = config_data_ptr->max_simultaneous_tx_payload_sgl_entries;
    }

    CdiConnectionState* con_state_ptr = (CdiConnectionState*)CdiOsMemAllocZero(sizeof *con_state_ptr);
    if (con_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        con_state_ptr->adapter_state_ptr = (CdiAdapterState*)config_data_ptr->adapter_handle;
        con_state_ptr->handle_type = kHandleTypeTx;
        con_state_ptr->protocol_type = protocol_type;
        con_state_ptr->magic = kMagicCon;

        // Make a copy of the configuration data.
        memcpy(&con_state_ptr->tx_state.config_data, config_data_ptr, sizeof *config_data_ptr);

        // Make a copy of configuration data strings and update the copy of the config data to use them. NOTE: The
        // connection_name_str is updated in logic below (see saved_connection_name_str).
        if (config_data_ptr->dest_ip_addr_str) {
            CdiOsStrCpy(con_state_ptr->tx_state.copy_dest_ip_addr_str,
                        sizeof(con_state_ptr->tx_state.copy_dest_ip_addr_str), config_data_ptr->dest_ip_addr_str);
            con_state_ptr->tx_state.config_data.dest_ip_addr_str = con_state_ptr->tx_state.copy_dest_ip_addr_str;
        }

        // Save callback address.
        con_state_ptr->tx_state.cb_ptr = tx_cb_ptr;
    }
    // Now that we have a connection logger, we can use the CDI_LOG_HANDLE() macro to add log messages to it. Since this
    // thread is from the application, we cannot use the CDI_LOG_THEAD() macro.

    // This log will be used by all the threads created for this connection.
    if (kCdiStatusOk == rs) {
        if (kLogMethodFile == config_data_ptr->connection_log_method_data_ptr->log_method) {
            CDI_LOG_HANDLE(cdi_global_context.global_log_handle, kLogInfo, "Setting log file[%s] for SDK Tx logging.",
                             config_data_ptr->connection_log_method_data_ptr->log_filename_str);
        }
        if (!CdiLoggerCreateLog(cdi_global_context.logger_handle, con_state_ptr,
                                config_data_ptr->connection_log_method_data_ptr, &con_state_ptr->log_handle)) {
            rs = kCdiStatusCreateLogFailed;
        }
    }

    // Copy the name for the connection from the config data or generate one. NOTE: Do this here, since other logic
    // below uses the saved name.
    if ((NULL == config_data_ptr->connection_name_str) || (0 == strlen(config_data_ptr->connection_name_str))) {
        if (NULL == config_data_ptr->dest_ip_addr_str) {
            snprintf(con_state_ptr->saved_connection_name_str, sizeof(con_state_ptr->saved_connection_name_str), "%s:%d",
                     "unknown_ip", config_data_ptr->dest_port);
        } else {
            snprintf(con_state_ptr->saved_connection_name_str, sizeof(con_state_ptr->saved_connection_name_str), "%s:%d",
                     config_data_ptr->dest_ip_addr_str, config_data_ptr->dest_port);
        }

        config_data_ptr->connection_name_str = con_state_ptr->saved_connection_name_str;

        CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo, "Tx connection is unnamed. Created name[%s]",
                       con_state_ptr->saved_connection_name_str);
    } else {
        CdiOsStrCpy(con_state_ptr->saved_connection_name_str, sizeof(con_state_ptr->saved_connection_name_str),
                    config_data_ptr->connection_name_str);
    }
    // Update copy of config data to use the saved connection string.
    con_state_ptr->tx_state.config_data.connection_name_str = con_state_ptr->saved_connection_name_str;

    if (kCdiStatusOk == rs) {
        CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogInfo,
                       "Creating Tx connection. Protocol[%s] Destination IP[%s] Destination Port[%d] Name[%s]",
                       CdiUtilityKeyEnumToString(kKeyConnectionProtocolType, protocol_type),
                       con_state_ptr->tx_state.config_data.dest_ip_addr_str,
                       con_state_ptr->tx_state.config_data.dest_port,
                       CdiGetEmptyStringIfNull(con_state_ptr->tx_state.config_data.connection_name_str));
    }

    if (kCdiStatusOk == rs) {
        rs = ConnectionCommonResourcesCreate(con_state_ptr, config_data_ptr->stats_cb_ptr,
                                             config_data_ptr->stats_user_cb_param, &config_data_ptr->stats_config);
    }

    if (kCdiStatusOk == rs) {
        // Create queue used to hold Tx payload messages that are sent to the TxPayloadThread() thread. Depth must be
        // less than the number of TX payloads allowed per connection to allow for proper pushback and payload state
        // data management.
        if (!CdiQueueCreate("TxPayloadState queue Pointer", max_tx_payloads-1,
                            FIXED_QUEUE_SIZE, FIXED_QUEUE_SIZE, sizeof(TxPayloadState*),
                            kQueueSignalPopWait | kQueueMultipleWritersFlag, // Can use wait signal for pops (reads),
                                                                             // thread safe for multiple writers.
                            &con_state_ptr->tx_state.payload_queue_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create worker thread.
        if (!CdiOsThreadCreate(TxPayloadThread, &con_state_ptr->payload_thread_id, "TxPayload", con_state_ptr,
                                con_state_ptr->start_signal)) {
            rs = kCdiStatusFatal;
        }
    }

    // Create memory pools. NOTE: These pools do not use any resource locks and are therefore not thread-safe.
    // TxPayloadThread() is the only user of the pools, except when restarting/shutting down the connection which is
    // done by EndpointManagerThread() while TxPayloadThread() is blocked.
    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Connection Tx TxPacketWorkRequest Pool", MAX_TX_PACKET_WORK_REQUESTS_PER_CONNECTION,
                           MAX_TX_PACKET_WORK_REQUESTS_PER_CONNECTION_GROW, MAX_POOL_GROW_COUNT,
                           sizeof(TxPacketWorkRequest), false, // false= Not thread-safe (no resource locks)
                           &con_state_ptr->tx_state.work_request_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Connection Tx CdiSglEntry Pool", TX_PACKET_SGL_ENTRY_SIZE_PER_CONNECTION, NO_GROW_SIZE,
                           NO_GROW_COUNT, sizeof(CdiSglEntry), false, // false= Not thread-safe (no resource locks)
                           &con_state_ptr->tx_state.packet_sgl_entry_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        // There is a limit on the number of simultaneous Tx payloads per connection, so don't allow this pool to grow.
        if (!CdiPoolCreate("Connection Tx Payload State Pool", max_tx_payloads,
                           NO_GROW_SIZE, NO_GROW_COUNT, sizeof(TxPayloadState), true, // true= Is thread-safe.
                           &con_state_ptr->tx_state.payload_state_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }
    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Connection Tx Payload CdiSglEntry Pool",
                           max_tx_payload_sgl_entries, NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(CdiSglEntry), true, // true= Is thread-safe.
                           &con_state_ptr->tx_state.payload_sgl_entry_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiQueueCreate("Connection Tx TxPacketWorkRequest* Queue", MAX_TX_PACKETS_PER_CONNECTION,
                            TX_PACKET_POOL_SIZE_GROW, MAX_POOL_GROW_COUNT,
                            sizeof(CdiSinglyLinkedList), kQueueSignalPopWait, // Make a blockable reader.
                            &con_state_ptr->tx_state.work_req_comp_queue_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create a packet message thread that is used by both Tx and Rx connections.
        rs = ConnectionCommonPacketMessageThreadCreate(con_state_ptr);
    }

    if (kCdiStatusOk == rs) {
        CdiAdapterConnectionConfigData config_data = {
            .cdi_adapter_handle = con_state_ptr->adapter_state_ptr,
            .cdi_connection_handle = con_state_ptr,
            .endpoint_manager_handle = con_state_ptr->endpoint_manager_handle,

            .connection_cb_ptr = con_state_ptr->tx_state.config_data.connection_cb_ptr,
            .connection_user_cb_param = con_state_ptr->tx_state.config_data.connection_user_cb_param,

            .log_handle = con_state_ptr->log_handle,
            .thread_core_num = config_data_ptr->thread_core_num,
            .direction = kEndpointDirectionSend,
            .port_number = con_state_ptr->tx_state.config_data.dest_port,

            // This endpoint is used for normal data transmission (not used for control). This means that the Endpoint
            // Manager is used for managing threads related to the connection.
            .data_type = kEndpointTypeData,
        };
        if (kCdiStatusOk != CdiAdapterCreateConnection(&config_data, &con_state_ptr->adapter_connection_ptr)) {
            rs = kCdiStatusFatal;
        }
    }

    if (kCdiStatusOk != rs) {
        ConnectionDestroyInternal((CdiConnectionHandle)con_state_ptr);
        con_state_ptr = NULL;
    }

    *ret_handle_ptr = (CdiConnectionHandle)con_state_ptr;

    return rs;
}

/**
 * Try to find an endpoint with the specified stream ID.
 *
 * @param handle Handle of Endpoint Manager to search for a matching endpoint.
 * @param stream_identifier Stream ID to use.
 *
 * @return If found, returns a pointer to CDI endpoint state data, otherwise NULL is returned.
 */
static CdiEndpointState* FindEndpoint(EndpointManagerHandle handle, int stream_identifier)
{
    CdiEndpointState* ret_endpoint_state_ptr = NULL;

    // Walk through the endpoint list and try to find the endpoint associated with the stream identifier.
    CdiEndpointHandle endpoint_handle = EndpointManagerGetFirstEndpoint(handle);
    while (endpoint_handle) {
        if (stream_identifier == endpoint_handle->stream_identifier ||
            STREAM_IDENTIFIER_NOT_USED == endpoint_handle->stream_identifier ||
            STREAM_IDENTIFIER_NOT_USED == stream_identifier) {
            // Found the matching endpoint, so setup to return it and break out of this loop.
            ret_endpoint_state_ptr = endpoint_handle;
            break;
        }
        endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
    }

    return ret_endpoint_state_ptr;
}

/**
 * Payload transfer has completed either successfully or in error. Update stats and queue a payload message to
 * the application.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param payload_state_ptr Pointer to payload state data. The pointer is no longer valid after function returns.
 */
static void PayloadTransferComplete(CdiEndpointState* endpoint_ptr, TxPayloadState* payload_state_ptr)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)endpoint_ptr->connection_state_ptr;

    StatsGatherPayloadStatsFromConnection(endpoint_ptr,
        kCdiStatusOk == payload_state_ptr->app_payload_cb_data.payload_status_code,
        payload_state_ptr->start_time, payload_state_ptr->max_latency_microsecs,
        payload_state_ptr->data_bytes_transferred);

    // Copy the payload's source SGL to the callback data, so we can free the SGL entries in AppCallbackPayloadThread()
    // to reduce the amount of work required here by the Tx Poll() thread. This also allows the payload_state_ptr to
    // be freed in this function, since it is no longer needed.
    payload_state_ptr->app_payload_cb_data.tx_source_sgl = payload_state_ptr->source_sgl;

    // Post message to notify application that payload transfer has completed.
    if (!CdiQueuePush(con_state_ptr->app_payload_message_queue_handle, &payload_state_ptr->app_payload_cb_data)) {
        CDI_LOG_THREAD(kLogError, "Queue[%s] full, push failed.",
                        CdiQueueGetName(con_state_ptr->app_payload_message_queue_handle));
    }

    // Done with payload state data, so free it.
    CdiPoolPut(con_state_ptr->tx_state.payload_state_pool_handle, payload_state_ptr);
}

/**
 * Flush a payload that did not complete transferring. This will set the payload's status and queue a payload message
 * to the application.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param payload_state_ptr Pointer to payload state data. The pointer is not longer valid after function returns.
 */
static void FlushFailedPayload(CdiEndpointState* endpoint_ptr, TxPayloadState* payload_state_ptr)
{
    if (kCdiStatusOk == payload_state_ptr->app_payload_cb_data.payload_status_code) {
        payload_state_ptr->app_payload_cb_data.payload_status_code = kCdiStatusSendFailed;
    }

    // Clear this list. It will be cleared by TxPayloadThreadFlushResources(). See packet_sgl_entry_pool_handle pool.
    CdiSinglyLinkedListInit(&payload_state_ptr->completed_packets_list);

    // Queue message to the application.
    PayloadTransferComplete(endpoint_ptr, payload_state_ptr);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus TxCreateInternal(ConnectionProtocolType protocol_type, CdiTxConfigData* config_data_ptr,
                                 CdiCallback tx_cb_ptr, CdiConnectionHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = TxCreateConnection(protocol_type, config_data_ptr, tx_cb_ptr, ret_handle_ptr);
    if (kCdiStatusOk == rs) {
        rs = EndpointManagerTxCreateEndpoint((*ret_handle_ptr)->endpoint_manager_handle, STREAM_IDENTIFIER_NOT_USED,
                                             config_data_ptr->dest_ip_addr_str, config_data_ptr->dest_port, NULL, NULL);
    }

    return rs;
}

CdiReturnStatus TxCreateStreamConnectionInternal(CdiTxConfigData* config_data_ptr, CdiCallback tx_cb_ptr,
                                                 CdiConnectionHandle* ret_handle_ptr)
{
    return TxCreateConnection(kProtocolTypeAvm, config_data_ptr, tx_cb_ptr, ret_handle_ptr);
}

CdiReturnStatus TxCreateStreamEndpointInternal(CdiConnectionHandle handle, CdiTxConfigDataStream* stream_config_ptr,
                                               CdiEndpointHandle* ret_handle_ptr)
{
    return  EndpointManagerTxCreateEndpoint(handle->endpoint_manager_handle,
                                            stream_config_ptr->stream_identifier,
                                            stream_config_ptr->dest_ip_addr_str, stream_config_ptr->dest_port,
                                            stream_config_ptr->stream_name_str, ret_handle_ptr);
}

CdiReturnStatus TxPayloadInternal(CdiConnectionHandle con_handle, const CdiCoreTxPayloadConfig* core_payload_config_ptr,
                                  const CdiSgList* sgl_ptr, int max_latency_microsecs, int extra_data_size,
                                  uint8_t* extra_data_ptr)
{
    assert(sgl_ptr->total_data_size > 0);

    uint64_t start_time = CdiOsGetMicroseconds();
    CdiReturnStatus rs = kCdiStatusOk;
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)con_handle;

    int stream_identifier = STREAM_IDENTIFIER_NOT_USED;
    if (extra_data_size >= (int)sizeof(CDIPacketAvmCommonHeader) && extra_data_ptr) {
        CDIPacketAvmCommonHeader* common_header = (CDIPacketAvmCommonHeader*)extra_data_ptr;
        stream_identifier = common_header->avm_extra_data.stream_identifier;
    }

    CdiEndpointState* endpoint_ptr = FindEndpoint(con_state_ptr->endpoint_manager_handle, stream_identifier);
    if (NULL == endpoint_ptr ||
        kCdiConnectionStatusConnected != endpoint_ptr->adapter_endpoint_ptr->connection_status_code) {
        // Currently not connected, so no need to advance the payload any further here.
        return kCdiStatusNotConnected;
    }

    // Get free entry for payload state data from pool. NOTE: This pool is thread-safe, since it is used by application
    // thread(s) here and by TxPayloadThread().
    TxPayloadState* payload_state_ptr = NULL;
    if (!CdiPoolGet(con_state_ptr->tx_state.payload_state_pool_handle, (void**)&payload_state_ptr)) {
        // No free entries are available. Since this pool does not dynamically grow the queue used below must be full,
        // so return the queue full status here.
        rs = kCdiStatusQueueFull;
    } else {
        memset((void*)payload_state_ptr, 0, sizeof(TxPayloadState));

        payload_state_ptr->app_payload_cb_data.core_extra_data = core_payload_config_ptr->core_extra_data;
        payload_state_ptr->app_payload_cb_data.tx_payload_user_cb_param = core_payload_config_ptr->user_cb_param;
        payload_state_ptr->start_time = start_time;
        payload_state_ptr->max_latency_microsecs = max_latency_microsecs;
        CdiSinglyLinkedListInit(&payload_state_ptr->completed_packets_list);

        // Calculate the size of a group of units of unit_size.
        int pattern_size = 1; // How many units of unit_size need to be grouped to be byte aligned.
        if (core_payload_config_ptr->unit_size > 0) {
            switch (core_payload_config_ptr->unit_size % 8) {
                case 0:
                    pattern_size = 1;
                    break;
                case 2:
                    pattern_size = 4;
                    break;
                case 4:
                    pattern_size = 2;
                    break;
                case 6:
                    pattern_size = 4;
                    break;
                default: // For a fixed unit_size worst case of 8 units together will always be byte aligned.
                    pattern_size = 8;
                    break;
            }
        }
        payload_state_ptr->pattern_size_bytes = (pattern_size * core_payload_config_ptr->unit_size) / 8;

        payload_state_ptr->app_payload_cb_data.extra_data_size = extra_data_size;
        if (extra_data_size) {
            memcpy(&payload_state_ptr->app_payload_cb_data.extra_data_array, extra_data_ptr, extra_data_size);
        }

        payload_state_ptr->cdi_endpoint_handle = endpoint_ptr; // Save the endpoint used to send this payload.

        if (!CdiPayloadInit(con_state_ptr, sgl_ptr, payload_state_ptr)) {
            rs = kCdiStatusAllocationFailed;
        } else {
            // Put Tx payload message into the payload queue. The TxPayloadThread() thread will then process the
            // message. Don't block here and wait if the queue is full, return an error.
            if (!CdiQueuePush(con_state_ptr->tx_state.payload_queue_handle, &payload_state_ptr)) {
                // Queue was full, put the allocated memory back in the pools.
                rs = kCdiStatusQueueFull;
            }
        }

        if (kCdiStatusOk != rs) {
            // An error occurred, so free pool buffers reserved here and in CdiPayloadInit().
            CdiSglEntry* entry_ptr = payload_state_ptr->source_sgl.sgl_head_ptr;
            while (entry_ptr) {
                CdiSglEntry* next_ptr = entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
                CdiPoolPut(con_state_ptr->tx_state.payload_sgl_entry_pool_handle, entry_ptr);
                entry_ptr = next_ptr;
            }
            CdiPoolPut(con_state_ptr->tx_state.payload_state_pool_handle, payload_state_ptr);
        }
    }
    return rs;
}

void TxPayloadThreadFlushResources(CdiEndpointState* endpoint_ptr)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)endpoint_ptr->connection_state_ptr;
    CdiQueueFlush(con_state_ptr->tx_state.payload_queue_handle);

    // Process items in the work request completion queue. This will drain the queue and free associated resources
    // (ie. work_request_pool_handle) before we manually remove resources below. PayloadTransferComplete() has already
    // been called for all items in this queue (so don't call it again here).
    ProcessWorkRequestCompletionQueue(con_state_ptr);

    // Walk through the work request pool and free associated resources.
    // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing
    // them at a time.
    TxPayloadState* payload_state_ptr = NULL;
    TxPacketWorkRequest* work_request_ptr = NULL;
    while (CdiPoolPeekInUse(con_state_ptr->tx_state.work_request_pool_handle, (void**)&work_request_ptr)) {
        // NOTE: PayloadTransferComplete() called from FlushFailedPayload() frees the payload pointer, so only call
        // FlushFailedPayload() after walking to the end of the list of work requests related to it.
        if (payload_state_ptr != work_request_ptr->payload_state_ptr) {
            if (NULL == payload_state_ptr) {
                payload_state_ptr = work_request_ptr->payload_state_ptr;
            } else {
                FlushFailedPayload(endpoint_ptr, payload_state_ptr); // Frees the payload pointer
                payload_state_ptr = NULL; // No longer valid, so clear it.
            }
        }

        // Free all packet SGL entries related to this work request. This will free all entries that have been
        // completed successfully and ones that have not.
        CdiSglEntry* packet_entry_hdr_ptr = work_request_ptr->packet.sg_list.sgl_head_ptr;
        if (packet_entry_hdr_ptr) {
            // Put back SGL entry for each one in the list.
            FreeSglEntries(con_state_ptr->tx_state.packet_sgl_entry_pool_handle, packet_entry_hdr_ptr);
        }

        // Put back work request into the pool.
        CdiPoolPut(con_state_ptr->tx_state.work_request_pool_handle, work_request_ptr);
        work_request_ptr = NULL; // No longer valid, so clear it.
    }

    // Flush last payload if value exists.
    if (payload_state_ptr) {
        FlushFailedPayload(endpoint_ptr, payload_state_ptr);
        payload_state_ptr = NULL; // No longer valid, so clear it.
    }

    CdiPoolPutAll(con_state_ptr->tx_state.work_request_pool_handle);
    CdiQueueFlush(con_state_ptr->tx_state.work_req_comp_queue_handle);
    CdiPoolPutAll(con_state_ptr->tx_state.packet_sgl_entry_pool_handle);

    // NOTE: Don't flush app_payload_message_queue_handle, payload_state_pool_handle or payload_sgl_entry_pool_handle
    // here. They are handled by AppCallbackPayloadThread(). It doesn't use the Endpoint Manager since it calls
    // user-registered callback functions in the application, which may erroneously block and would stall the internal
    // pipeline.

    con_state_ptr->tx_state.payload_num = 0; // Clear packet number so receiver can expect packet 0 first.
}

CdiReturnStatus TxConnectionThreadJoin(CdiConnectionHandle con_handle)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)con_handle;

    if (con_state_ptr) {
        // Clean-up thread resources. We will wait for it to exit using thread join.
        SdkThreadJoin(con_state_ptr->payload_thread_id, con_state_ptr->shutdown_signal);
        con_state_ptr->payload_thread_id = NULL;
    }

    return kCdiStatusOk;
}

void TxConnectionDestroyInternal(CdiConnectionHandle con_handle)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)con_handle;

    if (con_state_ptr) {
        // Now that the connection and adapter threads have stopped, it is safe to clean up the remaining resources in
        // the opposite order of their creation.
        CdiQueueDestroy(con_state_ptr->tx_state.work_req_comp_queue_handle);
        con_state_ptr->tx_state.work_req_comp_queue_handle = NULL;

        CdiPoolDestroy(con_state_ptr->tx_state.payload_sgl_entry_pool_handle);
        con_state_ptr->tx_state.payload_sgl_entry_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->tx_state.payload_state_pool_handle);
        con_state_ptr->tx_state.payload_state_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->tx_state.packet_sgl_entry_pool_handle);
        con_state_ptr->tx_state.packet_sgl_entry_pool_handle = NULL;

        CdiPoolDestroy(con_state_ptr->tx_state.work_request_pool_handle);
        con_state_ptr->tx_state.work_request_pool_handle = NULL;

        CdiQueueDestroy(con_state_ptr->tx_state.payload_queue_handle);
        con_state_ptr->tx_state.payload_queue_handle = NULL;

        // NOTE: con_state_ptr is freed by the caller.
    }
}

void TxEndpointDestroy(CdiEndpointHandle handle)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)handle;

    CdiOsCritSectionDelete(endpoint_ptr->tx_state.payload_num_lock);
    endpoint_ptr->tx_state.payload_num_lock = NULL;
}

void TxPacketWorkRequestComplete(void* param_ptr, Packet* packet_ptr)
{
    CdiEndpointState* endpoint_ptr = (CdiEndpointState*)param_ptr;
    CdiConnectionState* con_state_ptr = endpoint_ptr->connection_state_ptr;

    if (kAdapterPacketStatusNotConnected == packet_ptr->tx_state.ack_status) {
        return;
    }

    // The internal_data_ptr contains a work request pointer that was set in TxPayloadThread().
    TxPacketWorkRequest* work_request_ptr = (TxPacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;

    // Now that we have our work request, we can setup additional state data pointers.
    TxPayloadState* payload_state_ptr = work_request_ptr->payload_state_ptr;

    // Check if the packet is from the payload that we are currently processing.
    if (payload_state_ptr->payload_packet_state.payload_num != work_request_ptr->payload_num) {
        CDI_LOG_THREAD(kLogWarning, "Connection[%s] packet for payload[%d] not from current payload[%d]",
                       endpoint_ptr->connection_state_ptr->saved_connection_name_str,
                       payload_state_ptr->payload_packet_state.payload_num, work_request_ptr->payload_num);
    } else {
        payload_state_ptr->data_bytes_transferred += work_request_ptr->packet_payload_size;

        if (kPayloadTypeKeepAlive == payload_state_ptr->payload_packet_state.payload_type) {
            // Payload type is keep alive. Keep it internal and do not use the application callback. Nothing special to
            // do here, unless payload data was allocated dynamically using a pool. If so, will need to free it here.
        } else {
            CdiSinglyLinkedListPushTail(&payload_state_ptr->completed_packets_list,
                                        (void*)&work_request_ptr->packet.list_entry);

            if (payload_state_ptr->data_bytes_transferred >= payload_state_ptr->source_sgl.total_data_size) {
                // Payload transfer complete.
                // Pointer is freed below in PayloadTransferComplete(). Clear it now so it cannot be accidentally used
                // later.
                work_request_ptr->payload_state_ptr = NULL;

                // Put list of work requests in queue so TxPayloadThread() can free the allocated resources.
                if (!CdiQueuePush(con_state_ptr->tx_state.work_req_comp_queue_handle,
                                  &payload_state_ptr->completed_packets_list)) {
                    CDI_LOG_THREAD(kLogError, "Queue[%s] full, push failed.",
                                   CdiQueueGetName(con_state_ptr->tx_state.work_req_comp_queue_handle));
                }
                work_request_ptr = NULL; // Pointer may no longer be valid, so clear it now.

                // Updates stats and puts message in queue to call the user registered Tx callback function.
                PayloadTransferComplete(endpoint_ptr, payload_state_ptr);
                payload_state_ptr = NULL; // Pointer is no longer valid.
            }
        }
    }
}

void TxInvokeAppPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr)
{
    CdiCoreCbData core_cb_data = {
        .status_code = app_cb_data_ptr->payload_status_code,
        .err_msg_str = app_cb_data_ptr->error_message_str,
        .connection_handle = (CdiConnectionHandle)con_state_ptr,
        .core_extra_data = app_cb_data_ptr->core_extra_data,
        .user_cb_param = app_cb_data_ptr->tx_payload_user_cb_param
    };

    if (kProtocolTypeRaw == con_state_ptr->protocol_type) {
        // Raw protocol so calling CdiRawTxCallback().
        CdiRawTxCbData cb_data = {
            .core_cb_data = core_cb_data
        };

        CdiRawTxCallback raw_tx_cb_ptr = (CdiRawTxCallback)con_state_ptr->tx_state.cb_ptr;
        (raw_tx_cb_ptr)(&cb_data); // Call the user-registered callback function.
    } else {
        // AVM protocol so calling CdiAvmTxCallback().
        CDIPacketAvmCommonHeader* avm_common_header_ptr =
            (CDIPacketAvmCommonHeader*)&app_cb_data_ptr->extra_data_array;

        CdiAvmTxCbData cb_data = {
            .core_cb_data = core_cb_data,
            .avm_extra_data = avm_common_header_ptr->avm_extra_data
        };

        CdiAvmTxCallback avm_tx_cb_ptr = (CdiAvmTxCallback)con_state_ptr->tx_state.cb_ptr;
        (avm_tx_cb_ptr)(&cb_data); // Call the user-registered callback function.
    }
}
