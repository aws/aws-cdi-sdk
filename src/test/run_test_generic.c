// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the RunTestGeneric() function which is the main test execution function that uses the SDK's API to
 * initialize the adapter, create connections, and send and receive payloads.
 *
 * This function takes in the TestSettings array, and for each TestSetting instance, we create a TestConnectionInfo
 * instance which contains a pointer to the TestSettings instance associated with that connection.
 *
 * Then we register the SDK adapter, launch all Rx connections, and then launch up all Tx connections. Once all
 * connection threads have been launched, we wait until all of the threads complete and then check for any failures.
 *
 * See @ref cdi_test_all for diagrams and detailed description of this test function.
 */

#include "run_test.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_raw_api.h"
#include "cdi_test.h"
#include "test_args.h"
#include "test_console.h"
#include "test_dynamic.h"
#include "test_receiver.h"
#include "test_transmitter.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Structure used to pass arguments into InitPoolSgl().
typedef struct {
    uint8_t** tx_buffer_ptr;   ///< The address of the transmit buffer pointer; its value is assigned to the SGL's
                               ///  address pointer and updated to account for the SGL's size.
    int payload_buffer_size;   ///< The number of bytes in this SGL to be divided among the SGL entries.
    CdiBufferType buffer_type; ///< The type of buffer configured; if linear, only a sigle SGL entry is created while
                               ///  the SGL type creates several.
} PoolInitArgs;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create resources that are common to both Tx and Rx connections.
 *
 * @param connection_info_ptr Pointer to test connection structure.
 *
 * @return bool Returns true if successfully created, otherwise false is returned.
 */
static bool CreateCommonResources(TestConnectionInfo* connection_info_ptr)
{
    bool ret = true;

    // Create a critical section used to protect access to connection_handle.
    ret = CdiOsCritSectionCreate(&connection_info_ptr->connection_handle_lock);

    // Create signal to detect connection state changes.
    if (ret) {
        ret = CdiOsSignalCreate(&connection_info_ptr->connection_state_change_signal);
    }

    // Create signal for shutting down the thread and aborting FIFO waits. Must do this here, since we need
    // it in order to shutdown the thread.
    if (ret) {
        ret = CdiOsSignalCreate(&connection_info_ptr->connection_shutdown_signal);
    }

    // Create a signal to indicate when we have transmitted or received a single payload.
    if (ret) {
        ret = CdiOsSignalCreate(&connection_info_ptr->payload_done_signal);
    }

    // Create a signal to indicate when we processed all payloads.
    if (ret) {
        ret = CdiOsSignalCreate(&connection_info_ptr->done_signal);
    }

    if (ret) {
        ret = TestDynamicCreate(connection_info_ptr, &connection_info_ptr->test_dynamic_handle);
    }

    return ret;
}

/**
 * Destroy resources that are common to both Tx and Rx connections.
 *
 * @param connection_info_ptr Pointer to test connection structure.
 *
 */
static void DestroyCommonResources(TestConnectionInfo* connection_info_ptr)
{
    TestDynamicDestroy(connection_info_ptr->test_dynamic_handle);
    CdiOsSignalDelete(connection_info_ptr->done_signal);
    CdiOsSignalDelete(connection_info_ptr->payload_done_signal);
    CdiOsSignalDelete(connection_info_ptr->connection_shutdown_signal);
    CdiOsSignalDelete(connection_info_ptr->connection_state_change_signal);
    CdiOsCritSectionDelete(connection_info_ptr->connection_handle_lock);
}

/**
 * Wait for test to complete and provides stats update on the console while tests are running.
 *
 * @param connection_info_array Pointer to array of all the test connection structures.
 * @param num_connections Number of connections.
 */
