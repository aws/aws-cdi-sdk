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

#include "cloudwatch.h"

#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cloudwatch_sdk_metrics.h"
#include "internal_log.h"
#include "statistics.h"

#ifdef CLOUDWATCH_METRICS_ENABLED

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Structure used to hold state data for statistics.
 */
struct CloudWatchState {
    CdiConnectionState* con_state_ptr;  ///< Pointer to connection state data.
    CloudWatchSdkMetricsHandle cw_sdk_handle; ///< CloudWatch SDK instance handle.

    CdiSignalType thread_exit_signal;   ///< Signal used to control exit of the CloudWatch thread.
    CdiThreadID cw_thread_id;           ///< CloudWatch thread ID.

    CdiFifoHandle stat_fifo_handle;     ///< Handle of statistics FIFO.

    CdiCsID config_data_lock;           ///< Lock used to protect access to stats configuration data.
    CdiStatsConfigData config_data;     ///< Stats configuration data.

    bool previous_stats_valid;          ///< If true, previous stats are valid, otherwise have not set them yet.
    CdiTransferStats previous_stats;    ///< Copy of previous stats, used to generate deltas to send to CloudWatch.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Function that will be called whenever the CloudWatch stats FIFO is full. It accumulates the new statistics into the
 * set waiting at the head of the queue.
 *
 * @param cb_data_ptr The address of the callback data structure cantaining the parameters provided by the FIFO.
 */
static void FifoFullCallback(const CdiFifoFullCbData* cb_data_ptr)
{
    // Not used, but here as an example:
    // CloudWatchState* cw_state_ptr = (CloudWatchState*)cb_data_ptr->fifo_user_cb_param;

    // The FIFO was full, so accumulate the new stats into the last entry in the FIFO.
    CDI_LOG_THREAD(kLogError, "FIFO[%s] write failed: FIFO full. Accumulating the statistic into last FIFO entry.",
                   CdiFifoGetName(cb_data_ptr->fifo_handle));

    // Get pointers to new and last items. NOTE: Last item is the tail of the FIFO.
    const CdiTransferStats* new_stats_ptr = cb_data_ptr->new_item_data_ptr;
    CdiTransferStats* last_stats_ptr = cb_data_ptr->head_item_data_ptr;

    // Accumulate stats that are reset each period (each time this function is called). For other stats, use the latest
    // stat (timestamp and counters that don't reset).
    const CdiPayloadTimeIntervalStats* src_ptr = &new_stats_ptr->payload_time_interval_stats;
    CdiPayloadTimeIntervalStats* dest_ptr = &last_stats_ptr->payload_time_interval_stats;

    // Take timestamp and counter based data from the new stat.
    last_stats_ptr->timestamp_in_ms_since_epoch = new_stats_ptr->timestamp_in_ms_since_epoch;
    last_stats_ptr->payload_counter_stats = new_stats_ptr->payload_counter_stats;

    // Accumulate time-interval based stats. Update the counters.
    dest_ptr->transfer_count += src_ptr->transfer_count;
    dest_ptr->transfer_time_sum += src_ptr->transfer_time_sum;

    // When dealing with percentiles, when the fifo is full, replace the last element with our new results only if the
    // new results are higher. That way, in the event of data loss, we preserve the worst-case numbers. The only case
    // where we go with the lower number is the minimum.
    if (src_ptr->transfer_time_min < dest_ptr->transfer_time_min || (0 == dest_ptr->transfer_time_min)) {
        dest_ptr->transfer_time_min = src_ptr->transfer_time_min;
    }
    if (src_ptr->transfer_time_P50 > dest_ptr->transfer_time_P50) {
        dest_ptr->transfer_time_P50 = src_ptr->transfer_time_P50;
    }
    if (src_ptr->transfer_time_P90 > dest_ptr->transfer_time_P90) {
        dest_ptr->transfer_time_P90 = src_ptr->transfer_time_P90;
    }
    if (src_ptr->transfer_time_P99 > dest_ptr->transfer_time_P99) {
        dest_ptr->transfer_time_P99 = src_ptr->transfer_time_P99;
    }
    if (src_ptr->transfer_time_max > dest_ptr->transfer_time_max) {
        dest_ptr->transfer_time_max = src_ptr->transfer_time_max;
    }
}

/**
 * This function computes the differences in the statistics contained in two data sets.
 *
 * @param cw_state_ptr Pointer to the CloudWatch statistics state.
 * @param transfer_stats_ptr Address of the new statistics data set.
 * @param delta_stats_ptr Pointer to where the results are to be written.
 */
static void CalculateDeltas(CloudWatchState* cw_state_ptr, const CdiTransferStats* transfer_stats_ptr,
                            CloudWatchCounterBasedDeltas* delta_stats_ptr)
{
    CdiPayloadCounterStats* prev_counter_stats_ptr = &cw_state_ptr->previous_stats.payload_counter_stats;

    // Don't add stats where we cannot calculate deltas.
    if (cw_state_ptr->previous_stats_valid) {
        const CdiPayloadCounterStats* counter_stats_ptr = &transfer_stats_ptr->payload_counter_stats;

        delta_stats_ptr->delta_num_payloads_dropped = (int)(counter_stats_ptr->num_payloads_dropped -
                                                      prev_counter_stats_ptr->num_payloads_dropped);

        delta_stats_ptr->delta_num_payloads_late = (int)(counter_stats_ptr->num_payloads_late -
                                                    prev_counter_stats_ptr->num_payloads_late);

        delta_stats_ptr->delta_num_bytes_transferred = counter_stats_ptr->num_bytes_transferred -
                                                    prev_counter_stats_ptr->num_bytes_transferred;

        const CdiAdapterEndpointStats* endpoint_stats_ptr = &transfer_stats_ptr->endpoint_stats;
        const CdiAdapterEndpointStats* prev_endpoint_stats_ptr = &cw_state_ptr->previous_stats.endpoint_stats;
        delta_stats_ptr->delta_dropped_connection_count = endpoint_stats_ptr->dropped_connection_count -
                                                          prev_endpoint_stats_ptr->dropped_connection_count;

        delta_stats_ptr->delta_probe_command_retry_count = endpoint_stats_ptr->probe_command_retry_count -
                                                           prev_endpoint_stats_ptr->probe_command_retry_count;
    }
}

/**
 * Statistic gathering thread used to invoke registered callback functions when new statistics are available.
 *
 * @param ptr Pointer to thread specific data. In this case, a pointer to CloudWatchState.
 *
 * @return The return value is not used.
 */
static CDI_THREAD CloudWatchThread(void* ptr)
{
    CloudWatchState* cw_state_ptr = (CloudWatchState*)(ptr);

    // Set this thread to use the connection's log. Can now use CDI_LOG_THREAD() for logging within this thread.
    CdiLoggerThreadLogSet(cw_state_ptr->con_state_ptr->log_handle);

    // Loop until thread exit signal received.
    while (!CdiOsSignalGet(cw_state_ptr->thread_exit_signal)) {
        // Wait on read data or thread exit signal.
        CdiTransferStats transfer_stats;
        if (CdiFifoRead(cw_state_ptr->stat_fifo_handle, CDI_INFINITE, cw_state_ptr->thread_exit_signal,
                        &transfer_stats)) {

            // Get latest stats data.
            CdiOsCritSectionReserve(cw_state_ptr->config_data_lock);
            CdiStatsConfigData config_data = cw_state_ptr->config_data;
            CdiOsCritSectionRelease(cw_state_ptr->config_data_lock);

            CloudWatchTransferStats cw_stats = {
                .timestamp_in_ms_since_epoch = transfer_stats.timestamp_in_ms_since_epoch,
                .dimension_connection_str = cw_state_ptr->con_state_ptr->saved_connection_name_str,
                // Enable high-resolution storage mode for periods less than 60 seconds.
                .high_resolution = (config_data.stats_period_seconds < 60),
                .payload_time_interval_stats = transfer_stats.payload_time_interval_stats,
                .connected = transfer_stats.endpoint_stats.connected,
                .cpu_utilization = transfer_stats.endpoint_stats.poll_thread_load,
                .is_receiver = cw_state_ptr->con_state_ptr->handle_type == kHandleTypeRx,
            };
            CdiOsStrCpy(cw_stats.dimension_stream_str, sizeof(cw_stats.dimension_stream_str),
                        transfer_stats.stream_name_str);

            CalculateDeltas(cw_state_ptr, &transfer_stats, &cw_stats.count_based_delta_stats);

            // Setup initial throttling delay to start at set 1/10th of the stats gathering period. Convert period from
            // seconds to milliseconds. For throttling suggestions/algorithms, see:
            // https://aws.amazon.com/premiumsupport/knowledge-center/cloudwatch-400-error-throttling
            // The CloudWatch SDK API function PutMetricData() has a limit of 150 transactions per second. This limit
            // can be increased by requesting a quota increase through AWS.
            // Convert period from seconds to milliseconds.
            uint32_t throttle_timeout_ms = (config_data.stats_period_seconds*1000)/10;
            CdiReturnStatus rs = kCdiStatusOk;
            do {
                if (config_data.disable_cloudwatch_stats) {
                    // CloudWatch stats disabled, so clear previous stats and exit this loop.
                    cw_state_ptr->previous_stats_valid = false;
                    memset(&cw_state_ptr->previous_stats, 0, sizeof(cw_state_ptr->previous_stats));
                    break;
                }

                // Use the AWS SDK to send the stats to CloudWatch.
                rs = MetricsSend(cw_state_ptr->cw_sdk_handle, &cw_stats);
                if (kCdiStatusOk == rs) {
                    // Successfully sent the stats, so save a copy to use for calculating deltas in CalculateDeltas().
                    cw_state_ptr->previous_stats = transfer_stats;
                    cw_state_ptr->previous_stats_valid = true;
                } else if (kCdiStatusCloudWatchThrottling != rs) {
                    CDI_LOG_THREAD(kLogError, "CloudWatchSdkMetricsSend failed. Reason[%s].",
                                   CdiCoreStatusToString(rs));
                } else {
                    // Received throttling error from AWS SDK, so setup to sleep for a while and then retry sending the
                    // request again by staying in this loop.
                    bool timed_out = false;
                    CdiOsSignalWait(cw_state_ptr->con_state_ptr->shutdown_signal, throttle_timeout_ms, &timed_out);
                    if (timed_out) {
                        // Got timeout, so increase the next timeout value used but cap at timeout.
                        const uint32_t new_timeout = throttle_timeout_ms * 2;
                        if (new_timeout < config_data.stats_period_seconds * 1000) {
                            CDI_LOG_THREAD(kLogInfo, "Increasing stat sleep timeout.");
                            throttle_timeout_ms = new_timeout;
                        }

                        // Get latest stats data.
                        CdiOsCritSectionReserve(cw_state_ptr->config_data_lock);
                        config_data = cw_state_ptr->config_data;
                        CdiOsCritSectionRelease(cw_state_ptr->config_data_lock);
                    }
                }
            } while (kCdiStatusCloudWatchThrottling == rs);
        }
    }

    // Since we are shutting down, ensure FIFO is flushed.
    CdiFifoFlush(cw_state_ptr->stat_fifo_handle);

    return 0; // Return code not used.
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus CloudWatchCreate(CdiConnectionState* con_state_ptr, CloudWatchSdkMetricsHandle cw_sdk_handle,
                                 CloudWatchHandle* return_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use CDI_LOG_HANDLE() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    // Allocate a generic CloudWatch state structure.
    CloudWatchState* cw_state_ptr = CdiOsMemAllocZero(sizeof(CloudWatchState));
    if (cw_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
       if (!CdiOsCritSectionCreate(&cw_state_ptr->config_data_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (rs == kCdiStatusOk) {
        if (!CdiFifoCreate("CloudWatch Stat FIFO", CLOUDWATCH_STATS_FIFO_DEPTH, sizeof(CdiTransferStats),
                           FifoFullCallback, cw_state_ptr, &cw_state_ptr->stat_fifo_handle)) {
            CDI_LOG_HANDLE(con_state_ptr->log_handle, kLogError, "CloudWatch stat FIFO creation failed.");
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    if (rs == kCdiStatusOk) {
        cw_state_ptr->con_state_ptr = con_state_ptr;
        cw_state_ptr->cw_sdk_handle = cw_sdk_handle;

        if (!CdiOsSignalCreate(&cw_state_ptr->thread_exit_signal)) {
            rs = kCdiStatusAllocationFailed;
        }
    }

    if (kCdiStatusOk == rs) {
        // Create worker thread.
        if (!CdiOsThreadCreate(CloudWatchThread, &cw_state_ptr->cw_thread_id, "CloudWatchThread",
                               cw_state_ptr, con_state_ptr->start_signal)) {
            rs = kCdiStatusCreateThreadFailed;
        }
    }

    if (rs != kCdiStatusOk) {
        CloudWatchDestroy((CloudWatchHandle)cw_state_ptr);
        cw_state_ptr = NULL;
    }

    *return_handle_ptr = (CloudWatchHandle)cw_state_ptr;

    return rs;
}

CdiReturnStatus CloudWatchDestroy(CloudWatchHandle handle)
{
    // NOTE: Since the caller is the application's thread, use CDI_LOG_HANDLE() for any logging in this function.
    CloudWatchState* cw_state_ptr = (CloudWatchState*)handle;

    if (cw_state_ptr) {
        if (cw_state_ptr->cw_thread_id) {
            // Cloudwatch thread exist, so signal it to exit and then wait for it to actually exit.
            CdiOsSignalSet(cw_state_ptr->thread_exit_signal);
            CdiOsThreadJoin(cw_state_ptr->cw_thread_id, CDI_INFINITE, NULL);
            cw_state_ptr->cw_thread_id = NULL;
        }

        CdiOsSignalDelete(cw_state_ptr->thread_exit_signal);
        cw_state_ptr->thread_exit_signal = NULL;

        CdiFifoDestroy(cw_state_ptr->stat_fifo_handle);
        cw_state_ptr->stat_fifo_handle = NULL;

        CdiOsCritSectionDelete(cw_state_ptr->config_data_lock);
        cw_state_ptr->config_data_lock = NULL;

        CdiOsMemFree(cw_state_ptr);
        cw_state_ptr = NULL;
    }

    return kCdiStatusOk;
}

CdiReturnStatus CloudWatchConfigure(CloudWatchHandle handle, const CdiStatsConfigData* stats_config_ptr)
{
    // NOTE: Since the caller is the application's thread, use CDI_LOG_HANDLE() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;
    CloudWatchState* cw_state_ptr = (CloudWatchState*)handle;

    CdiOsCritSectionReserve(cw_state_ptr->config_data_lock);
    cw_state_ptr->config_data = *stats_config_ptr;
    CdiOsCritSectionRelease(cw_state_ptr->config_data_lock);

    return rs;
}

void CloudWatchStatisticsMessage(CloudWatchHandle handle, int stats_count, const CdiTransferStats* transfer_stats_array)
{
    CloudWatchState* cw_state_ptr = (CloudWatchState*)handle;

    // Don't add stats to FIFO if shutting down.
    if (!CdiOsSignalGet(cw_state_ptr->con_state_ptr->shutdown_signal)) {
        // If the FIFO is full, the FifoFullCallback() is invoked, where we will accumulate them into the last stats on
        // the tail of the FIFO.
        for (int i = 0; i < stats_count; i++) {
            CdiFifoWrite(cw_state_ptr->stat_fifo_handle, 0, NULL, &transfer_stats_array[i]);
        }
    }
}

#else // CLOUDWATCH_METRICS_ENABLED

#if defined(METRICS_GATHERING_SERVICE_ENABLED)
#error CLOUDWATCH_METRICS_ENABLED must be defined when METRICS_GATHERING_SERVICE_ENABLED is defined.
#endif

// Stub functions.
CdiReturnStatus CloudWatchCreate(CdiConnectionState* con_state_ptr, CloudWatchSdkMetricsHandle cw_sdk_handle,
                                CloudWatchHandle* return_handle_ptr)
{
    (void)con_state_ptr;
    (void)cw_sdk_handle;
    *return_handle_ptr = NULL;
    return kCdiStatusOk;
}
CdiReturnStatus CloudWatchDestroy(CloudWatchHandle handle)
{
    (void)handle;
    return kCdiStatusOk;
}

CdiReturnStatus CloudWatchConfigure(CloudWatchHandle handle, const CdiStatsConfigData* stats_config_ptr)
{
    (void)handle;
    (void)stats_config_ptr;
    return kCdiStatusOk;
}

void CloudWatchStatisticsMessage(CloudWatchHandle handle, int stats_count, const CdiTransferStats* transfer_stats_array)
{
    (void)handle;
    (void)stats_count;
    (void)transfer_stats_array;
}

#endif // !CLOUDWATCH_METRICS_ENABLED
