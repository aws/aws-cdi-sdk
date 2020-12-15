// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in statistics.c.
 */

#ifndef CDI_STATISTICS_H__
#define CDI_STATISTICS_H__

// The configuration.h file must be included first since it can have defines that affect subsequent files.
#include "configuration.h"

#include "cdi_core_api.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for managing statistics for a connection. Each handle
 * represents a single data flow.
 */
typedef struct StatisticsState* StatisticsHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of the statistics component for the specified connection.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param stats_cb_ptr Address of user-defined callback function.
 * @param stats_user_cb_param Parameter used in structure passed to stats_cb_ptr.
 * @param cw_sdk_handle Handle to CloudWatch Metrics instance.
 * @param metrics_gatherer_sdk_handle Handle to CDI metrics gatherer instance.
 * @param return_handle_ptr Address where to write returned statistics handle.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus StatsCreate(CdiConnectionState* con_state_ptr, CdiCoreStatsCallback stats_cb_ptr,
                            CdiUserCbParameter stats_user_cb_param, CloudWatchSdkMetricsHandle cw_sdk_handle,
                            CloudWatchSdkMetricsHandle metrics_gatherer_sdk_handle,
                            StatisticsHandle* return_handle_ptr);

/**
 * Free all resources related to the specified statistics component.
 *
 * @param handle Handle of statistics component.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus StatsDestroy(StatisticsHandle handle);

/**
 * Configure transfer statistics.
 *
 * @param handle The handle of the connection to set statistics configuration.
 * @param stats_config_ptr Pointer to statistics configuration data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus StatsConfigure(StatisticsHandle handle, const CdiStatsConfigData* stats_config_ptr);

/**
 * Gather transfer time statistics for a single payload from a connection.
 *
 * @param endpoint_ptr Pointer to endpoint state data.
 * @param payload_ok Use true if payload was successfully transferred, otherwise false.
 * @param start_time Time when transfer for the payload started in microseconds.
 * @param max_latency_microsecs The specified maximum latency in microseconds of the payload.
 * @param bytes_transferred The number of bytes transferred for this connection.
 */
void StatsGatherPayloadStatsFromConnection(CdiEndpointState* endpoint_ptr, bool payload_ok, uint64_t start_time,
                                           uint64_t max_latency_microsecs, uint64_t bytes_transferred);

#endif  // CDI_STATISTICS_H__
