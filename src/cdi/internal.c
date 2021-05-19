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

#include "internal.h"

#include <arpa/inet.h> // For inet_ntop()
#include <string.h>

#include "adapter_api.h"
#include "adapter_control_interface.h"
#include "cdi_baseline_profile_01_00_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_logger_api.h"
#include "cdi_utility_api.h"
#include "endpoint_manager.h"
#include "fifo_api.h"
#include "internal_tx.h"
#include "internal_rx.h"
#include "statistics.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// @brief global configuration data.
CdiGlobalContext cdi_global_context = { 0 };

/// @brief Statically allocated mutex used to make initialization of global data thread-safe.
static CdiStaticMutexType global_context_mutex_lock = CDI_STATIC_MUTEX_INITIALIZER;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Payload thread used to notify application that payload has been transmitted and acknowledged as being received by the
 * receiver.
 *
 * @param ptr Pointer to thread specific data. In this case, a pointer to CdiConnectionState.
 *
 * @return The return value is not used.
 */
static THREAD AppCallbackPayloadThread(void* ptr)
{
    CdiConnectionState* con_state_ptr = (CdiConnectionState*)ptr;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(con_state_ptr->log_handle);

    while (!CdiOsSignalGet(con_state_ptr->shutdown_signal)) {
        // Wait for work to do. If the queue is empty, we will wait for data or the shutdown signal.
        AppPayloadCallbackData app_cb_data;
        if (CdiQueuePopWait(con_state_ptr->app_payload_message_queue_handle, CDI_INFINITE,
                            con_state_ptr->shutdown_signal, (void**)&app_cb_data)) {
            // Invoke application payload callback function.
            if (con_state_ptr->handle_type == kHandleTypeTx) {
                // Tx connection. All packets in the payload have been acknowledged as being received by the
                // receiver. Put the Tx payload entries and payload state data back in the pool. We do this here on
                // this thread to reduce the amount of work on the Tx Poll() thread.
                CdiSglEntry* entry_ptr = app_cb_data.tx_source_sgl.sgl_head_ptr;
                while (entry_ptr) {
                    CdiSglEntry* next_ptr = entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
                    CdiPoolPut(con_state_ptr->tx_state.payload_sgl_entry_pool_handle, entry_ptr);
                    entry_ptr = next_ptr;
                }
                // Notify the application.
                TxInvokeAppPayloadCallback(con_state_ptr, &app_cb_data);
            } else {
                // Rx connection. The SGL from the queue represents a received packet. Need to reassemble it into a
                // payload and send the payload SGL to the application.
                RxInvokeAppPayloadCallback(con_state_ptr, &app_cb_data);
            }
            // If error message exists, return it to pool.
            PayloadErrorFreeBuffer(con_state_ptr->error_message_pool, &app_cb_data);
        }
    }

    // Shutting down, so ensure queues and pools are drained.
    AppPayloadCallbackData app_cb_data;
    while (CdiQueuePop(con_state_ptr->app_payload_message_queue_handle, (void**)&app_cb_data)) {
        PayloadErrorFreeBuffer(con_state_ptr->error_message_pool, &app_cb_data);
    }
    if (con_state_ptr->handle_type == kHandleTypeTx) {
        CdiPoolPutAll(con_state_ptr->tx_state.payload_state_pool_handle);
        CdiPoolPutAll(con_state_ptr->tx_state.payload_sgl_entry_pool_handle);
    }

    return 0; // Return code not used.
}

/**
 * Function to shutdown connection.
 *
 * @param handle Pointer to connection being shutdown.
 */
static void ConnectionShutdownInternal(CdiConnectionHandle handle)
{
    assert(IsValidConnectionHandle(handle));

    EndpointManagerShutdownConnection(handle->endpoint_manager_handle);

    // Clean-up thread resources. We will wait for them to exit using thread join.
    SdkThreadJoin(handle->app_payload_message_thread_id, handle->shutdown_signal);
    handle->app_payload_message_thread_id = NULL;

    // Now that the connection and adapter threads have stopped, it is safe to clean up the remaining resources.
    if (kHandleTypeTx == handle->handle_type) {
        TxConnectionDestroyInternal(handle);
    } else {
        RxConnectionDestroyInternal(handle);
    }

    ConnectionCommonResourcesDestroy(handle); // Destroy resources that are common to Tx and Rx connections.

    CdiLoggerDestroyLog(handle->log_handle); // Destroy log last, so we can use it above (if necessary).

    // Free up this connection's memory.
    CdiOsMemFree(handle);
}

