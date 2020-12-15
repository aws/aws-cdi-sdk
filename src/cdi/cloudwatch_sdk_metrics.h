// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

 /**
  * @file
  * @brief
  * The declarations in this header file correspond to the definitions in cloudwatch_sdk_metrics.cpp.
  */

 #ifndef CDI_CLOUDWATCH_SDK_METRICS_H__
 #define CDI_CLOUDWATCH_SDK_METRICS_H__

#include "cdi_core_api.h"

 //*********************************************************************************************************************
 //***************************************** START OF DEFINITIONS AND TYPES ********************************************
 //*********************************************************************************************************************

/// Forward reference of structure to create pointers later.
typedef struct CloudWatchSdkMetrics CloudWatchSdkMetrics;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for managing statistics for a connection. Each handle
 * represents a single data flow.
 */
typedef struct CloudWatchSdkMetrics* CloudWatchSdkMetricsHandle;

/**
 * @brief Counter based statistics data that contain deltas since the last set was generated.
 */
typedef struct {
    /// @brief Current number of payloads successfully transferred since the connection was created.
    int delta_num_payloads_transferred;

    /// @brief The number of payloads that have been dropped due to timeout conditions since the connection was created.
    /// Payloads are typically dropped because of network connectivity issues but will also occur when the receiving
    /// host is unresponsive among other possible causes.
    int delta_num_payloads_dropped;

    /// @brief Number of payloads that were transmitted late since the connection was created.
    int delta_num_payloads_late;

    /// Number of times the connection has been lost.
    uint32_t delta_dropped_connection_count;

    /// Number of probe command retries due to dropped/lost control packets. The control protocol is UDP based and does
    /// not use the SRD hardware. This provides a secondary channel of communication.
    uint32_t delta_probe_command_retry_count;

    /// Number of bytes transferred over the stats period.
    uint64_t delta_num_bytes_transferred;
} CloudWatchCounterBasedDeltas;

/**
 * @brief A structure that is used to hold statistics gathering configuration data that is specific to CloudWatch.
 */
typedef struct {
    /// @brief Pointer to a string that defines a dimension called "Connection" that is associated with each metric.
    const char* dimension_connection_str;

    /// @brief A string that defines a dimension called "Stream" that is associated with each metric. A stream is single
    /// endpoint within a connection. One or more streams can exist in a connection.
    char dimension_stream_str[MAX_STREAM_NAME_STRING_LENGTH];

    /// @brief Time when last statistic of the set was gathered. Units is in milliseconds since epoch.
    uint64_t timestamp_in_ms_since_epoch;

    /// @brief If true, high resolution storage for metrics is used. This means metrics are stored at 1-second
    /// resolution. If false, metrics are stored at 1-minute resolution (CloudWatch default).
    bool high_resolution;

    CloudWatchCounterBasedDeltas count_based_delta_stats;    ///< Counter based stats that contain delta values.
    CdiPayloadTimeIntervalStats payload_time_interval_stats; ///< Payload time stats.

    bool connected;       ///< true if the connection is up, false if the connection is not connected.
    int cpu_utilization;  ///< CPU load of polling thread in hundreths of a percent.
    bool is_receiver;     ///< true if this endpoint is a receiver, false if a transmitter.
} CloudWatchTransferStats;

/**
 * @brief A structure that is used to hold statistics gathering configuration data for instantiating a metrics gathering
 * client handler.
 */
typedef struct {
    /// @brief Pointer to a string that defines a dimension called "Domain" that is associated with each metric. This
    /// value is required and cannot be NULL.
    const char* dimension_domain_str;
} MetricsGathererConfigData;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an instance of CloudWatch SDK metrics and open a connection.
 *
 * @param config_ptr Pointer to configuration data.
 * @param ret_handle_ptr Address where to write returned CloudWatch wrapper handle.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CloudWatchSdkMetricsCreate(const CloudWatchConfigData* config_ptr,
                                           CloudWatchSdkMetricsHandle* ret_handle_ptr);

/**
 * Free all resources related to the specified CloudWatch SDK metrics instance.
 *
 * @param handle Handle of CloudWatch SDK metrics instance.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CloudWatchSdkMetricsDestroy(CloudWatchSdkMetricsHandle handle);

/**
 * Create a CDI metrics gathering system client and open a connection to the service.
 *
 * @param config_ptr Pointer to configuration data.
 * @param ret_handle_ptr Address where to write returned handle.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus MetricsGathererCreate(const MetricsGathererConfigData* config_ptr,
                                      CloudWatchSdkMetricsHandle* ret_handle_ptr);

/**
 * Free all resources related to the specified CloudWatch SDK metrics instance.
 *
 * @param handle Handle of CDI metrics gathering service client instance.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus MetricsGathererDestroy(CloudWatchSdkMetricsHandle handle);

/**
 * Send transfer statistics using the specified pubishing client instance.
 *
 * @param handle The handle of the CloudWatch SDK metrics instance to send statistics to.
 * @param transfer_stats_ptr Pointer to stats to send.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus MetricsSend(CloudWatchSdkMetricsHandle handle, const CloudWatchTransferStats* transfer_stats_ptr);

#ifdef __cplusplus
}
#endif

#endif  // CDI_CLOUDWATCH_SDK_METRICS_H__