static void WaitForTestToComplete(TestConnectionInfo* connection_info_array, int num_connections)
{
    uint64_t start_time = CdiOsGetMicroseconds();

    // Create an array of all the connection done signals so we can get a signal when they are all done.
    CdiSignalType signal_array[MAX_SIMULTANEOUS_CONNECTIONS];
    for (int i = 0; i < num_connections; i++) {
        signal_array[i] = connection_info_array[i].done_signal;
    }

    int time_pos_x = 16; // Set starting X-position of elapsed time digits "00:00:00" as used in first line below:
    const char* line1_str = "| Elapsed Time: 00:00:00  |                         Payload Latency (us)                  |      | Connection | Control |";
    const char* line2_str = "|      Payload Counts     |    Overall    |                 Most Recent Series            |      |            | Command |";
    const char* line3_str = "| Success | Errors | Late |  Min  |  Max  |  Min  |  P50  |  P90  |  P99  |  Max  | Count | CPU%% | Drop Count | Retries |";
    //       Example values: |00000000 |0000000 |00000 |000000 |000000 |000000 |000000 |000000 |000000 |000000 | 0000  |    0000    |  0000   |

    TestConsoleStats(0, 0, A_REVERSE, line1_str);
    TestConsoleStats(0, 1, A_REVERSE, line2_str);
    TestConsoleStats(0, 2, A_REVERSE, line3_str);

    // Ensure the define matches what we actually use here.
    bool ret = true;
    if (4 != STATS_WINDOW_STATIC_HEIGHT) {
        ret = false;
    }

    if (ret) {
        TestConsoleStatsHorzLine(0, STATS_WINDOW_STATIC_HEIGHT-1+num_connections, 0);

        // Convert how often to update the message from seconds to milliseconds.
        uint32_t timeout_ms = (uint32_t)(REFRESH_STATS_PERIOD_SECONDS * 1000.0);
        bool all_done = false;
        bool first_time = true;
        while (!all_done) {
            if (!first_time) {
                // Wait for all the done signals or the timeout.
                uint32_t signal_index;
                CdiOsSignalsWait(signal_array, num_connections, true, timeout_ms, &signal_index);
                if (CDI_OS_SIG_TIMEOUT != signal_index) {
                    // All the done signals are set, so we can exit the loop after displaying the stats message.
                    all_done = true;
                }
            } else {
                first_time = false;
            }

            if (GetGlobalTestSettings()->use_multiwindow_console) {
                // Calculate elapsed time in seconds.
                uint64_t elapsed_seconds = (CdiOsGetMicroseconds() - start_time) / (1000*1000);
                int hours = elapsed_seconds / 3600;
                int secs_remaining = elapsed_seconds % 3600;
                int minutes = secs_remaining / 60;
                int seconds =secs_remaining % 60;
                TestConsoleStats(time_pos_x, 0, A_REVERSE, "%02d:%02d:%02d", hours, minutes, seconds);
            }
            // NOTE: Connection statistics are updated in the user-registered callback function TestStatisticsCallback().

            TestConsoleStatsRefresh(); // Refresh the console (make the changes visible).
        }
    }

    // Now, wait for all the connection threads to finish and clean up resources each was using.
    for (int connection_index = 0; connection_index < num_connections; connection_index++) {
        TestConnectionInfo* connection_info_ptr = &connection_info_array[connection_index];
        if (connection_info_ptr->thread_id) {
            CdiOsThreadJoin(connection_info_ptr->thread_id, CDI_INFINITE, NULL);
            connection_info_ptr->thread_id = NULL;
        }
        DestroyCommonResources(connection_info_ptr);
    }
}

/**
 * Pool operator function that gets called once for each item during the creation of the transmit buffer SGL pool.
 *
 * @param context_ptr pointer to the structure containing all of the values passed into CdiPoolCreateAndInitItems() for
 *                    a given connection's transmit buffer pool.
 * @param item_ptr the address of the particular CdiSgList being added to the pool in need of initialization.
 *
 * @return true if the SGL item was initialized or false if a problem was encountered while attempting initialization.
 */