/**
 * Function to shutdown an adapter.
 *
 * @param handle Pointer to adapter being shut down.
 */
static void AdapterShutdownInternal(CdiAdapterHandle handle)
{
    // NOTE: No need to use the connections_list_lock here, since only one thread should be calling this function.
    if (!CdiListIsEmpty(&handle->connections_list)) {
        SDK_LOG_GLOBAL(kLogError,
                       "Connection list is not empty. Must use CdiCoreConnectionDestroy() for each connection before"
                       " shutting down an adapter.");
    }

    // Free the lock resource.
    CdiOsCritSectionDelete(handle->connections_list_lock);
    handle->connections_list_lock = NULL;

    // Shut down the adapter itself.
    CdiAdapterShutdown(handle);

    // Free the memory holding the adapter's state.
    CdiOsMemFree(handle);
}

#ifdef DEBUG_ENABLE_FIFO_DEBUGGING
static void FifoDebugCallback(const CdiFifoCbData* cb_ptr)
{
    CdiSgList* item_ptr = (CdiSgList*)cb_ptr->item_data_ptr;
    CdiPacketCommonHeader *common_hdr_ptr = (CdiPacketCommonHeader *)item_ptr->sgl_head_ptr->address_ptr;

    if (cb_ptr->is_read) {
        CDI_LOG_THREAD(kLogDebug, "FR H[%d] T[%d] P[%d] S[%d] A[%p]", cb_ptr->head_index, cb_ptr->tail_index,
                       common_hdr_ptr->payload_num, common_hdr_ptr->packet_sequence_num, item_ptr->sgl_head_ptr);
    } else {
        CDI_LOG_THREAD(kLogDebug, "FW H[%d] T[%d] P[%d] S[%d] A[%p]", cb_ptr->head_index, cb_ptr->tail_index,
                       common_hdr_ptr->payload_num, common_hdr_ptr->packet_sequence_num, item_ptr->sgl_head_ptr);
    }
}
#endif

/**
 * @brief Cleanup global resources. NOTE: Caller must have acquired mutex_lock.
 */
