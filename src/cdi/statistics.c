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

#include "statistics.h"

#include "cdi_os_api.h"
#include "cloudwatch.h"
#include "endpoint_manager.h"
#include "internal_log.h"
#include "logger_api.h"
#include "t_digest.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Structure that holds the parts of StatisticsState structure required per statistics gathering path.
 */
typedef struct {
    TDigestHandle td_handle;          ///< Handle for accessing this connection's percentile tracking t-Digest.
    CdiSignalType thread_exit_signal; ///< Signal used for dynamic thread exit.
    CdiThreadID stats_thread_id;      ///< Stats thread ID. The thread is dynamically created/destroyed as needed.
} MetricsDestinationInfo;

/**
 * Enumeration of the possible metrics destinations.
 */
typedef enum {
    kMetricsDestinationCloudWatch,        ///< The user's CloudWatch metrics.
#ifdef METRICS_GATHERING_SERVICE_ENABLED
    kMetricsDestinationGatheringService,  ///< The CDI metrics gathering service.
#endif  // METRICS_GATHERING_SERVICE_ENABLED
    kMetricsDestinationsCount             ///< The number of supported metrics destinations.
} MetricsDestinations;

/**
 * Function pointer used for sending metrics from the StatsThread().
 */
typedef void (*SendStatsMessage)(StatisticsState* stats_state_ptr, int stats_path_number);

/**
 * Arguments to the StatsThread().
 */
typedef struct {
    StatisticsState* stats_state_ptr; ///< Pointer to the StatsState to be managed by the thread.
    SendStatsMessage send_stats_message_ptr; ///< Pointer to the function for sending statistics.
    int metrics_destination_idx;      ///< The index into StatisticsState.destination_info array to use for this thread.
    uint32_t stats_period_ms;         ///< Stats period in milliseconds.
} StatsThreadArgs;

/**
 * @brief Structure used to hold state data for statistics.
 */
struct StatisticsState {
    CdiConnectionState* con_state_ptr;     ///< Pointer to connection state data.

    /// @brief The metrics destinations info for all destinations of the statistics managed by this statistics object.
    MetricsDestinationInfo destination_info[kMetricsDestinationsCount];

    /// @brief Lock used to protect multi-threaded access to counter/time base stats data.
    CdiCsID stats_data_lock;

    uint32_t stats_period_ms;              ///< Stats period in milliseconds.

    CdiCoreStatsCallback user_cb_ptr;      ///< Callback function pointer.
    CdiUserCbParameter user_cb_param;      ///< Callback function user parameter.

