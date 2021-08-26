// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains common cdi_test definitions and functions used by both receiver connections and transmitter
* connections.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "test_control.h"
#include "curses.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "cdi_logger_api.h"
#include "cdi_test.h"
#include "curses.h"
#include "run_test.h"
#include "test_console.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Number of attoseconds in a nanosecond.
#define ATTOSECONDS_TO_NANOSECONDS (1000000000UL)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initializes a buffer the size of a single payload to use when using test patterns. This buffer is used to efficiently
 * send the payload by the test_transmitter and is used for comparing the receive data buffer in test_receiver.
 *
 * @param   seed_value            The seed_value to start patterns with.
 * @param   pattern_type          The enum (TestPatternTypes) value for the pattern type.
 * @param   payload_word_size     Sets how many 64 bit words fit in the buffer pointed to by pattern_buffer_ptr.
 * @param   pattern_buffer_ptr    The pointer to a buffer of payload_word_size that will be filled with
 *                                  payload_word_size number of words of pattern pattern_type.
 *
 * @return                        If successful returns true, otherwise returns false.
 */
static bool TestPayloadPatternSet(uint64_t seed_value, TestPatternType pattern_type, int payload_word_size,
                                  void* pattern_buffer_ptr)
{
    bool ret = payload_word_size > 0;
    int  payload_word_remaining = payload_word_size;

    // As long as we have 1 or more words to add to the buffer, proceed.
    if (ret) {
        uint64_t* current_buffer_position_ptr = (uint64_t*)pattern_buffer_ptr;

        // Reserve one word at the start of the payload buffer to act as as a payload identifier.
        // This will be modified after every payload on both the Tx and Rx side to add some uniqueness to payloads.
        *current_buffer_position_ptr = 0;
        current_buffer_position_ptr++;
        payload_word_remaining--;

        // Store the seed word in the first location.
        if (payload_word_remaining > 0) {
            *current_buffer_position_ptr = seed_value;
            current_buffer_position_ptr++;
            payload_word_remaining--;
        }

        uint64_t current_word = seed_value;

        // Switch here and loop inside the switch so the pattern_type only needs to be evaluated once.
        // Based on the pattern selected by the user, we implement the chosen algorithm.
        switch (pattern_type) {
            case kTestPatternSame: // Pattern never changes from the seed value.
                for (int i=0; i<payload_word_remaining; i++) {
                    current_buffer_position_ptr[i] = seed_value;
                }
                break;
            case kTestPatternInc: // Pattern increments every word.
                for (int i=0; i<payload_word_remaining; i++) {
                    current_word++;
                    current_buffer_position_ptr[i] = current_word;
                }
                break;
            case kTestPatternSHL: // Pattern shifts one bit to the left every word.
                for (int i=0; i<payload_word_remaining; i++) {
                    current_word = (current_word>>63) | (current_word<<1);
                    current_buffer_position_ptr[i] = current_word;
                }
                break;
            case kTestPatternSHR: // Pattern shifts one bit to the right every word.
                for (int i=0; i<payload_word_remaining; i++) {
                    current_word = (current_word<<63) | (current_word>>1);
                    current_buffer_position_ptr[i] = current_word;
                }
                break;
            case kTestPatternNone:
            case kTestPatternIgnore:
                break; // Nothing special do to here.
            default:
                CDI_LOG_THREAD(kLogError, "Test pattern is not defined and cannot be set.");
                ret = false;
                break;
        }
    }

    return ret;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

int IntDivCeil(int numerator, int denominator) {
    return (numerator - 1) / denominator + 1;
}

bool TestWaitForConnection(TestConnectionInfo* connection_info_ptr, int timeout_seconds)
{
    bool ret = true;
    CdiSignalType signal_array[2];
    signal_array[0] = connection_info_ptr->connection_state_change_signal;
    signal_array[1] = connection_info_ptr->connection_shutdown_signal;

    TEST_LOG_CONNECTION(kLogInfo, "Waiting up to [%d]seconds to establish a connection...", timeout_seconds);

    uint64_t start_ms = CdiOsGetMilliseconds();
    uint32_t timeout_ms = timeout_seconds*1000; // Convert to seconds.
    uint32_t time_to_wait_ms = timeout_ms;

    // Wait until we are connected.
    while (kCdiConnectionStatusConnected != connection_info_ptr->connection_status) {
        uint32_t signal_index;
        CdiOsSignalsWait(signal_array, 2, false, time_to_wait_ms, &signal_index);
        CdiOsSignalClear(connection_info_ptr->connection_state_change_signal);
        if (0 != signal_index) {
            // Wait was aborted (signal_index=1) or timed-out (signal_index=CDI_OS_SIG_TIMEOUT).
            ret = false;
            break;
        }

        // Get the total time that has expired since we entered this function and see if we have exceeded the timeout.
        uint64_t expired_ms = CdiOsGetMilliseconds() - start_ms;
        if (expired_ms >= timeout_ms) {
            // Yes, got timeout.
            ret = false;
            break;
        }
        // Have not exceeded timeout, so setup the remaining time to wait and go wait again.
        time_to_wait_ms = timeout_ms - expired_ms;
    }

    const char* connection_name_str = connection_info_ptr->test_settings_ptr->tx ?
                                      connection_info_ptr->config_data.tx.connection_name_str :
                                      connection_info_ptr->config_data.rx.connection_name_str;
    if (ret) {
        if (GetGlobalTestSettings()->all_connected_signal) {
            int num_connections = CdiOsAtomicInc32(&GetGlobalTestSettings()->num_connections_established);
            if (num_connections >= GetGlobalTestSettings()->total_num_connections) {
                // All connections have been established, so ok to continue on with test.
                TEST_LOG_CONNECTION(kLogInfo, "Final connection[%s] established. Starting transfer...",
                                    connection_name_str, GetGlobalTestSettings()->total_num_connections);
                CdiOsSignalSet(GetGlobalTestSettings()->all_connected_signal);
            } else {
                // Wait for all connections to be established.
                TEST_LOG_CONNECTION(kLogInfo, "Connection[%s] established. Waiting for [%d] other connections.",
                                    connection_name_str,
                                    GetGlobalTestSettings()->total_num_connections - num_connections);
                signal_array[0] = GetGlobalTestSettings()->all_connected_signal;
                CdiOsSignalsWait(signal_array, 2, false, time_to_wait_ms, NULL);
            }
        } else {
            TEST_LOG_CONNECTION(kLogInfo, "Connection[%s] established.", connection_name_str);
        }
    } else {
        TEST_LOG_CONNECTION(kLogError, "Unable to establish connection[%s] within timeout period.", connection_name_str);
    }

    return ret;
}

void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->connection_user_cb_param;
    const char* connection_name_str = connection_info_ptr->test_settings_ptr->tx ?
                                      connection_info_ptr->config_data.tx.connection_name_str :
                                      connection_info_ptr->config_data.rx.connection_name_str;

    TEST_LOG_CONNECTION(kLogInfo, "Connection[%s] remote IP[%s:%d] status changed[%s]. Msg[%s].",
                        CdiGetEmptyStringIfNull(connection_name_str), cb_data_ptr->remote_ip_str,
                        cb_data_ptr->remote_dest_port,
                        CdiUtilityKeyEnumToString(kKeyConnectionStatus, cb_data_ptr->status_code),
                        CdiGetEmptyStringIfNull(cb_data_ptr->err_msg_str));

    CdiConnectionStatus status_code = cb_data_ptr->status_code;
    connection_info_ptr->connection_status = status_code;
    if (connection_info_ptr->test_settings_ptr->tx && connection_info_ptr->test_settings_ptr->multiple_endpoints) {
        // Tx connection supports multiple endpoints, so don't signal connected state until all of the endpoints are
        // connected.
        for (int i = 0; i < connection_info_ptr->test_settings_ptr->number_of_streams; i++) {
            if (connection_info_ptr->tx_stream_endpoint_handle_array[i] == cb_data_ptr->tx_stream_endpoint_handle) {
                connection_info_ptr->connection_status_stream_array[i] = cb_data_ptr->status_code;
            }
            if (kCdiConnectionStatusDisconnected == connection_info_ptr->connection_status_stream_array[i]) {
                connection_info_ptr->connection_status = kCdiConnectionStatusDisconnected;
            }
        }
    }

    CdiOsSignalSet(connection_info_ptr->connection_state_change_signal);
}

