// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

 /**
  * @file
  * @brief
  * The declarations in this header file correspond to the definitions in cloudwatch.c.
  */

 #ifndef CDI_CLOUDWATCH_H__
 #define CDI_CLOUDWATCH_H__

 #include "configuration.h"
 #include "private.h"
 #include "cdi_core_api.h"

 //*********************************************************************************************************************
 //***************************************** START OF DEFINITIONS AND TYPES ********************************************
 //*********************************************************************************************************************

/// Forward reference of structure to create pointers later.
typedef struct CloudWatchState CloudWatchState;

 /**
  * @brief Type used as the handle (pointer to an opaque structure) for managing statistics for a connection. Each handle
  * represents a single data flow.
  */
 typedef struct CloudWatchState* CloudWatchHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of the statistics component for the specified connection.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param cw_sdk_handle CloudWatch SDK instance handle
 * @param return_handle_ptr Address where to write returned statistics handle.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CloudWatchCreate(CdiConnectionState* con_state_ptr, CloudWatchSdkMetricsHandle cw_sdk_handle,
                                 CloudWatchHandle* return_handle_ptr);

/**
 * Free all resources related to the specified statistics component.
 *
 * @param handle Handle of statistics component.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CloudWatchDestroy(CloudWatchHandle handle);

/**
 * Configure transfer statistics.
 *
 * @param handle The handle of the connection to set statistics configuration.
 * @param stats_config_ptr Pointer to stats configuration data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CloudWatchConfigure(CloudWatchHandle handle, const CdiStatsConfigData* stats_config_ptr);

/**
 * Process a message that contains new statistics to post to CloudWatch.
 *
 * @param handle Handle of statistics component.
 * @param stats_count Number of array items in transfer_stats_array.
 * @param transfer_stats_array Pointer to start of transfer stats array.
 */
void CloudWatchStatisticsMessage(CloudWatchHandle handle, int stats_count,
                                 const CdiTransferStats* transfer_stats_array);

 #endif  // CDI_CLOUDWATCH_H__