    CloudWatchHandle cloudwatch_handle;    ///< Handle to instance of CloudWatch component related to this connection.
    CloudWatchHandle metrics_gatherer_handle; ///< Handle of object to send metrics to gathering service.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Get current transfer statistics data for the specified connection and write to the provided address.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param ret_stats_ptr Address where to write returned statistics data.
 * @param destination_idx The index into the destination info array within the statistics state.
 */
static void GetStats(CdiEndpointState* endpoint_ptr, CdiTransferStats* ret_stats_ptr, int destination_idx)
{
    StatisticsState* stats_state_ptr = endpoint_ptr->connection_state_ptr->stats_state_ptr;
    MetricsDestinationInfo* stats_path_members_ptr = &stats_state_ptr->destination_info[destination_idx];

    CdiOsCritSectionReserve(stats_state_ptr->stats_data_lock); // Synchronize with the writer.

    // Set timestamp of the stats, in milliseconds since epoch.
    struct timespec tm;
    CdiOsGetUtcTime(&tm);
    endpoint_ptr->transfer_stats.timestamp_in_ms_since_epoch = (tm.tv_sec * 1000) + (tm.tv_nsec / 1000000);

    // Get percentile values for P50, P90, P99. Also get min and max, which are P0 and P100, respectively.
    TDigestHandle td_handle = stats_path_members_ptr->td_handle;
    CdiPayloadTimeIntervalStats* interval_ptr = &endpoint_ptr->transfer_stats.payload_time_interval_stats;
    TDigestGetPercentileValue(td_handle, 0, &interval_ptr->transfer_time_min);
    TDigestGetPercentileValue(td_handle, 50, &interval_ptr->transfer_time_P50);
    TDigestGetPercentileValue(td_handle, 90, &interval_ptr->transfer_time_P90);
    TDigestGetPercentileValue(td_handle, 99, &interval_ptr->transfer_time_P99);
    TDigestGetPercentileValue(td_handle, 100, &interval_ptr->transfer_time_max);
    interval_ptr->transfer_count = TDigestGetCount(td_handle);

    // Copy the stats series to returned stats.
    *ret_stats_ptr = endpoint_ptr->transfer_stats;

    // Reset the payload time interval stats.
    memset(interval_ptr, 0, sizeof(*interval_ptr));
    // Reset the t-Digest.
    TDigestClear(td_handle);

    CdiOsCritSectionRelease(stats_state_ptr->stats_data_lock);
}

/**
 * Get latest transfer statistics data and provide to users by invoking all registered callbacks.
 *
 * @param stats_state_ptr Pointer to stats state data.
 * @param destination_idx The index into the destination info array within the statistics state.
 */
static void SendUserStatsMessage(StatisticsState* stats_state_ptr, int destination_idx)
{
    CdiTransferStats transfer_stats_array[MAX_ENDPOINTS_PER_CONNECTION];
    CdiCoreStatsCbData cb_data = {
        .stats_count = 0,
        .transfer_stats_array = transfer_stats_array,
        .stats_user_cb_param = stats_state_ptr->user_cb_param,
    };

    // Collect the stats from all of the endpoints of the connection.
    CdiEndpointHandle endpoint_handle =
        EndpointManagerGetFirstEndpoint(stats_state_ptr->con_state_ptr->endpoint_manager_handle);

    if (endpoint_handle) {
        while (endpoint_handle) {
            GetStats(endpoint_handle, cb_data.transfer_stats_array + cb_data.stats_count++, destination_idx);
            endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
        }

        if (stats_state_ptr->user_cb_ptr) {
            (stats_state_ptr->user_cb_ptr)(&cb_data);
        }

        if (stats_state_ptr->cloudwatch_handle) {
            CloudWatchStatisticsMessage(stats_state_ptr->cloudwatch_handle, cb_data.stats_count,
                                        cb_data.transfer_stats_array);
        }
    }
}

#ifdef METRICS_GATHERING_SERVICE_ENABLED
/**
 * Get latest transfer statistics data and provide to users by invoking all registered callbacks.
 *
 * @param stats_state_ptr Pointer to stats state data.
 * @param destination_idx The index into the destination info array within the statistics state.
 */
static void SendToCdiMetricsService(StatisticsState* stats_state_ptr, int destination_idx)
{
    CdiTransferStats transfer_stats_array[MAX_ENDPOINTS_PER_CONNECTION];
    int stats_count = 0;

    // Collect the stats from all of the endpoints of the connection.
    CdiEndpointHandle endpoint_handle =
        EndpointManagerGetFirstEndpoint(stats_state_ptr->con_state_ptr->endpoint_manager_handle);
    while (endpoint_handle) {
        GetStats(endpoint_handle, transfer_stats_array + stats_count++, destination_idx);
        endpoint_handle = EndpointManagerGetNextEndpoint(endpoint_handle);
    }

    if (stats_state_ptr->metrics_gatherer_handle) {
        CloudWatchStatisticsMessage(stats_state_ptr->metrics_gatherer_handle, stats_count, transfer_stats_array);
    }
}
#endif  // METRICS_GATHERING_SERVICE_ENABLED

/**
 * Statistic gathering thread used to invoke registered callback functions when new statistics are available.
 *
 * @param ptr Pointer to thread specific data. In this case, a pointer to StatisticsState.
 *
 * @return The return value is not used.
 */
static THREAD StatsThread(void* ptr)
{
    StatsThreadArgs* args_ptr = (StatsThreadArgs*)ptr;
    StatisticsState* stats_state_ptr = args_ptr->stats_state_ptr;
    CdiConnectionState* con_state_ptr = stats_state_ptr->con_state_ptr;

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(con_state_ptr->log_handle);

    // Setup an array of signals to wait on.
    CdiSignalType signal_array[2];
    signal_array[0] = con_state_ptr->shutdown_signal;
    signal_array[1] = stats_state_ptr->destination_info[args_ptr->metrics_destination_idx].thread_exit_signal;

    uint64_t base_time = CdiOsGetMilliseconds();
    int interval_counter = 0;

    uint32_t wait_time_ms = args_ptr->stats_period_ms;
    uint32_t signal_index = 0;
    while (CdiOsSignalsWait(signal_array, 2, false, wait_time_ms, &signal_index)) {
        wait_time_ms = args_ptr->stats_period_ms;
        if (0 == signal_index || 1 == signal_index) {
            // Got shutdown or thread exit signal, so exit.
            break;
        } else {
            // Got timeout (OS_SIG_TIMEOUT). Send latest stats to all registered callbacks.
            args_ptr->send_stats_message_ptr(stats_state_ptr, args_ptr->metrics_destination_idx);
            interval_counter++;
            uint64_t next_start_time = base_time + (interval_counter * args_ptr->stats_period_ms) +
                                       args_ptr->stats_period_ms;

            uint64_t current_time = CdiOsGetMilliseconds();
            if (current_time > next_start_time) {
                uint32_t late_time_ms = (uint32_t)(current_time - next_start_time);
                CDI_LOG_THREAD(kLogError, "Connection[%s] Gather stats late by[%d] milliseconds.",
                               con_state_ptr->saved_connection_name_str, late_time_ms);
                // Set new base time, reset interval counter and set wait time to zero (process next stat immediately).
                base_time = current_time;
                interval_counter = 0;
                wait_time_ms = 0;
            } else {
                // Calculate remaining wait time in order to stay on cadence.
                wait_time_ms = next_start_time - current_time;
            }
        }
    }

    // Thread is exiting. Send last set of stats, if any.
    args_ptr->send_stats_message_ptr(stats_state_ptr, args_ptr->metrics_destination_idx);

    // Free the memory used to hold the args to the thread.
    CdiOsMemFree(ptr);

    return 0; // Return code not used.
}

/**
 * Destroy stats thread. Used for both dynamic and shutdown destruction.
 *
 * @param destination_info_ptr Pointer to the information applicable to the metrics destination managed by the thread.
 */
static void StatsThreadDestroy(MetricsDestinationInfo* destination_info_ptr)
{
    if (destination_info_ptr->stats_thread_id) {
        // Stats thread exist, so signal it to exit and then wait for it to actually exit.
        if (destination_info_ptr->thread_exit_signal) {
            CdiOsSignalSet(destination_info_ptr->thread_exit_signal);
        }
        CdiOsThreadJoin(destination_info_ptr->stats_thread_id, CDI_INFINITE, NULL);
        destination_info_ptr->stats_thread_id = NULL;

        if (destination_info_ptr->thread_exit_signal) {
            CdiOsSignalClear(destination_info_ptr->thread_exit_signal); // Done with the signal so clear it.
        }
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus StatsCreate(CdiConnectionState* con_state_ptr, CdiCoreStatsCallback stats_cb_ptr,
                            CdiUserCbParameter stats_user_cb_param, CloudWatchSdkMetricsHandle cw_sdk_handle,
                            CloudWatchSdkMetricsHandle metrics_gatherer_sdk_handle,
                            StatisticsHandle* return_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    // Allocate a generic endpoint state structure.
    StatisticsState* stats_state_ptr = CdiOsMemAllocZero(sizeof(StatisticsState));
    if (stats_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        stats_state_ptr->con_state_ptr = con_state_ptr;
        stats_state_ptr->user_cb_ptr = stats_cb_ptr;
        stats_state_ptr->user_cb_param = stats_user_cb_param;
    }

    // Create t-Digest instances and exit signals.
    for (int i = 0 ; kCdiStatusOk == rs && i < kMetricsDestinationsCount ; i++) {
        if (!TDigestCreate(&stats_state_ptr->destination_info[i].td_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
        if (!CdiOsSignalCreate(&stats_state_ptr->destination_info[i].thread_exit_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create critical section for protecting access to stats data.
        if (!CdiOsCritSectionCreate(&stats_state_ptr->stats_data_lock)) {
            rs = kCdiStatusFatal;
        }
    }

    // Don't create instance of CloudWatch if the AWS-SDK is not enabled.
    if (kCdiStatusOk == rs && cw_sdk_handle) {
        rs = CloudWatchCreate(con_state_ptr, cw_sdk_handle, &stats_state_ptr->cloudwatch_handle);
    }

    static const int metrics_gathering_period_ms = 60000;  // Metrics are sent to the gathering service once per minute.

    // Create an instance of a CloudWatch queue handler for the metrics gathering service.
    if (kCdiStatusOk == rs) {
        rs = CloudWatchCreate(con_state_ptr, metrics_gatherer_sdk_handle, &stats_state_ptr->metrics_gatherer_handle);
    }

    // Statically configure the CloudWatch queue handler for the metrics gathering service.
    if (kCdiStatusOk == rs) {
        const CdiStatsConfigData stats_config = {
            .disable_cloudwatch_stats = false,
            .stats_period_seconds = metrics_gathering_period_ms,
        };
        rs = CloudWatchConfigure(stats_state_ptr->metrics_gatherer_handle, &stats_config);
    }

#ifdef METRICS_GATHERING_SERVICE_ENABLED
    // Create the statistics update thread that feeds the queue for the metrics gathering service.
    if (kCdiStatusOk == rs) {
        // The args need to be allocated on the heap since the thread needs access to it after this block ends. If the
        // thread is successfully created, ownership of this memory passes to it.
        StatsThreadArgs* args_ptr = CdiOsMemAlloc(sizeof(*args_ptr));
        if (NULL == args_ptr) {
            rs = kCdiStatusNotEnoughMemory;
        } else {
            args_ptr->stats_state_ptr = stats_state_ptr;
            args_ptr->send_stats_message_ptr = SendToCdiMetricsService;
            args_ptr->metrics_destination_idx = kMetricsDestinationGatheringService;
            args_ptr->stats_period_ms = metrics_gathering_period_ms;
            if (!CdiOsThreadCreate(StatsThread, &stats_state_ptr->
                                   destination_info[kMetricsDestinationGatheringService].stats_thread_id, "StatsThread",
                                   args_ptr, stats_state_ptr->con_state_ptr->start_signal)) {
                CdiOsMemFree(args_ptr);
                rs = kCdiStatusCreateThreadFailed;
            }
        }
    }
#endif  // METRICS_GATHERING_SERVICE_ENABLED

    // NOTE: The worker thread StatsThread() is created/destroyed dynamically by StatsConfigure(), depending if stats
    // is enabled or disabled.
    if (kCdiStatusOk != rs) {
        StatsDestroy((StatisticsHandle)stats_state_ptr);
        stats_state_ptr = NULL;
    }

    *return_handle_ptr = (StatisticsHandle)stats_state_ptr;

    return rs;
}

CdiReturnStatus StatsDestroy(StatisticsHandle handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    StatisticsState* stats_state_ptr = (StatisticsState*)handle;
    if (stats_state_ptr) {
        for (int i = 0 ; i < kMetricsDestinationsCount ; i++) {
            StatsThreadDestroy(&stats_state_ptr->destination_info[i]);
        }
        // Now that the thread has stopped, it is safe to clean up the remaining resources.

        CloudWatchDestroy(stats_state_ptr->metrics_gatherer_handle);
        stats_state_ptr->metrics_gatherer_handle = NULL;

        CloudWatchDestroy(stats_state_ptr->cloudwatch_handle);
        stats_state_ptr->cloudwatch_handle = NULL;

        CdiOsCritSectionDelete(stats_state_ptr->stats_data_lock);
        stats_state_ptr->stats_data_lock = NULL;

        for (int i = 0 ; i < kMetricsDestinationsCount ; i++) {
            CdiOsSignalDelete(stats_state_ptr->destination_info[i].thread_exit_signal);
            stats_state_ptr->destination_info[i].thread_exit_signal = NULL;

            TDigestDestroy(stats_state_ptr->destination_info[i].td_handle);
            stats_state_ptr->destination_info[i].td_handle = NULL;
        }

        CdiOsMemFree(stats_state_ptr);
        stats_state_ptr = NULL;
    }

    return kCdiStatusOk;
}

CdiReturnStatus StatsConfigure(StatisticsHandle handle, const CdiStatsConfigData* stats_config_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    StatisticsState* stats_state_ptr = (StatisticsState*)handle;

    // The StatsThread() is created/destroyed here dynamically as needed in order to minimize thread resources. Other
    // than during system startup, this function will typically not be used very often.
    StatsThreadDestroy(&stats_state_ptr->destination_info[kMetricsDestinationCloudWatch]);

    // Set stats period, converting seconds to milliseconds.
    stats_state_ptr->stats_period_ms = stats_config_ptr->stats_period_seconds * 1000;

    // If stats period is non-zero and either the user-registered callback exists or CloudWatch exist and is not
    // disabled, then create the stats thread.
    if (stats_state_ptr->stats_period_ms && (stats_state_ptr->user_cb_ptr ||
        (stats_state_ptr->cloudwatch_handle && !stats_config_ptr->disable_cloudwatch_stats))) {
        // The args need to be allocated on the heap since the thread needs access to it after this block ends. If the
        // thread is successfully created, ownership of this memory passes to it.
        StatsThreadArgs* args_ptr = CdiOsMemAlloc(sizeof(*args_ptr));
        if (NULL == args_ptr) {
            rs = kCdiStatusNotEnoughMemory;
        } else {
            args_ptr->stats_state_ptr = stats_state_ptr;
            args_ptr->send_stats_message_ptr = SendUserStatsMessage;
            args_ptr->metrics_destination_idx = kMetricsDestinationCloudWatch;
            args_ptr->stats_period_ms = stats_state_ptr->stats_period_ms;
            if (!CdiOsThreadCreate(StatsThread, &stats_state_ptr->
                                   destination_info[kMetricsDestinationCloudWatch].stats_thread_id, "StatsThread",
                                   args_ptr, stats_state_ptr->con_state_ptr->start_signal)) {
                CdiOsMemFree(args_ptr);
                rs = kCdiStatusCreateThreadFailed;
            }
        }
    }

    if (stats_state_ptr->cloudwatch_handle) {
        rs = CloudWatchConfigure(stats_state_ptr->cloudwatch_handle, stats_config_ptr);
    }

    return rs;
}

void StatsGatherPayloadStatsFromConnection(CdiEndpointState* endpoint_ptr, bool payload_ok,
                                           uint64_t start_time, uint64_t max_latency_microsecs)
{
    StatisticsState* stats_state_ptr = endpoint_ptr->connection_state_ptr->stats_state_ptr;
    CdiPayloadCounterStats* counter_stats_ptr = &endpoint_ptr->transfer_stats.payload_counter_stats;
    CdiPayloadTimeIntervalStats* interval_stats_ptr = &endpoint_ptr->transfer_stats.payload_time_interval_stats;
    uint64_t current_time = CdiOsGetMicroseconds();
    uint64_t elapsed_time = current_time - start_time;

    bool payload_late = false;
    if (payload_ok) {
        if (elapsed_time > max_latency_microsecs) {
            payload_late = true;
            CDI_LOG_THREAD(kLogWarning,
                           "Connection[%s] Stream[%s] Payload[%lu] was late by[%llu] microseconds. Max[%llu]",
                           endpoint_ptr->connection_state_ptr->saved_connection_name_str, endpoint_ptr->stream_name_str,
                           counter_stats_ptr->num_payloads_transferred,
                           elapsed_time - max_latency_microsecs, max_latency_microsecs);
        }
    }

    // Update stats. NOTE: Need to synchronize with reads/writes of data used here since it is also used by
    // StatsThread().
    CdiOsCritSectionReserve(stats_state_ptr->stats_data_lock);

    // Add sample to the t-Digests.
    for (int i = 0 ; i < kMetricsDestinationsCount ; i++) {
        TDigestAddSample(stats_state_ptr->destination_info[i].td_handle, elapsed_time);
    }

    // Keep running sum of all payload times this interval.
    interval_stats_ptr->transfer_time_sum += elapsed_time;

    if (payload_late) {
        counter_stats_ptr->num_payloads_late++;
    }

    if (payload_ok) {
        counter_stats_ptr->num_payloads_transferred++;
    } else {
        // This value is also incremented in TxPayloadThread(), so use atomic operation here.
        CdiOsAtomicInc32(&counter_stats_ptr->num_payloads_dropped);
    }

    // Done with stats data, so release the lock.
    CdiOsCritSectionRelease(stats_state_ptr->stats_data_lock);
}