void TestStatisticsCallback(const CdiCoreStatsCbData* cb_data_ptr)
{
    TestConnectionInfo* connection_info_ptr = cb_data_ptr->stats_user_cb_param;

    connection_info_ptr->total_poll_thread_load = 0;
    for (int i = 0; i < cb_data_ptr->stats_count; i++) {
        CdiTransferStats* transfer_stats_ptr = &cb_data_ptr->transfer_stats_array[i];
        const CdiPayloadCounterStats* counter_stats_ptr = &transfer_stats_ptr->payload_counter_stats;
        const CdiPayloadTimeIntervalStats* interval_stats_ptr = &transfer_stats_ptr->payload_time_interval_stats;
        const CdiAdapterEndpointStats* endpoint_stats_ptr = &transfer_stats_ptr->endpoint_stats;
        int connection_num = connection_info_ptr->my_index;

        // Update overall stats.
        if (interval_stats_ptr->transfer_time_min < connection_info_ptr->transfer_time_min_overall ||
                0 == connection_info_ptr->transfer_time_min_overall) {
            connection_info_ptr->transfer_time_min_overall = interval_stats_ptr->transfer_time_min;
        }
        if (interval_stats_ptr->transfer_time_max > connection_info_ptr->transfer_time_max_overall) {
            connection_info_ptr->transfer_time_max_overall = interval_stats_ptr->transfer_time_max;
        }

        connection_info_ptr->total_poll_thread_load += endpoint_stats_ptr->poll_thread_load;
        int total_load = 0;
        for (int i = 0; i < GetGlobalTestSettings()->total_num_connections; i++) {
            total_load += GetGlobalTestSettings()->connection_info_array[i].total_poll_thread_load;
        }

        // NOTE: Could choose not to update stats if currently not connected (use
        // connection_info_ptr->connection_status).
        TestConsoleStats(0, connection_num+STATS_WINDOW_STATIC_HEIGHT-1, A_NORMAL,
                        "|%8u |%7u |%5u |%6lu |%6lu |%6lu |%6lu |%6lu |%6lu |%6lu |%6u | %3u(%2u) | %4u  |  %4u   |",
                        counter_stats_ptr->num_payloads_transferred,
                        counter_stats_ptr->num_payloads_dropped,
                        counter_stats_ptr->num_payloads_late,
                        connection_info_ptr->transfer_time_min_overall,
                        connection_info_ptr->transfer_time_max_overall,
                        interval_stats_ptr->transfer_time_min,
                        interval_stats_ptr->transfer_time_P50,
                        interval_stats_ptr->transfer_time_P90,
                        interval_stats_ptr->transfer_time_P99,
                        interval_stats_ptr->transfer_time_max,
                        interval_stats_ptr->transfer_count,
                        endpoint_stats_ptr->poll_thread_load / 100, total_load / 100,
                        endpoint_stats_ptr->dropped_connection_count,
                        endpoint_stats_ptr->probe_command_retry_count);

        TestConsoleStatsRefresh(); // Refresh the console (make the changes visible).

        // Generate optional log message of stats.
        CDI_LOG_THREAD_COMPONENT(kLogInfo, kLogComponentPerformanceMetrics,
                        "Payloads %d-%d: Min[%lu]us P50[%lu]us P90[%lu] P99[%lu] Max[%lu]us. Overall: Min[%lu]us "
                        " Max[%lu]us. Late Payloads[%u].",
                        counter_stats_ptr->num_payloads_transferred - interval_stats_ptr->transfer_count,
                        counter_stats_ptr->num_payloads_transferred - 1,
                        interval_stats_ptr->transfer_time_min,
                        interval_stats_ptr->transfer_time_P50,
                        interval_stats_ptr->transfer_time_P90,
                        interval_stats_ptr->transfer_time_P99,
                        interval_stats_ptr->transfer_time_max,
                        connection_info_ptr->transfer_time_min_overall,
                        connection_info_ptr->transfer_time_max_overall,
                        counter_stats_ptr->num_payloads_late);

        // Save counter based stats so we can calculate deltas next time.
        connection_info_ptr->payload_counter_stats_array[i] = *counter_stats_ptr;
    }
    connection_info_ptr->number_stats = cb_data_ptr->stats_count;
}