static bool InitPoolSgl(const void* context_ptr, void* item_ptr)
{
    bool ret = true;

    const PoolInitArgs* args_ptr = (PoolInitArgs*)context_ptr;
    CdiSgList* sgl_ptr = (CdiSgList*)item_ptr;

    // How large each of the entries will be in 8 byte words. 0 indicates the end of the list or "the rest." These
    // values were chosen to try to exercise the SGL handling and the packetizer. Payloads larger than 64848 bytes will
    // require all five entries, otherwise fewer will be used.
    const int entry_sizes[] = { 255, 1, 256, 9, 0 };

    // Use multiple entries if SGL selected, otherwise use a single entry to represent the whole buffer.
    const int max_entry_count = (kCdiSgl == args_ptr->buffer_type) ? sizeof(entry_sizes) / sizeof(entry_sizes[0]) : 1;

    // Create temporary entries, filling from 0 until running out of entries or buffer needing entries.
    CdiSglEntry tmp_entries[sizeof(entry_sizes) / sizeof(entry_sizes[0])] = { 0 };
    int entry_count = 0;
    int buffer_remaining = args_ptr->payload_buffer_size;
    while (buffer_remaining > 0) {
        const int max_entry_size = entry_sizes[entry_count] * sizeof(uint64_t);
        const int entry_size =
            (entry_count + 1 >= max_entry_count || 0 == entry_sizes[entry_count] || max_entry_size > buffer_remaining)
            ? buffer_remaining : max_entry_size;
        tmp_entries[entry_count].address_ptr = *args_ptr->tx_buffer_ptr;
        tmp_entries[entry_count].size_in_bytes = entry_size;
        *args_ptr->tx_buffer_ptr += entry_size;
        buffer_remaining -= entry_size;
        entry_count++;
    }

    ret = (0 == buffer_remaining && 0 < entry_count && entry_count <= max_entry_count);

    // Allocate memory for real SGL entries.
    const size_t s1 = sizeof(CdiSglEntry);
    const size_t s2 = s1 * entry_count;
    CdiSglEntry* sgl_entry_ptr = (CdiSglEntry*)CdiOsMemAllocZero(s2);
    if (sgl_entry_ptr && ret) {
        // Shuffle the entries. The final one is the same in source and destination, the others are reversed.
        for (int i = 0 ; i < entry_count - 1 ; i++) {
            sgl_entry_ptr[i] = tmp_entries[entry_count - i - 2]; // Copy address and size.
            sgl_entry_ptr[i].next_ptr = &sgl_entry_ptr[i + 1];
        }

        // Copy the final (or only) entry.
        sgl_entry_ptr[entry_count - 1] = tmp_entries[entry_count - 1];
        sgl_entry_ptr[entry_count - 1].next_ptr = NULL;  // Terminate the list.

        // Set list to reference allocated entries.
        sgl_ptr->sgl_head_ptr = &sgl_entry_ptr[0];
        sgl_ptr->sgl_tail_ptr = &sgl_entry_ptr[entry_count - 1];
        sgl_ptr->total_data_size = args_ptr->payload_buffer_size;
    } else {
        ret = false;
    }

    return ret;
}

/**
 * Pool operator function that gets called once for each item prior to the destruction of the transmit buffer SGL pool.
 * This function must free any resources that were allocated in InitPoolSgl().
 *
 * @param context_ptr pointer to the structure containing all of the values passed into CdiPoolForEach() for
 *                    a given connection's transmit buffer pool.
 * @param item_ptr the address of the particular CdiSgList need of cleanup.
 *
 * @return true always.
 */
static bool DestroyPoolSgl(const void* context_ptr, void* item_ptr)
{
    (void)context_ptr;
    CdiSgList* sgl_ptr = (CdiSgList*)item_ptr;

    // Free the CdiSglEntry array allocated in InitPoolSgl().
    CdiOsMemFree(sgl_ptr->sgl_head_ptr);

    return true;
}

