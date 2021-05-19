// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions that dynamically test CDI functionality. This may include features such as
 * enabling/disabling connections, reconfigurating statistics gathering, changing payload configurations, exercising
 * corner cases and injecting error conditions.
 */

#include "test_dynamic.h"

#include "cdi_logger_api.h"
#include "cdi_test.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief For statistics configuration changes, this is the number of configured interval periods that must expire
/// before applying a configuration change.
#define STATS_RECONFIGURE_INTERVAL_FACTOR   (4)

/// @brief For statistics configuration changes, this is how much to increase the configured interval period when
/// performing the change interval test.
#define STATS_PERIOD_MULT_FACTOR            (2)

/// @brief For endpoint configuration changes, this is the number of milliseconds that the endpoint will be enabled.
#define ENDPOINT_ENABLED_TIME_MS            (5000)

/// @brief For endpoint configuration changes, this is the number of milliseconds that the endpoint will be disabled.
#define ENDPOINT_DISABLED_TIME_MS           (1000)

/**
 * @brief Enums used to indicate configuration change states.
 */
typedef enum {
    kStatsModeChangeInterval,       ///< Change interval period (see STATS_INTERVAL_MULT_FACTOR)
    kStatsModeDisable,              ///< Disable stats gathering.
    kStatsModeSetOriginalSettings,  ///< Set original settings.

    kStateModeLast                  ///< Used for range checking. Do not remove.
} TestStatsState;

/**
 * @brief Structure used to hold state data for the dynamic statistics reconfiguration test used by
 * TestDynamicStatsReconfigure().
 */
typedef struct {
    /// @brief What time to advance to the next state, in milliseconds.
    uint64_t state_change_time_ms;
    TestStatsState test_state; ///< Current test stats state.
} StatsData;

/**
 * @brief Enums used to indicate configuration change states.
 */
typedef enum {
    kEndpointEnabled,  ///< Endpoint is enabled.
    kEndpointDisabled, ///< Endpoint is disabled.

    kEndpointLast      ///< Used for range checking. Do not remove.
} TestEndpointState;

/**
 * @brief Structure used to hold state data for the dynamic statistics reconfiguration test used by
 * TestDynamicStatsReconfigure().
 */
typedef struct {
    /// @brief What time to advance to the next state, in milliseconds.
    uint64_t state_change_time_ms;
    TestEndpointState test_state; ///< Current test endpoint state.
} EndpointData;

// Add additional defines and types here for new features.

/**
 * @brief Structure used to hold state data for dynamic tests.
 */
struct TestDynamicState {
    TestConnectionInfo* connection_info_ptr; ///< Test connection state data.