void TestIncPayloadCount(TestConnectionInfo* connection_info_ptr, int stream_index) {
    // NOTE: Since the caller is CDI's thread, use TEST_LOG_CONNECTION() to log to the application's connection log.
    const char* connection_name_str = connection_info_ptr->test_settings_ptr->tx ?
                                      connection_info_ptr->config_data.tx.connection_name_str :
                                      connection_info_ptr->config_data.rx.connection_name_str;
    TestConnectionStreamInfo* stream_info_ptr = &connection_info_ptr->stream_info[stream_index];

    // Is this the last payload? Are we done? +1 because payload_count is zero-based.
    if (!IsPayloadNumLessThanTotal((connection_info_ptr->payload_count + 1), connection_info_ptr->total_payloads)) {
        TEST_LOG_CONNECTION(kLogInfo, "Last expected payload[%d] complete for connection[%s], marking connection done.",
                            connection_info_ptr->payload_count,
                            CdiGetEmptyStringIfNull(connection_name_str));
        CdiOsSignalSet(connection_info_ptr->done_signal);
    }

    // Increment the payload counters.
    CdiOsAtomicInc32(&connection_info_ptr->payload_count);
    CdiOsAtomicInc32(&stream_info_ptr->payload_count);
    CdiOsSignalSet(connection_info_ptr->payload_done_signal);
}