/**
 * Creates all of the pools for tracking the connection's transmit buffers. Each stream in the connection has its own
 * pool. The buffer passed through connection_info_ptr is assumed to have been sized correctly to account for all
 * streams of all connections before the first time this function is called.
 *
 * @param connection_info_ptr pointer to the structure containing all of the specifics of the connection for which to
 *                            create the transmit buffer SGL pools.
 * @param tx_buffer_ptr pointer to the pointer which tracks the assignment of each stream's buffers.
 *
 * @return true if the pool creations succeeded, false if not.
 */
static bool CreateTxBufferPools(TestConnectionInfo* connection_info_ptr, uint8_t** tx_buffer_ptr)
{
    bool ret = true;

    // Create a payload memory pool for each stream. Will allocate enough pool items to allow for 1 + the maximum number
    // of simultaneous connections. Each item in the pool is an SGList with one or more entries, depending on the buffer
    // type setting for the stream.
    const int num_streams = connection_info_ptr->test_settings_ptr->number_of_streams;
    int pool_size = connection_info_ptr->config_data.tx.max_simultaneous_tx_payloads;
    if (0 == pool_size) {
        pool_size = MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION;
    }
    pool_size++; // Make the pool_size larger the maximum number of simultaneous payloads.

    for (int j = 0; j < num_streams && ret; j++) {
        TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[j];
        char pool_name_str[MAX_POOL_NAME_LENGTH];
        snprintf(pool_name_str, sizeof(pool_name_str), "TxBuffer Con[%d] Stream[%d]", connection_info_ptr->my_index, j);

        // Create pool of SGLs pointing to the existing buffer reserved by the adapter. See
        // CdiCoreNetworkAdapterInitialize().
        PoolInitArgs init_args = {
            .tx_buffer_ptr = tx_buffer_ptr,
            // Use the value of the max payload size and not the size of the buffer which is rounded up to the nearest
            // 64-bit word. The next_payload_size is always set to the user specified maximum payload size when
            // CreateTxBufferPools() is called.
            .payload_buffer_size = stream_info_ptr->next_payload_size,
            .buffer_type = connection_info_ptr->test_settings_ptr->buffer_type
        };
        ret = CdiPoolCreateAndInitItems(pool_name_str, // Name of the pool.
                                        pool_size, // Number of pool items.
                                        0, // Grow count.
                                        0, // Max grow count.
                                        sizeof(CdiSgList),
                                        true, // true= Make thread-safe.,
                                        &stream_info_ptr->tx_pool_handle, // Returned handle to the pool.
                                        InitPoolSgl,
                                        &init_args);
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool RunTestGeneric(TestSettings* test_settings_ptr, int max_test_settings_entries, int num_connections)
{
    CdiAdapterHandle adapter_handle = NULL;
    bool got_error = false;

    // Create a data structure for all connection info that we can assign the test settings to.
    TestConnectionInfo connection_info_array[MAX_SIMULTANEOUS_CONNECTIONS] = {{ 0 }};

    // Make sure we cannot overrun the test_settings and connection_info_array arrays.
    if (num_connections > max_test_settings_entries) {
        CDI_LOG_THREAD(kLogError, "Number of connections [%d] has exceeded the maximum allowed connections [%d].",
                       num_connections, max_test_settings_entries);
        got_error = true;
    }

    if (!got_error) {
        // For each requested connection, create a data structure and initialize it.
        for (int i = 0; i < num_connections; i++) {
            // Create connection data structure, add its pointer to the connections array, and initialize its elements.
            connection_info_array[i].my_index = i;
            connection_info_array[i].pass_status = true;
            // Set the config_payload_skip_count to the user's input skip number so we will send config data on the
            // first payload.
            int num_streams = test_settings_ptr->number_of_streams;
            for (int j = 0; j < num_streams; j++) {
                connection_info_array[i].stream_info[j].config_payload_skip_count =
                                                        test_settings_ptr->stream_settings[j].config_skip;
            }
            // Copy test settings into connection_info_array.
            connection_info_array[i].test_settings_ptr = &test_settings_ptr[i];
            // The maximum expected payload count after all transactions are sent is the number of transactions across
            // all streams.
            connection_info_array[i].total_payloads = connection_info_array[i].test_settings_ptr->number_of_streams *
                                                        connection_info_array[i].test_settings_ptr->num_transactions;
        }
    }

    int total_tx_payload_bytes = 0;
    // Calculate the total amount of tx and rx buffer to reserve for the adapter. Do this by cycling through all
    // connections and summing rx and tx payload sizes.
    if (!got_error) {
        bool have_tx = false;
        for (int i = 0; i < num_connections && !got_error; i++) {
            int num_streams = connection_info_array[i].test_settings_ptr->number_of_streams;
            for (int j = 0; j < num_streams && !got_error; j++) {
                TestConnectionStreamInfo* stream_info_ptr = &connection_info_array[i].stream_info[j];
                StreamSettings* stream_settings_ptr = &connection_info_array[i].test_settings_ptr->stream_settings[j];

                // Round up to the nearest pattern.
                stream_info_ptr->payload_buffer_size = IntDivCeil(stream_settings_ptr->payload_size,
                                                                    BYTES_PER_PATTERN_WORD) * BYTES_PER_PATTERN_WORD;

                // All streams within a connection have the same direction (Rx/Tx).
                if (connection_info_array[i].test_settings_ptr->tx) {
                    // Calculate the required size of each buffer and keep a running total of memory required so it can
                    // later be allocated using the adapter. Round buffer size up so each can be 8 byte aligned. See
                    // CdiCoreNetworkAdapterInitialize().

                    int payload_pool_size = connection_info_array[i].config_data.tx.max_simultaneous_tx_payloads;
                    if (0 == payload_pool_size) {
                        payload_pool_size = MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION;
                    }
                    payload_pool_size++; // Make payload pool one larger than maximum simultaneous payloads.

                    stream_info_ptr->tx_pool_buffer_size = payload_pool_size *
                        IntDivCeil(stream_info_ptr->payload_buffer_size, sizeof(uint64_t)) * sizeof(uint64_t);
                    total_tx_payload_bytes += stream_info_ptr->tx_pool_buffer_size;
                    have_tx = true;

                    // Check if Transmitter has an inconsistent riff file configuration. This is checked here because
                    // there is no enforced ordering between the --riff and --file_read options. This is only checked
                    // for transmit because a receiver may use --riff without any corresponding file_read or file_write
                    // to bypass payload size checking because the transmitter is sending variably sized payloads.
                    if (stream_settings_ptr->riff_file && (NULL == stream_settings_ptr->file_read_str)) {
                        CDI_LOG_THREAD(kLogError, "The --riff option is set for transmit connection [%d] stream [%d] "
                        "but a corresponding payload file was not provided using the --file_read option.", i, j);
                        got_error = true;
                    }
                }

                // Initialize next_payload_size with the stream_settings.payload_size. This won't change if a RIFF
                // file is not being used for payload data.
                connection_info_array[i].stream_info[j].next_payload_size =
                                        connection_info_array[i].test_settings_ptr->stream_settings[j].payload_size;

                // Default this setting to the video sample rate of 90kHz if it isn't set by test_args.
                if (connection_info_array[i].test_settings_ptr->stream_settings[j].rtp_sample_rate == 0) {
                    connection_info_array[i].test_settings_ptr->stream_settings[j].rtp_sample_rate =
                                                                                              PCR_VIDEO_SAMPLE_RATE;
                }
            }
        }

        if (total_tx_payload_bytes == 0 && have_tx) {
            got_error = true;
        }
    }

    CdiAdapterData* adapter_data_ptr = &GetGlobalTestSettings()->adapter_data;

    // Register the adapter.
    if (!got_error) {
        CDI_LOG_THREAD(kLogInfo, "Registering an adapter.");
        adapter_data_ptr->tx_buffer_size_bytes = total_tx_payload_bytes;
        got_error = (kCdiStatusOk != CdiCoreNetworkAdapterInitialize(adapter_data_ptr, &adapter_handle));
    }

    // If the we are still happy at this point, then start up all the connections and run the tests.
    if (!got_error) {
        // Get pointer to Tx buffer allocated by adapter. Will use to allocate Tx payload memory pools for all
        // connections and streams within them.
        uint8_t* tx_buffer_ptr = adapter_data_ptr->ret_tx_buffer_ptr;

        // Start all of the test threads. Each creates an underlying connection, running to completion of the test.
        for (int connection_index = 0; connection_index < num_connections && !got_error; connection_index++) {
            TestConnectionInfo* connection_info_ptr = &connection_info_array[connection_index];

            // Create resources that are common to Tx and Rx.
            got_error = !CreateCommonResources(connection_info_ptr);
            if (!got_error) {
                if (connection_info_ptr->test_settings_ptr->rx) {
                    // Add the adapter handle to this connection's Rx config data.
                    connection_info_ptr->config_data.rx.adapter_handle = adapter_handle;

                    // Start the thread that creates the Rx connection and checks received payloads.
                    if (!got_error) {
                        got_error = !CdiOsThreadCreate(TestRxCreateThread, &connection_info_ptr->thread_id, "TestRx",
                                                       connection_info_ptr, NULL);
                        if (got_error) {
                            CDI_LOG_THREAD(kLogError, "Failed to create test receiver thread");
                        }
                    }
                } else {
                    // Add the adapter handle to this connection's Tx config data.
                    connection_info_ptr->config_data.tx.adapter_handle = adapter_handle;

                    // Create pools of buffers, allocates one more than the maximum number of simultaneous payloads for
                    // each stream in this connection.
                    got_error = !CreateTxBufferPools(connection_info_ptr, &tx_buffer_ptr);

                    // Start the thread that creates the Tx connection and associated resources and sends requested
                    // payloads.
                    if (!got_error) {
                        got_error = !CdiOsThreadCreate(TestTxCreateThread, &connection_info_ptr->thread_id, "TestTx",
                                                    connection_info_ptr, NULL);
                        if (got_error) {
                            CDI_LOG_THREAD(kLogError, "Failed to create test transmitter thread");
                        }
                    }
                }
            }
        }
    }

    if (!got_error) {
        // Wait for connections to all say they are done. For a transmitter, done means either the transmitter has sent
        // all payloads and received delivery acknowledgements for each one it expects, or it has timed out waiting for
        // them. For a receiver, done means the receiver has either received all the payloads it expects, or it has
        // timed out waiting for them.
        WaitForTestToComplete(connection_info_array, num_connections);

        // Roll up the pass/fail status words into one status and free Tx buffer pools.
        for (int connection_index = 0; connection_index < num_connections; connection_index++) {
            TestConnectionInfo* connection_info_ptr = &connection_info_array[connection_index];
            got_error = got_error || !connection_info_ptr->pass_status;
            int num_streams = connection_info_ptr->test_settings_ptr->number_of_streams;
            for (int j = 0; j < num_streams; j++) {
                TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[j];
                if (stream_info_ptr->tx_pool_handle) {
                    CdiPoolPutAll(stream_info_ptr->tx_pool_handle); // Ensure all pool items are not being used.
                    CdiPoolForEachItem(stream_info_ptr->tx_pool_handle, DestroyPoolSgl, NULL);
                    CdiPoolDestroy(stream_info_ptr->tx_pool_handle);
                    stream_info_ptr->tx_pool_handle = NULL;
                }
            }
        }
    }

    if (adapter_handle) {
        if (kCdiStatusOk != CdiCoreNetworkAdapterDestroy(adapter_handle)) {
            CDI_LOG_THREAD(kLogError, "Failed to destroy network adapter.");
            got_error = true;
        }
        adapter_handle = NULL;
    }

    // Return the correct return status to the test app.
    // We fail if we got any errors from the SDK or if our test logic didn't pass.
    return !got_error;
}