    StatsData stats_data;   ///< Statistics reconfiguration state data.
    EndpointData endpoint_data; ///< Endpoint state data.
    // Add additional state data here for new features.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Apply statistics reconfiguration, based on current test state.
 *
 * @param handle Handle of the test dynamic component.
 *
 * @return true if successful, otherwise false is returned.
 */
static bool StatisticsReconfigure(TestDynamicHandle handle)
{
    bool ret = true;
    TestDynamicState* state_ptr = (TestDynamicState*)handle;
    TestConnectionInfo* connection_info_ptr = state_ptr->connection_info_ptr;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    StatsData* stats_ptr = &state_ptr->stats_data;

    CdiStatsConfigData* original_stats_config_ptr = NULL;
    if (test_settings_ptr->tx) {
        original_stats_config_ptr = &connection_info_ptr->config_data.tx.stats_config;
    } else {
        original_stats_config_ptr = &connection_info_ptr->config_data.rx.stats_config;
    }

    CdiStatsConfigData stats_config = *original_stats_config_ptr;

    switch (stats_ptr->test_state) {
        case kStatsModeChangeInterval:
        {
            int new_period = stats_config.stats_period_seconds * STATS_PERIOD_MULT_FACTOR;
            CDI_LOG_THREAD(kLogInfo, "Change stats period from [%d] sec to [%d]secs", stats_config.stats_period_seconds,
                           new_period);
            stats_config.stats_period_seconds = new_period;
            break;
        }
        case kStatsModeDisable:
            memset(&stats_config, 0, sizeof(stats_config));
            CDI_LOG_THREAD(kLogInfo, "Disable stats gathering.");
            break;
        case kStatsModeSetOriginalSettings:
            // Nothing special to do.
            CDI_LOG_THREAD(kLogInfo, "Restore original stats configuration.");
            break;
        case kStateModeLast:
            CDI_LOG_THREAD(kLogError, "Entering the last mode state. Currently this state is unused.");
            ret = false;
            break;
    }
    // Advance to next state.
    if (++stats_ptr->test_state >= kStateModeLast) {
        stats_ptr->test_state = 0;
    }

    if (kCdiStatusOk != CdiCoreStatsReconfigure(connection_info_ptr->connection_handle, &stats_config)) {
        ret = false;
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool TestDynamicCreate(TestConnectionInfo* connection_info_ptr, TestDynamicHandle* return_handle_ptr)
{
    bool ret = true;

    // Allocate a state structure.
    TestDynamicState* state_ptr = CdiOsMemAllocZero(sizeof(TestDynamicState));
    if (state_ptr == NULL) {
        return false;
    }

    state_ptr->connection_info_ptr = connection_info_ptr;

    *return_handle_ptr = (TestDynamicHandle)state_ptr;

    return ret;
}

void TestDynamicDestroy(TestDynamicHandle handle)
{
    TestDynamicState* state_ptr = (TestDynamicState*)handle;
    if (state_ptr) {
        CdiOsMemFree(state_ptr);
        state_ptr = NULL;
    }
}

bool TestDynamicPollStatsReconfigure(TestDynamicHandle handle)
{
    bool ret = true;
    TestDynamicState* state_ptr = (TestDynamicState*)handle;
    TestSettings* test_settings_ptr = state_ptr->connection_info_ptr->test_settings_ptr;
    StatsData* stats_ptr = &state_ptr->stats_data;

    // If enabled, apply next stats reconfiguration after desired time interval has expired.
    if (test_settings_ptr->stats_period_seconds) {
        uint64_t current_time_ms = CdiOsGetMilliseconds();
        bool first_time = (0 == stats_ptr->state_change_time_ms);

        if (first_time || current_time_ms >= stats_ptr->state_change_time_ms) {
            // Set next state change time, converting seconds to millisconds.
            stats_ptr->state_change_time_ms = current_time_ms +
                    (test_settings_ptr->stats_period_seconds * 1000) * STATS_RECONFIGURE_INTERVAL_FACTOR;
            if (!first_time) {
                ret = StatisticsReconfigure(handle);
            }
        }
    }

    return ret;
}

bool TestDynamicEndpoints(TestDynamicHandle handle)
{
    bool ret = true;
    TestDynamicState* state_ptr = (TestDynamicState*)handle;
    TestConnectionInfo* connection_info_ptr = state_ptr->connection_info_ptr;
    TestSettings* test_settings_ptr = connection_info_ptr->test_settings_ptr;
    EndpointData* data_ptr = &state_ptr->endpoint_data;

    if (test_settings_ptr->multiple_endpoints) {
        uint64_t current_time_ms = CdiOsGetMilliseconds();
        if (0 == data_ptr->state_change_time_ms) {
            // First time.
            data_ptr->state_change_time_ms = current_time_ms + ENDPOINT_ENABLED_TIME_MS;
            data_ptr->test_state = kEndpointEnabled;
        } else if (current_time_ms >= data_ptr->state_change_time_ms) {
            int i = 0;
            StreamSettings* stream_settings_ptr = &test_settings_ptr->stream_settings[i];

            if (kEndpointEnabled == data_ptr->test_state) {
                CDI_LOG_THREAD(kLogInfo, "Destroying endpoint Stream ID[%d]", stream_settings_ptr->stream_id);
                CdiAvmStreamEndpointDestroy(connection_info_ptr->tx_stream_endpoint_handle_array[i]);
                connection_info_ptr->tx_stream_endpoint_handle_array[i] = NULL;
                data_ptr->state_change_time_ms = current_time_ms + ENDPOINT_DISABLED_TIME_MS;
                data_ptr->test_state = kEndpointDisabled;
            } else {
                CDI_LOG_THREAD(kLogInfo, "Creating endpoint Stream ID[%d]", stream_settings_ptr->stream_id);
                CdiTxConfigDataStream stream_config = { 0 };
                stream_config.dest_ip_addr_str = stream_settings_ptr->remote_adapter_ip_str;
                stream_config.dest_port = stream_settings_ptr->dest_port;
                stream_config.stream_name_str = NULL;
                ret = (kCdiStatusOk == CdiAvmTxStreamEndpointCreate(connection_info_ptr->connection_handle,
                    &stream_config, &connection_info_ptr->tx_stream_endpoint_handle_array[i]));
                data_ptr->state_change_time_ms = current_time_ms + ENDPOINT_ENABLED_TIME_MS;
                data_ptr->test_state = kEndpointEnabled;
            }
        }
    }

    return ret;
}

bool TestDynamicIsEndpointEnabled(TestDynamicHandle handle, int stream_index)
{
    bool ret = true;
    TestDynamicState* state_ptr = (TestDynamicState*)handle;
    TestConnectionInfo* connection_info_ptr = state_ptr->connection_info_ptr;

    if (connection_info_ptr->test_settings_ptr->multiple_endpoints) {
        ret = (NULL != connection_info_ptr->tx_stream_endpoint_handle_array[stream_index]);
    }

    return ret;
}