bool PreparePayloadData(StreamSettings* stream_settings_ptr, int payload_buffer_size, CdiFileID* read_file_handle_ptr,
                        void* buffer_ptr)
{
    bool return_val = true;

    // If the user has asked us to get our pattern from a file, then open the file the user gave us.
    if (stream_settings_ptr->file_read_str != NULL) {
        return_val = CdiOsOpenForRead(stream_settings_ptr->file_read_str, read_file_handle_ptr);
        if (!return_val) {
            CDI_LOG_THREAD(kLogError, "Error opening file [%s] for reading.", stream_settings_ptr->file_read_str);
        } else if (stream_settings_ptr->riff_file) {
            return_val = StartRiffPayloadFile(stream_settings_ptr, *read_file_handle_ptr);
        }
    // Otherwise, load the buffer with a pattern.
    } else {
        // Buffer has been rounded-up to the nearest 8 byte boundary to simplify pattern creation.
        if (0 != payload_buffer_size % BYTES_PER_PATTERN_WORD) {
            return_val = false;
        } else {
            int payload_words = payload_buffer_size / BYTES_PER_PATTERN_WORD;
            // Load the buffer with the algorithmic pattern selected by the user.
            return_val = TestPayloadPatternSet(stream_settings_ptr->pattern_start, stream_settings_ptr->pattern_type,
                                               payload_words, buffer_ptr);
        }
    }

    return return_val;
}

bool GetNextPayloadDataSgl(TestConnectionInfo* connection_info_ptr, int stream_id, int payload_id,
                           CdiFileID read_file_handle, CdiSgList* sgl_ptr)
{
    bool return_val = true;

    // We can only load the buffer if the pointer to it is not NULL.
    if (NULL != sgl_ptr && 0 < sgl_ptr->total_data_size) {
        // Receive logic increments payload_count before this function is called, so the last payload will have
        // payload_count == num_payloads, so no need to update.
        // On the transmit side, this will never happen because the payload_count is incremented after this function is
        // called.
        if (IsPayloadNumLessThanTotal(connection_info_ptr->payload_count,
                                      connection_info_ptr->total_payloads)) {
            // If we are using a file, then load the buffer from the file.
            if (NULL != read_file_handle) {
                // If we are using a file, then load the buffer from the file and read in payload_sized chunks.
                for (CdiSglEntry* entry_ptr = sgl_ptr->sgl_head_ptr ; entry_ptr != NULL ;
                        entry_ptr = entry_ptr->next_ptr) {
                    uint32_t bytes_read;
                    return_val = CdiOsRead(read_file_handle, entry_ptr->address_ptr, entry_ptr->size_in_bytes,
                                           &bytes_read);

                    // The payload was not read so go back to the top of the file.
                    if (return_val && (bytes_read == 0))  {
                        if (CdiOsFSeek(read_file_handle, 0, SEEK_SET)) {
                            return_val = CdiOsRead(read_file_handle, entry_ptr->address_ptr, entry_ptr->size_in_bytes,
                                                   &bytes_read);
                       }
                    }

                    // Make sure we got the whole payload from the file, and error if we didn't.
                    if (!return_val || (bytes_read != (uint32_t)entry_ptr->size_in_bytes)) {
                        TEST_LOG_CONNECTION(kLogError, "File must be an integer number of payloads in size. Read [%u] "
                                                       "payload bytes out of payload size [%d].",
                                            bytes_read, entry_ptr->size_in_bytes);
                        return_val = false;
                    }
                }
            } else {
                // Set the first 64-bit word of the buffer using stream index and stream payload count to to make this
                // payload unique.
                (*(uint64_t*)sgl_ptr->sgl_head_ptr->address_ptr) = ((uint64_t)(stream_id) << 56) | payload_id;
            }
        } else {
            TEST_LOG_CONNECTION(kLogInfo, "Loaded last payload already.");
        }
    } else {
        TEST_LOG_CONNECTION(kLogError, "buffer_ptr for next payload is NULL.");
        return_val = false;
    }

    return return_val;
}