static void CleanupGlobalResources(void)
{
    // Adapter list should be empty here.
    if (!CdiListIsEmpty(&cdi_global_context.adapter_handle_list)) {
        SDK_LOG_GLOBAL(kLogError,
                       "Adapter list is not empty. Must use CdiCoreNetworkAdapterDestroy() for each adapter before"
                       " shutting down the SDK.");
    }
    if (cdi_global_context.adapter_handle_list_lock) {
        CdiOsCritSectionDelete(cdi_global_context.adapter_handle_list_lock);
    }

#ifdef CLOUDWATCH_METRICS_ENABLED
#ifdef METRICS_GATHERING_SERVICE_ENABLED
    MetricsGathererDestroy(cdi_global_context.metrics_gathering_sdk_handle);
#endif  // METRICS_GATHERING_SERVICE_ENABLED

    CloudWatchSdkMetricsDestroy(cdi_global_context.cw_sdk_handle);
    cdi_global_context.cw_sdk_handle = NULL;
#endif // CLOUDWATCH_METRICS_ENABLED

    CdiLoggerDestroyLog(cdi_global_context.global_log_handle); // WARNING: Cannot use the logger after this.
    cdi_global_context.global_log_handle = NULL;
    CdiLoggerShutdown(false); // Matches call to CdiLoggerInitialize(). NOTE: false= Normal termination.
    CdiOsMemFree(cdi_global_context.logger_handle);
    cdi_global_context.logger_handle = NULL;

    cdi_global_context.sdk_initialized = false;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CdiGlobalInitialization(const CdiCoreConfigData* core_config_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    CdiOsStaticMutexLock(global_context_mutex_lock);

    if (cdi_global_context.sdk_initialized) {
        SDK_LOG_GLOBAL(kLogError, "SDK Already initialized.");
        rs = kCdiStatusNonFatal;
    }

    if (kCdiStatusOk == rs) {
        // Create a critical section used to protect access to connections_list.
        if (!CdiOsCritSectionCreate(&cdi_global_context.adapter_handle_list_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        CdiListInit(&cdi_global_context.adapter_handle_list);
    }

    // Ensure the logger has been initialized.
    if (!CdiLoggerInitialize()) {
        rs = kCdiStatusFatal;
    }

    if (kCdiStatusOk == rs) {
        if (!CdiLoggerCreate(core_config_ptr->default_log_level, &cdi_global_context.logger_handle)) {
            rs = kCdiStatusFatal;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiLoggerCreateLog(cdi_global_context.logger_handle, NULL, core_config_ptr->global_log_method_data_ptr,
                                &cdi_global_context.global_log_handle)) {
            rs = kCdiStatusCreateLogFailed;
        }
    }

    // If CloudWatch pointer exists, set pointer to point to a copy of the settings and then save a copy of the
    // configuration strings. This is done so the caller can free the memory used by the data.
    if (kCdiStatusOk == rs && core_config_ptr->cloudwatch_config_ptr) {
#ifdef CLOUDWATCH_METRICS_ENABLED
        CloudWatchConfigData cleaned_cloudwatch_config = { 0 };
        const CloudWatchConfigData* cloudwatch_config_ptr = core_config_ptr->cloudwatch_config_ptr;

        // If a namespace string is not provided for cloudwatch use the CDI SDK default namespace string.
        if (!cloudwatch_config_ptr->namespace_str || ('\0' == cloudwatch_config_ptr->namespace_str[0])) {
            SDK_LOG_GLOBAL(kLogInfo, "CloudWatch namespace string not provided. Using default [%s].",
                           CLOUDWATCH_DEFAULT_NAMESPACE_STRING);
            cleaned_cloudwatch_config.namespace_str = CLOUDWATCH_DEFAULT_NAMESPACE_STRING;
        } else {
            cleaned_cloudwatch_config.namespace_str = cloudwatch_config_ptr->namespace_str;
        }

        // Region does not need any cleaning because the AWS SDK will automatically use the region called from if
        // a region is not set.
        cleaned_cloudwatch_config.region_str = cloudwatch_config_ptr->region_str;

        // A dimension domain string must be provided.
        if (!cloudwatch_config_ptr->dimension_domain_str || ('\0' == cloudwatch_config_ptr->dimension_domain_str[0])) {
            SDK_LOG_GLOBAL(kLogError, "CloudWatch dimension domain string cannot be NULL.");
            rs = kCdiStatusInvalidParameter;
        } else {
            cleaned_cloudwatch_config.dimension_domain_str = cloudwatch_config_ptr->dimension_domain_str;
        }

        if (kCdiStatusOk == rs) {
            rs = CloudWatchSdkMetricsCreate(&cleaned_cloudwatch_config, &cdi_global_context.cw_sdk_handle);
        }
#else  // CLOUDWATCH_METRICS_ENABLED
        SDK_LOG_GLOBAL(kLogError,
                       "Cannot use CloudWatch. The SDK was not built with CLOUDWATCH_METRICS_ENABLED defined.");
        rs = kCdiStatusCloudWatchNotEnabled;
#endif  // CLOUDWATCH_METRICS_ENABLED
    }

#ifdef METRICS_GATHERING_SERVICE_ENABLED
    if (kCdiStatusOk == rs) {
        bool use_default_dimension_string = (NULL == core_config_ptr->cloudwatch_config_ptr) ||
                                            (NULL == core_config_ptr->cloudwatch_config_ptr->dimension_domain_str) ||
                                            ('\0' == core_config_ptr->cloudwatch_config_ptr->dimension_domain_str[0]);
        const MetricsGathererConfigData config = {
            .dimension_domain_str = use_default_dimension_string ? "<none>" :
                                        core_config_ptr->cloudwatch_config_ptr->dimension_domain_str
        };
        rs = MetricsGathererCreate(&config, &cdi_global_context.metrics_gathering_sdk_handle);
    }
#endif  // METRICS_GATHERING_SERVICE_ENABLED

    if (kCdiStatusOk == rs) {
        cdi_global_context.sdk_initialized = true;
    } else {
        CleanupGlobalResources();
    }

    CdiOsStaticMutexUnlock(global_context_mutex_lock);

    return rs;
}

int CdiGatherInternal(const CdiSgList* sgl_ptr, int offset, void* dest_data_ptr, int byte_count)
{
    int bytes_skipped = 0;
    int bytes_copied = 0;
    bool done = false;
    uint8_t* p = (uint8_t *)dest_data_ptr;

    // go through all SGL entries with the option of early exit once byte_count is reached
    for (const CdiSglEntry* entry_ptr = sgl_ptr->sgl_head_ptr; !done && (entry_ptr != NULL);
            entry_ptr = entry_ptr->next_ptr) {
        // does this entry get bytes_skipped to offset?
        if (bytes_skipped + entry_ptr->size_in_bytes < offset) {
            // still skipping: nothing to copy yet
            bytes_skipped += entry_ptr->size_in_bytes;
        } else {  // not skipping: copy
            // how far into this entry to skip to get to offset
            const int offset_for_entry = CDI_MAX(0, offset - bytes_skipped);

            // offset source by the skip amount
            const uint8_t* src_ptr = (uint8_t*)entry_ptr->address_ptr + offset_for_entry;

            // how many bytes to copy: smaller of size of entry less the offset into it or number of bytes left to reach
            // byte_count
            const int num_bytes = CDI_MIN(entry_ptr->size_in_bytes - offset_for_entry, byte_count - bytes_copied);

            if (num_bytes > 0) {
                // copy the source bytes to the linear buffer
                memcpy(p, src_ptr, num_bytes);

                // account for the number of bytes copied
                bytes_copied += num_bytes;
                p += num_bytes;

                // if some bytes were skipped in this entry, set bytes_skipped so no more skipping occurs
                if (bytes_skipped < offset) {
                    bytes_skipped = offset;
                }
            } else {
                // skipping is complete: signal early exit if copy is complete
                done = bytes_copied >= byte_count;
            }
        }
    }
    return bytes_copied;
}

CdiReturnStatus AdapterInitializeInternal(CdiAdapterData* adapter_data_ptr, CdiAdapterHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiListIterator list_iterator;

    CdiOsCritSectionReserve(cdi_global_context.adapter_handle_list_lock);
    CdiListIteratorInit(&cdi_global_context.adapter_handle_list, &list_iterator);
    CdiListEntry* entry_ptr = NULL;
    // If there are any adapters that have already been initialized, then walk through list until we reach the head or
    // find an entry that matches the one we are currently trying to initialize. If we do find a match, then error out
    // and exit.
    while (NULL != (entry_ptr = CdiListIteratorGetNext(&list_iterator))) {
        CdiAdapterHandle adapter_handle_entry = (CdiAdapterHandle)entry_ptr;
        if ((adapter_handle_entry->adapter_data.adapter_type == adapter_data_ptr->adapter_type) &&
           (0 == CdiOsStrCmp(adapter_handle_entry->adapter_ip_addr_str, adapter_data_ptr->adapter_ip_addr_str))) {
            // If we find an adapter of the same type and with the same local IP addr as the new one we are attempting
            // to initialize, then error out and exit.
            CDI_LOG_THREAD(kLogError, "Unable to register an adapter with the IP address[%s] because an adapter "
                           "already exists for that IP address.", adapter_handle_entry->adapter_ip_addr_str);
            // Set return code to "duplicate adapter" status.
            rs = kCdiStatusAdapterDuplicateEntry;
            // Exit the search loop.
            break;
        }
    }

    CdiAdapterState* state_ptr = NULL;
    if (rs == kCdiStatusOk) {
        state_ptr = (CdiAdapterState*)CdiOsMemAllocZero(sizeof *state_ptr);
        if (state_ptr == NULL) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (rs == kCdiStatusOk) {
        state_ptr->magic = kMagicAdapter;

        // Make a copy of the adapter's initialization data.
        state_ptr->adapter_data = *adapter_data_ptr;

        // Make a copy of the IP string and update the copy of the adapter data to point to it. This is done so the
        // caller can free the memory used by the data.
        CdiOsStrCpy(state_ptr->adapter_ip_addr_str, sizeof(state_ptr->adapter_ip_addr_str),
                    adapter_data_ptr->adapter_ip_addr_str);
        state_ptr->adapter_data.adapter_ip_addr_str = state_ptr->adapter_ip_addr_str;

        switch (adapter_data_ptr->adapter_type) {
        case kCdiAdapterTypeEfa:
            rs = EfaNetworkAdapterInitialize(state_ptr, /*not socket-based*/ false);
            break;
        case kCdiAdapterTypeSocketLibfabric:
            rs = EfaNetworkAdapterInitialize(state_ptr, /*socket-based*/ true);
            break;
        case kCdiAdapterTypeSocket:
            rs = SocketNetworkAdapterInitialize(state_ptr);
            break;
        }

        if (rs == kCdiStatusOk) {
            if (state_ptr->adapter_data.tx_buffer_size_bytes) {
                // Ensure returned Tx buffer pointer was set.
                assert(state_ptr->adapter_data.ret_tx_buffer_ptr);
            }

            // Update returned Tx buffer pointer.
            adapter_data_ptr->ret_tx_buffer_ptr = state_ptr->adapter_data.ret_tx_buffer_ptr;

            // Ensure platform specific state got set correctly.
            assert(state_ptr->functions_ptr != NULL);
        }

        if (rs == kCdiStatusOk) {
            // Create a critical section used to protect access to connections_list.
            if (!CdiOsCritSectionCreate(&state_ptr->connections_list_lock)) {
                rs = kCdiStatusNotEnoughMemory;
            }
        }

        if (rs == kCdiStatusOk) {
            // Initialize the list of connections using this adapter.
            CdiListInit(&state_ptr->connections_list);

            // Add the structure to network adapter handle list.
            CdiListAddTail(&cdi_global_context.adapter_handle_list, &state_ptr->list_entry);
        }
    }

    if (rs != kCdiStatusOk) {
        if (state_ptr) {
            CdiOsCritSectionDelete(state_ptr->connections_list_lock);
            CdiOsMemFree(state_ptr);
            state_ptr = NULL;
        }
    }
    CdiOsCritSectionRelease(cdi_global_context.adapter_handle_list_lock);

    *ret_handle_ptr = state_ptr;

    return rs;
}

CdiReturnStatus NetworkAdapterDestroyInternal(CdiAdapterHandle handle)
{
    CdiReturnStatus rs = kCdiStatusInvalidHandle; // Default to an error, if we don't find the handle in the list.
    CdiListIterator list_iterator;

    CdiOsCritSectionReserve(cdi_global_context.adapter_handle_list_lock);
    CdiListIteratorInit(&cdi_global_context.adapter_handle_list, &list_iterator);
    CdiListEntry* entry_ptr = NULL;

    // Walk adapter list and try to find a match.
    while (NULL != (entry_ptr = CdiListIteratorGetNext(&list_iterator))) {
        CdiAdapterHandle adapter_handle_entry = (CdiAdapterHandle)entry_ptr;
        if (adapter_handle_entry == handle) {
            // Remove it from the list and then shutdown and free resources used by the adapter.
            CdiListRemove(&cdi_global_context.adapter_handle_list, entry_ptr);
            AdapterShutdownInternal(handle);
            rs = kCdiStatusOk; // Found handle, so set returned status to ok.
            break; // Exit the loop.
        }
    }

    CdiOsCritSectionRelease(cdi_global_context.adapter_handle_list_lock);

    return rs;
}


CdiReturnStatus ConnectionCommonResourcesCreate(CdiConnectionHandle handle, CdiCoreStatsCallback stats_cb_ptr,
                                                CdiUserCbParameter stats_user_cb_param,
                                                const CdiStatsConfigData* stats_config_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Create signal for starting connection threads.
    if (!CdiOsSignalCreate(&handle->start_signal)) {
        rs = kCdiStatusNotEnoughMemory;
    }

    // Create signal for shutting down connection threads.
    if (!CdiOsSignalCreate(&handle->shutdown_signal)) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        // Create Endpoint Manager.
        rs = EndpointManagerCreate(handle, stats_cb_ptr, stats_user_cb_param, stats_config_ptr,
                                   &handle->endpoint_manager_handle);
    }

    if (kCdiStatusOk == rs) {
        // Create payload receive message queue that is used to send messages to the application callback thread.
        if (!CdiQueueCreate("PayloadRequests AppPayloadCallbackData Queue", MAX_PAYLOADS_PER_CONNECTION,
                            FIXED_QUEUE_SIZE, FIXED_QUEUE_SIZE, sizeof(AppPayloadCallbackData),
                            kQueueSignalPopWait, // Queue can block on pops.
                            &handle->app_payload_message_queue_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create a pool used to hold error message strings.
        int max_rx_payloads = MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION;
        int max_tx_payloads = MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION;

        if (handle->handle_type == kHandleTypeRx) {
            if (handle->rx_state.config_data.max_simultaneous_rx_payloads_per_connection) {
                max_rx_payloads = handle->rx_state.config_data.max_simultaneous_rx_payloads_per_connection;
            }
        } else {
            if (handle->tx_state.config_data.max_simultaneous_tx_payloads) {
                max_tx_payloads = handle->tx_state.config_data.max_simultaneous_tx_payloads;
            }
        }

        int size = CDI_MAX(max_tx_payloads, max_rx_payloads);

        if (!CdiPoolCreate("Error Messages Pool", size, NO_GROW_SIZE, NO_GROW_COUNT, MAX_ERROR_STRING_LENGTH,
                           true, // true= Make thread-safe
                           &handle->error_message_pool)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (kCdiStatusOk == rs) {
        // Add the structure to the adapter's list of connections.
        CdiOsCritSectionReserve(handle->adapter_state_ptr->connections_list_lock);
        CdiListAddTail(&handle->adapter_state_ptr->connections_list, &handle->list_entry);
        CdiOsCritSectionRelease(handle->adapter_state_ptr->connections_list_lock);
    }

    return rs;
}

void ConnectionCommonResourcesDestroy(CdiConnectionHandle handle)
{
    CdiPoolDestroy(handle->error_message_pool);
    handle->error_message_pool = NULL;
    CdiQueueDestroy(handle->app_payload_message_queue_handle);
    handle->app_payload_message_queue_handle = NULL;

    EndpointManagerDestroy(handle->endpoint_manager_handle);
    handle->endpoint_manager_handle = NULL;

    CdiOsSignalDelete(handle->shutdown_signal);
    handle->shutdown_signal = NULL;

    CdiOsSignalDelete(handle->start_signal);
    handle->start_signal = NULL;
}

CdiReturnStatus ConnectionCommonPacketMessageThreadCreate(CdiConnectionHandle handle)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Start the thread which will service items from the queue.
    if (!CdiOsThreadCreate(AppCallbackPayloadThread, &handle->app_payload_message_thread_id, "PayloadMessage",
                           handle, handle->start_signal)) {
        rs = kCdiStatusNotEnoughMemory;
    }

    return rs;
}

CdiReturnStatus CoreStatsConfigureInternal(CdiConnectionHandle handle, const CdiStatsConfigData* new_config_ptr,
                                           bool force_changes)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiStatsConfigData* current_config_ptr = NULL;
    if (kHandleTypeTx == handle->handle_type) {
        current_config_ptr = &handle->tx_state.config_data.stats_config;
    } else {
        current_config_ptr = &handle->rx_state.config_data.stats_config;
    }

    // If forcing changes or the stats configuration has changed, then apply the new configuration.
    if (force_changes || (0 != memcmp(current_config_ptr, new_config_ptr, sizeof(*current_config_ptr)))) {
        // Settings changed, so apply them.
        rs = StatsConfigure(handle->stats_state_ptr, new_config_ptr);
    }

    if (kCdiStatusOk == rs) {
        // Update saved configuration stats data.
        *current_config_ptr = *new_config_ptr;
    }

    return kCdiStatusOk;
}

void PayloadErrorSet(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr,
                     CdiReturnStatus status_code, const char* format_str, ...)
{
    va_list vars;
    va_start(vars, format_str);

    app_cb_data_ptr->payload_status_code = status_code; // Set the status code.

    // NOTE: No critical sections needed, since only called by a single thread for the related app_cb_data_ptr.
    if (NULL == app_cb_data_ptr->error_message_str) {
        if (!CdiPoolGet(con_state_ptr->error_message_pool, (void**)&app_cb_data_ptr->error_message_str)) {
            CDI_LOG_THREAD(kLogError, "Unable to get free entry from pool[%s].",
                           CdiPoolGetName(con_state_ptr->error_message_pool));
        } else {
            // Generate error message string.
            vsnprintf(app_cb_data_ptr->error_message_str, CdiPoolGetItemSize(con_state_ptr->error_message_pool),
                      format_str, vars);
        }
    }

    va_end(vars);
}

void PayloadErrorFreeBuffer(CdiPoolHandle pool_handle, AppPayloadCallbackData* app_cb_data_ptr)
{
    // NOTE: No critical sections needed, since only called by a single thread for the related app_cb_data_ptr.
    if (app_cb_data_ptr->error_message_str) {
        CdiPoolPut(pool_handle, app_cb_data_ptr->error_message_str);
        app_cb_data_ptr->error_message_str = NULL; // Pointer is no longer valid, so clear it.
    }
}

void ConnectionDestroyInternal(CdiConnectionHandle handle)
{
    if (handle) {
        CdiAdapterHandle adapter = handle->adapter_state_ptr;

        CdiOsCritSectionReserve(adapter->connections_list_lock);
        bool locked = true;

        CdiListIterator list_iterator;
        CdiListIteratorInit(&adapter->connections_list, &list_iterator);

        CdiListEntry* entry_ptr = NULL;
        // Walk through list until we reach the head or find our desired entry.
        while (NULL != (entry_ptr = CdiListIteratorGetNext(&list_iterator))) {
            CdiConnectionState* obj_ptr = CONTAINER_OF(entry_ptr, CdiConnectionState, list_entry);

            // If we find the desired entry, remove it from the list and free the memory used by the object.
            if (obj_ptr == handle) {
                CdiListRemove(&adapter->connections_list, entry_ptr);
                CdiOsCritSectionRelease(adapter->connections_list_lock);
                locked = false;
                // Shut down this connection's associated endpoint and free the associated memory.
                ConnectionShutdownInternal(obj_ptr);
                break;
            }
        }
        if (locked) {
            CdiOsCritSectionRelease(adapter->connections_list_lock);
        }
    }
}

void EndpointDestroyInternal(CdiEndpointHandle handle)
{
    EndpointManagerEndpointDestroy(handle);
}

CdiReturnStatus SdkShutdownInternal(void)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiOsStaticMutexLock(global_context_mutex_lock);

    if (cdi_global_context.sdk_initialized) {
        CleanupGlobalResources();
    }
    CdiOsShutdown(); // Always cleanup/shutdown the OS API.

    CdiOsStaticMutexUnlock(global_context_mutex_lock);

    return rs;
}

bool FreeSglEntries(CdiPoolHandle pool_handle, CdiSglEntry* sgl_entry_head_ptr)
{
    bool ret = true;

    // Put back SGL entry for each one in the list.
    CdiSglEntry* sgl_entry_ptr = sgl_entry_head_ptr;
    while (sgl_entry_ptr) {
        CdiSglEntry* sgl_entry_next_ptr = sgl_entry_ptr->next_ptr; // Save next entry, since Put() will free its memory.
        CdiPoolPut(pool_handle, sgl_entry_ptr);

        // Check for infinite loop (using same pointer)?
        if (sgl_entry_ptr == sgl_entry_next_ptr) {
            assert(false);
            ret = false;
            break;
        }
        sgl_entry_ptr = sgl_entry_next_ptr;
    }

    return ret;
}

void DumpPayloadConfiguration(const CdiCoreExtraData* core_extra_data_ptr, int extra_data_size,
                              const uint8_t* extra_data_array, ConnectionProtocolType protocol_type)
{
    CdiLogMultilineState m_state;
    CDIPacketAvmUnion* avm_union_ptr = (CDIPacketAvmUnion*)extra_data_array;
    CDI_LOG_THREAD_MULTILINE_BEGIN(kLogInfo, &m_state);
    CDI_LOG_MULTILINE(&m_state, "Dumping Tx payload configuration:");

    CDI_LOG_MULTILINE(&m_state, "origination_ptp_timestamp [%u:%u]",
                      core_extra_data_ptr->origination_ptp_timestamp.seconds,
                      core_extra_data_ptr->origination_ptp_timestamp.nanoseconds);
    CDI_LOG_MULTILINE(&m_state, "payload_user_data         [%llu]", core_extra_data_ptr->payload_user_data);
    CDI_LOG_MULTILINE(&m_state, "extra_data_size           [%d]", extra_data_size);

    if (kProtocolTypeAvm == protocol_type && sizeof(avm_union_ptr->with_config) == extra_data_size) {
        CdiAvmBaselineConfig baseline_config;
        CdiAvmConfig* avm_config_ptr = &avm_union_ptr->with_config.config;
        CdiAvmParseBaselineConfiguration(avm_config_ptr, &baseline_config);
        // NOTE: Payload type is not specific to a profile version, so using NULL here for version.
        CDI_LOG_MULTILINE(&m_state, "payload_type              [%s]",
                          CdiAvmKeyEnumToString(kKeyAvmPayloadType, baseline_config.payload_type, NULL));
        switch (baseline_config.payload_type) {
            case kCdiAvmNotBaseline:
                break;
            case kCdiAvmVideo:
                {
                    CdiAvmVideoConfig* video_config_ptr = &baseline_config.video_config;

                    CDI_LOG_MULTILINE(&m_state, "resolution                [%dx%d]",
                                      video_config_ptr->width,
                                      video_config_ptr->height);

                    CDI_LOG_MULTILINE(&m_state, "sampling                  [%s]",
                                      CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType,
                                                            video_config_ptr->sampling,
                                                            &baseline_config.video_config.version));

                    CDI_LOG_MULTILINE(&m_state, "bit depth                 [%s]",
                                      CdiAvmKeyEnumToString(kKeyAvmVideoBitDepthType,
                                                            video_config_ptr->depth,
                                                            &baseline_config.video_config.version));

                    CDI_LOG_MULTILINE(&m_state, "frame rate (num/den)      [%d/%d]",
                                      video_config_ptr->frame_rate_num,
                                      video_config_ptr->frame_rate_den);

                    CDI_LOG_MULTILINE(&m_state, "colorimetry               [%s]",
                                      CdiAvmKeyEnumToString(kKeyAvmVideoColorimetryType,
                                                            video_config_ptr->colorimetry,
                                                            &baseline_config.video_config.version));

                    CDI_LOG_MULTILINE(&m_state, "interlace                 [%s]",
                                      CdiUtilityBoolToString(video_config_ptr->interlace));

                    CDI_LOG_MULTILINE(&m_state, "segmented                 [%s]",
                                     CdiUtilityBoolToString(video_config_ptr->segmented));

                    CDI_LOG_MULTILINE(&m_state, "TCS                       [%s]",
                                     CdiAvmKeyEnumToString(kKeyAvmVideoTcsType,
                                                           video_config_ptr->tcs,
                                                            &baseline_config.video_config.version));

                    CDI_LOG_MULTILINE(&m_state, "range                     [%s]",
                                     CdiAvmKeyEnumToString(kKeyAvmVideoRangeType,
                                                           video_config_ptr->range,
                                                            &baseline_config.video_config.version));

                    CDI_LOG_MULTILINE(&m_state, "PAR (width:height)        [%d:%d]",
                                      video_config_ptr->par_width,
                                      video_config_ptr->par_height);
                }
                break;
            case kCdiAvmAudio:
                {
                    CdiAvmAudioConfig* audio_config_ptr = &baseline_config.audio_config;

                    CDI_LOG_MULTILINE(&m_state, "grouping                  [%s]",
                                      CdiAvmKeyEnumToString(kKeyAvmAudioChannelGroupingType,
                                                            audio_config_ptr->grouping,
                                                            &baseline_config.audio_config.version));
                }
                break;
            case kCdiAvmAncillary:
                CDI_LOG_MULTILINE(&m_state, "Ancillary payloads do not have config header data.");
                break;
            default:
                CDI_LOG_MULTILINE(&m_state, "Invalid payload type[%d].", baseline_config.payload_type);
        }
    }

    CDI_LOG_MULTILINE_END(&m_state);
}

void BytesToHexString(const void* data_ptr, int data_byte_count, char* dest_buffer_str, int dest_buffer_size)
{
    // Allow for trailing '\0'.
    for (int i = 0; i < data_byte_count && dest_buffer_size > 2+1; i++) {
        int char_count = snprintf(dest_buffer_str, dest_buffer_size, "%02X", ((uint8_t*)data_ptr)[i]);
        dest_buffer_str += char_count;
        dest_buffer_size -= char_count;
    }
    *dest_buffer_str = '\0';
}

void DeviceGidToString(const uint8_t* device_gid_ptr, int gid_length, char* dest_buffer_str, int dest_buffer_size)
{
    // For the EFA, the address will contain the GID (16 bytes) and QPN (2 bytes), which combine to make a unique value
    // for each endpoint. See "efa_ep_addr" in the EFA provider (efa.h). The structure is private, so we don't use it
    // here to get at the QPN value.
    char inet_str[MAX_IPV6_ADDRESS_STRING_LENGTH] = { 0 };
    inet_ntop(AF_INET6, device_gid_ptr, inet_str, sizeof(inet_str));

    // Get the two QP bytes that follow the 16 byte GID and convert to a hex string.
    char hex_str[16] = { 0 };
    if (gid_length >= 16+2) {
        BytesToHexString(device_gid_ptr + 16, 2, hex_str, sizeof(hex_str));
    }

    snprintf(dest_buffer_str, dest_buffer_size, "%s-%s", inet_str, hex_str);
}

void SdkThreadJoin(CdiThreadID thread_id, CdiSignalType shutdown_signal)
{
    if (NULL != shutdown_signal) {
        CdiOsSignalSet(shutdown_signal);
    }
    if (thread_id) {
        CdiOsThreadJoin(thread_id, CDI_INFINITE, NULL);
    }
}

CdiLogHandle CdiLogGlobalGetInternal(void)
{
    return cdi_global_context.global_log_handle;
}
