// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * When payloads are received from the transmitter, they can arrive in any order. The routines here will put the
 * payloads in order.
 */

#include "rx_reorder_payloads.h"

#include "statistics.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief The maximum number of outstanding application payloads.
#define APP_PAYLOADS_MAX                (100)

/// @brief Number of packets in a payload.
#define PAYLOAD_PACKET_COUNT            (2)

/// @brief Expected reorder index when test completes.
#define EXPECTED_REORDER_INDEX          (8)

/// @brief Number of expected successful application payloads processed.
#define EXPECTED_APP_PAYLOAD_SUCCESSES  (7)

/// @brief Number of expected error application payloads processed.
#define EXPECTED_APP_PAYLOAD_ERRORS     (1)

/// @brief Number of expected ignore payloads remaining in state array when test completes.
#define EXPECTED_IGNORE_PAYLOADS        (2)

/// @brief Maximum payload latency in uS.
#define PAYLOAD_LATENCY_MAX             (16667)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus TestUnitRxReorderPayloads(void)
{
    CdiReturnStatus rs = kCdiStatusOk;

    CdiEndpointState endpoint_state = { 0 };
    CdiEndpointState* endpoint_ptr = &endpoint_state;

    AdapterEndpointState adapter_endpoint = { 0 };
    endpoint_ptr->adapter_endpoint_ptr = &adapter_endpoint;

    CdiConnectionState con_state = { 0 };
    CdiConnectionState* con_state_ptr = &con_state;
    endpoint_ptr->connection_state_ptr = con_state_ptr;

    if (kCdiStatusOk == rs) {
        rs = CdiOsSignalCreate(&con_state_ptr->shutdown_signal);
    }

    rs = StatsCreate(&con_state, NULL, NULL, NULL, NULL, &con_state.stats_state_ptr);

    if (kCdiStatusOk == rs) {
        if (!CdiQueueCreate("PayloadRequests AppPayloadCallbackData Queue", APP_PAYLOADS_MAX, CDI_FIXED_QUEUE_SIZE,
                            CDI_FIXED_QUEUE_SIZE,
                            sizeof(AppPayloadCallbackData), kQueueSignalPopWait, // Queue can block on pops.
                            &con_state_ptr->app_payload_message_queue_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        } else {
            con_state_ptr->rx_state.active_payload_complete_queue_handle =
                con_state_ptr->app_payload_message_queue_handle;
        }
    }

    if (kCdiStatusOk == rs) {
        if (!CdiPoolCreate("Error Messages Pool", APP_PAYLOADS_MAX, NO_GROW_SIZE, NO_GROW_COUNT,
                           MAX_ERROR_STRING_LENGTH, true, // true= Make thread-safe
                           &con_state_ptr->error_message_pool)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    CdiProtocolVersionNumber version = {
        .version_num = 1,
        .major_version_num = 0,
        .probe_version_num = 0
    };
    ProtocolVersionSet(&version, &endpoint_ptr->adapter_endpoint_ptr->protocol_handle);

    if (kCdiStatusOk == rs) {
        // Payload state pool.
        if (!CdiPoolCreate("Rx Payload State Pool", 100, NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(RxPayloadState), true, &con_state_ptr->rx_state.rx_payload_state_pool_handle)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    // Force to use 15 for max payload number to make test easy to manage.
    endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max = 16-1;
    CDI_LOG_THREAD(kLogInfo, "Forced payload_num_max=[%d].",
                   endpoint_ptr->adapter_endpoint_ptr->protocol_handle->payload_num_max);

    typedef struct {
        int payload_num;
        CdiPayloadState payload_state;
    } TestState;
    static TestState state_array[] = {
        // start_index=0 (see rx_state.rxreorder_current_index)
        { 2, kPayloadComplete }, // 2=out of order (expecting 0).

        // start_index=0.
        { 1, kPayloadInProgress }, // 1=out of order (expecting 0).

        // start_index=0.
        // 0=in order, sent to app. NOTE: We are forcing rxreorder_buffered_packet_count beyond max value to force an
        // error condition. As a result, 1 (in progress) is changed to an error and sent to app and then set to ignore.
        // 2 (complete) is also sent to app.
        { 0, kPayloadComplete },

        // start_index=3.
        { 3, kPayloadComplete }, // 3=in order, sent to app.

        // start_index=4.
        { 4, kPayloadInProgress }, // 4=in order, held due to in progress.

        // start_index=4.
        { 5, kPayloadError }, // 5=out of order (expecting 4).

        // start_index=4.
        { 4, kPayloadComplete }, // 4=in order, sent to app. Also, 5 (error) sent to app.

        // start_index=6.
        { 5, kPayloadIgnore }, // 5=old, saved as ignore (expecting 6).

        // start_index=6.
        { 6, kPayloadComplete }, // 6=in order, sent to app.

        // start_index=7.
        { 7, kPayloadComplete }, // 7=in order, sent to app.

        // start_index=8. End of test. The only payloads left in state array should be 2 and 5 (ignore).
    };

    int app_payload_ok_count = 0;
    int app_payload_error_count = 0;

    for (int i = 0; kCdiStatusOk  == rs && i < CDI_ARRAY_ELEMENT_COUNT(state_array); i++) {
        CDI_LOG_THREAD(kLogInfo, "Testing payload_num[%d] State[%d] Buffered packets[%d].",
                       state_array[i].payload_num,
                       state_array[i].payload_state,
                       endpoint_ptr->rx_state.rxreorder_buffered_packet_count);
        int payload_num = state_array[i].payload_num;

        RxPayloadState* payload_state_ptr = RxReorderPayloadStateGet(endpoint_ptr,
                                                                     con_state_ptr->rx_state.rx_payload_state_pool_handle,
                                                                     payload_num);
        assert(payload_state_ptr->payload_num == payload_num);

        // Simulate payload state and packet count;
        payload_state_ptr->payload_state = state_array[i].payload_state;
        payload_state_ptr->packet_count = PAYLOAD_PACKET_COUNT;
        payload_state_ptr->work_request_state.max_latency_microsecs = PAYLOAD_LATENCY_MAX;
        payload_state_ptr->work_request_state.start_time = CdiOsGetMicroseconds();

        // Simulate an error condition.
        int saved_window_count = endpoint_ptr->rx_state.rxreorder_buffered_packet_count;
        if (2 == i) {
            CDI_LOG_THREAD(kLogInfo, "Forcing rxreorder_buffered_packet_count=[%d]. NOTE: Should generate an SDK error.",
                           CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW);
            endpoint_ptr->rx_state.rxreorder_buffered_packet_count = CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW;
        }

        endpoint_ptr->rx_state.rxreorder_buffered_packet_count += PAYLOAD_PACKET_COUNT;
        endpoint_ptr->rx_state.total_packet_count += PAYLOAD_PACKET_COUNT;
        RxReorderPayloadSendReadyPayloads(endpoint_ptr);
        if (2 == i) {
            endpoint_ptr->rx_state.rxreorder_buffered_packet_count = saved_window_count -
                (CDI_MAX_RX_PACKET_OUT_OF_ORDER_WINDOW - endpoint_ptr->rx_state.rxreorder_buffered_packet_count);
        }

        // Simulate processing application payload messages.
        AppPayloadCallbackData app_cb_data;
        while (CdiQueuePop(con_state_ptr->app_payload_message_queue_handle, (void**)&app_cb_data)) {
            (kCdiStatusOk == app_cb_data.payload_status_code) ? app_payload_ok_count++ : app_payload_error_count++;

            CDI_LOG_THREAD(kLogInfo, "App payload[%d] status[%s]. Counts: Ok[%d], Err[%d].",
                           app_payload_ok_count + app_payload_error_count,
                           CdiCoreStatusToString(app_cb_data.payload_status_code),
                           app_payload_ok_count, app_payload_error_count);
            PayloadErrorFreeBuffer(con_state_ptr->error_message_pool, &app_cb_data);
        }
    }

    // End of test. Now validate the results.
    if (EXPECTED_REORDER_INDEX != endpoint_ptr->rx_state.rxreorder_current_index) {
        CDI_LOG_THREAD(kLogError, "Wrong expected rxreorder_current_index. [%d]!=[%d].",
                       EXPECTED_REORDER_INDEX, endpoint_ptr->rx_state.rxreorder_current_index);
        rs = kCdiStatusFatal;
    }

    if ((EXPECTED_APP_PAYLOAD_SUCCESSES != app_payload_ok_count) ||
        (EXPECTED_APP_PAYLOAD_ERRORS != app_payload_error_count)) {
        CDI_LOG_THREAD(kLogError, "Wrong number of app payloads processed. Success[%d]!=[%d]. Error[%d]!=[%d]",
                       EXPECTED_APP_PAYLOAD_SUCCESSES, app_payload_ok_count,
                       EXPECTED_APP_PAYLOAD_ERRORS, app_payload_error_count);
        rs = kCdiStatusFatal;
    }

    int payload_ignore_count = 0;
    RxPayloadState* payload_state_ptr = NULL;
    while (CdiPoolPeekInUse(con_state_ptr->rx_state.rx_payload_state_pool_handle, (void**)&payload_state_ptr)) {
        if (kPayloadIgnore != payload_state_ptr->payload_state) {
            CDI_LOG_THREAD(kLogError, "Pool should only contain ignore state[%d]. Found state[%d].",
                           kPayloadIgnore, payload_state_ptr->payload_state);
            rs = kCdiStatusFatal;
        } else {
            payload_ignore_count++;
        }

        // Get masked version of payload index.
        int current_payload_index = payload_state_ptr->payload_num & (CDI_MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER-1);
        endpoint_ptr->rx_state.payload_state_array_ptr[current_payload_index] = NULL;
        CdiPoolPut(con_state_ptr->rx_state.rx_payload_state_pool_handle, payload_state_ptr);
    }

    if (EXPECTED_IGNORE_PAYLOADS != payload_ignore_count) {
        CDI_LOG_THREAD(kLogError, "Wrong expected number of ignore payloads in state array. [%d]!=[%d].",
                       EXPECTED_IGNORE_PAYLOADS, payload_ignore_count);
        rs = kCdiStatusFatal;
    }

    // Should not find any entries in the payload state array.
    for (int i = 0; i < CDI_ARRAY_ELEMENT_COUNT(endpoint_ptr->rx_state.payload_state_array_ptr); i++) {
        RxPayloadState* payload_state_ptr = endpoint_ptr->rx_state.payload_state_array_ptr[i];
        if (payload_state_ptr) {
            CDI_LOG_THREAD(kLogError, "Payload state array is not empty at index[%d].", i);
            rs = kCdiStatusFatal;
        }
    }

    CdiPoolDestroy(con_state_ptr->rx_state.rx_payload_state_pool_handle);
    ProtocolVersionDestroy(endpoint_ptr->adapter_endpoint_ptr->protocol_handle);
    CdiPoolDestroy(con_state_ptr->error_message_pool);
    CdiQueueDestroy(con_state_ptr->app_payload_message_queue_handle);
    StatsDestroy(con_state_ptr->stats_state_ptr);
    CdiOsSignalDelete(con_state_ptr->shutdown_signal);

    return rs;
}