bool GetNextPayloadDataLinear(TestConnectionInfo* connection_info_ptr, int stream_id, int payload_id,
                              CdiFileID read_file_handle, uint8_t* buffer_ptr, int buffer_size)
{
    // Create a trivial SGL representing the linear buffer for the call to GetNextPayloadData().
    CdiSglEntry entry = {
        .address_ptr = buffer_ptr,
        .size_in_bytes = buffer_size,
        .next_ptr = NULL
    };
    CdiSgList sgl = {
        .sgl_head_ptr = &entry,
        .sgl_tail_ptr = &entry,
        .total_data_size = buffer_size
    };
    return GetNextPayloadDataSgl(connection_info_ptr, stream_id, payload_id, read_file_handle, &sgl);
}

bool StartRiffPayloadFile(StreamSettings* stream_settings_ptr, CdiFileID read_file_handle)
{
    bool return_val = true;

    RiffFileHeader file_header;

    uint32_t bytes_read = 0;
    return_val = CdiOsRead(read_file_handle, &file_header, sizeof(RiffFileHeader), &bytes_read);

    if (!return_val || (bytes_read != sizeof(RiffFileHeader))) {
        CDI_LOG_THREAD(kLogError, "Failed to read RIFF file header from file [%s].",
                       stream_settings_ptr->file_read_str);
        return_val = false;
    }

    // Parse the header file.
    if (return_val) {
        // Check for "RIFF" four_cc marker.
        if (CdiOsStrNCmp(file_header.chunk_header.four_cc, "RIFF", 4) != 0) {
            CDI_LOG_THREAD(kLogError, "File is not a RIFF file [%s], the four_cc code received is not 'RIFF'.",
            stream_settings_ptr->file_read_str);
            return_val = false;
        }

        // The file_header.chunk_header.four_cc is not being verified.

        // Check for "CDI " Form Type.
        if (CdiOsStrNCmp(file_header.form_type, "CDI ", 4) != 0) {
            CDI_LOG_THREAD(kLogError, "RIFF file [%s]: Form Type received is not 'CDI '.",
                           stream_settings_ptr->file_read_str, file_header.form_type);
            return_val = false;
        }
    }

    return return_val;
}

bool GetNextRiffPayloadSize(TestConnectionInfo* connection_info_ptr, StreamSettings* stream_settings_ptr,
                            CdiFileID read_file_handle, int* ret_payload_size_ptr)
{
    bool return_val = true;

    RiffChunkHeader chunk_header; // Buffer for holding chunk headers four_cc code and chunk size.

    uint32_t bytes_read = 0;
    if (read_file_handle) {
        return_val = CdiOsRead(read_file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
    } else {
        TEST_LOG_CONNECTION(kLogError, "No file handle for RIFF File");
    }

    // Ran out of subchunk headers to read so retry at the top of the file.
    if (return_val && (bytes_read == 0)) {
        if (CdiOsFSeek(read_file_handle, 0, SEEK_SET)) {
            return_val = StartRiffPayloadFile(stream_settings_ptr, read_file_handle);
        }
        if (return_val) {
            return_val = CdiOsRead(read_file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
        }
    }

    if (!return_val || (bytes_read != sizeof(RiffChunkHeader))) {
        TEST_LOG_CONNECTION(kLogError, "Failed to read chunk header from file [%s]. Read [%d] header bytes.",
        stream_settings_ptr->file_read_str, bytes_read);
        return_val = false;
    }

    // For now check if the chunk ID is "ANC ". NOTE: this check may be removed or expanded in the future to support
    // additional chunk IDs.
    // payload types.
    if (CdiOsStrNCmp(chunk_header.four_cc, "ANC ", 4) != 0) {
        TEST_LOG_CONNECTION(kLogError, "RIFF File [%s] subchunk ID is not 'ANC '.", stream_settings_ptr->file_read_str);
        return_val =false;
    }

    if (return_val) {
        *ret_payload_size_ptr = chunk_header.size;
        // Payload size just be larger than the larger RIFF file payload in the source file.
        if (*ret_payload_size_ptr > stream_settings_ptr->payload_size) {
            TEST_LOG_CONNECTION(kLogError, "Payload size from RIFF file [%d] is larger than the payload buffer [%d].",
                                ret_payload_size_ptr, stream_settings_ptr->payload_size);
            return_val = false;
        }
    }

    return return_val;
}

bool TestCreateConnectionLogFiles(TestConnectionInfo* connection_info_ptr, CdiLogMethodData* log_method_data_ptr,
                                  char* sdk_log_filename_buffer_str)
{
    bool ret = true;
    CdiLogMethodData base_log_method_data = {0};

    // Get the name of this connection for later use. The saved_connection_name_str is not yet populated at this
    // point, the logs get created first. Therefore, we use the connection number as part of the log file name.
    const char* config_data_connection_name_str = connection_info_ptr->test_settings_ptr->tx ?
                                            connection_info_ptr->config_data.tx.connection_name_str :
                                            connection_info_ptr->config_data.rx.connection_name_str;
    char connection_name_str[CDI_MAX_CONNECTION_NAME_STRING_LENGTH] = {0};

    // If the user did not supply a connection name, create a connection name using the index number.
    if ((NULL == config_data_connection_name_str) || (0 == strlen(config_data_connection_name_str))) {
        snprintf(connection_name_str, CDI_MAX_CONNECTION_NAME_STRING_LENGTH, "%d", connection_info_ptr->my_index);
    } else {
        CdiOsStrCpy(connection_name_str, CDI_MAX_CONNECTION_NAME_STRING_LENGTH, config_data_connection_name_str);
    }

    // If no log filename, then skip all this.
    if (GetGlobalTestSettings()->base_log_filename_str[0]) {
        char filename_buffer_str[MAX_LOG_FILENAME_LENGTH];

        // File method.
        if (GetGlobalTestSettings()->use_single_connection_log_file) {
            // Single log file for the app, so just re-use the app log filename.
            CdiOsStrCpy(filename_buffer_str, MAX_LOG_FILENAME_LENGTH, GetGlobalTestSettings()->base_log_filename_str);
        } else {
            // Generate a unique log filename based on the connection information for this test application.
            if (MAX_LOG_FILENAME_LENGTH < snprintf(filename_buffer_str, MAX_LOG_FILENAME_LENGTH, "%s_%s.log",
                GetGlobalTestSettings()->base_log_filename_str, connection_name_str)) {
                filename_buffer_str[MAX_LOG_FILENAME_LENGTH-1] = '\0';
            }
        }
        base_log_method_data.log_filename_str = filename_buffer_str;
        base_log_method_data.log_method = kLogMethodFile;

        // Create a logger for the test application's connection.
        TestConsoleLog(kLogInfo, "Setting log file[%s] for test application logging on connection[%d].",
                       filename_buffer_str, connection_info_ptr->my_index);

        // Create a logger for the test application's connection.
        if (!CdiLoggerCreateLog(test_app_logger_handle, connection_info_ptr->connection_handle, &base_log_method_data,
                                &connection_info_ptr->app_file_log_handle)) {
            // Can only log to the console here, since we were unable to open a log file.
            TestConsoleLog(kLogError, "Unable to open log file[%s] for writing.",
                           GetGlobalTestSettings()->base_log_filename_str);
            ret = false;
        }

        // Prepare SDK Logging.
        if (ret) {
            if (GetGlobalTestSettings()->use_single_connection_log_file) {
                // Single log file for the SDK, so just re-use the SDK log filename.
                CdiOsStrCpy(sdk_log_filename_buffer_str, MAX_LOG_FILENAME_LENGTH,
                            GetGlobalTestSettings()->sdk_log_filename_str);
            } else {
                // Generate a unique SDK log filename based on the connection information.
                char filename[MAX_LOG_FILENAME_LENGTH] = {0};
                char directory[MAX_LOG_FILENAME_LENGTH] = {0};
                if (!CdiOsSplitPath(GetGlobalTestSettings()->base_log_filename_str, filename, MAX_LOG_FILENAME_LENGTH,
                                    directory, MAX_LOG_FILENAME_LENGTH)) {
                    TestConsoleLog(kLogError, "CdiOsSplitPath() failed, filename or directory buffer is too small.");
                }
                if (snprintf(sdk_log_filename_buffer_str, MAX_LOG_FILENAME_LENGTH, "%sSDK_%s_%s.log",
                             directory, filename, connection_name_str) >= MAX_LOG_FILENAME_LENGTH) {
                    TestConsoleLog(kLogError, "Path to log file name is too long.");
                    ret = false;
                }
            }
            if (ret) {
                // Using file logger for SDK. The SDK will create the file.
                log_method_data_ptr->log_method = kLogMethodFile;
                log_method_data_ptr->log_filename_str = sdk_log_filename_buffer_str;
            }
        }
    } else {
        if (GetGlobalTestSettings()->use_multiwindow_console) {
            log_method_data_ptr->log_method = kLogMethodCallback;
            log_method_data_ptr->callback_data.log_msg_cb_ptr = TestConsoleLogMessageCallback;
            log_method_data_ptr->callback_data.log_user_cb_param = NULL;
        } else {
            log_method_data_ptr->log_method = kLogMethodStdout;
        }

        if (!CdiLoggerCreateLog(test_app_logger_handle, connection_info_ptr->connection_handle, log_method_data_ptr,
                                &connection_info_ptr->app_file_log_handle)) {
            // Can only log to the console here, since we were unable to open a log file.
            TestConsoleLog(kLogError, "Unable to open log for writing.");
            ret = false;
        }
    }

    return ret;
}

bool IsPayloadNumLessThanTotal(int current_payload_num, int total_payloads)
{
    // If run infinitely or the current payload is less than the total, return true.
    return ((0 == total_payloads) || (current_payload_num < total_payloads));
}

CdiPtpTimestamp GetPtpTimestamp(const TestConnectionInfo* connection_info_ptr,
                                const StreamSettings* stream_settings_ptr,
                                const TestConnectionStreamInfo* stream_info_ptr, int ptp_rate_count)
{
    // Calculate length of time based on PTP rate count and rate period, converting to nanoseconds. Using the rate
    // period as specified on the command line as the base for all PTP calculations.
    uint64_t duration_ns = ptp_rate_count * connection_info_ptr->test_settings_ptr->rate_period_nanoseconds;

    // For audio, make adjustment to simulate a PTP time that is not split across an audio sample.
    if (stream_settings_ptr->avm_data_type == kCdiAvmAudio && !stream_settings_ptr->do_not_use_audio_rtp_time) {
        uint64_t period_adjustment = stream_settings_ptr->audio_sample_period_attoseconds / ATTOSECONDS_TO_NANOSECONDS;
        // If rounding to closest multiple of period_adjustment use this:
        duration_ns = ((duration_ns + period_adjustment / 2 + 1) / period_adjustment) * period_adjustment;
        // Round up to next even multiple of period_adjustment.
        //duration_ns = ((duration_ns - 1) / period_adjustment + 1) * period_adjustment;
    }

    // Add the existing start time nanoseconds to the duration so the logic below calculates the correct seconds and
    // nanoseconds.
    duration_ns += stream_info_ptr->connection_start_time.nanoseconds;

    CdiPtpTimestamp timestamp = {
        .seconds = stream_info_ptr->connection_start_time.seconds + duration_ns / CDI_NANOSECONDS_PER_SECOND,
        .nanoseconds = duration_ns % CDI_NANOSECONDS_PER_SECOND
    };

    return timestamp;
}
