// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in test_control.c.
 */

#ifndef TEST_CONTROL_H__
#define TEST_CONTROL_H__

#include <stdbool.h>
#include <stdint.h>

#include "fifo_api.h"
#include "cdi_pool_api.h"
#include "cdi_raw_api.h"
#include "test_args.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief The maximum length for a file name string (includes additional name characters added automatically by the
/// test app.
#define MAX_LOG_FILENAME_LENGTH     (1024)

/// @brief The fixed number of SGL entries we will try to use when in SGL mode. If payload is too small to be broken
/// into this number of SGL entries, then we will use a smaller number of entries that can accommodate the given
/// payload. Must be greater than 0.
#define MAX_SGL_ENTRIES_PER_PAYLOAD (7)

extern CdiLoggerHandle test_app_logger_handle;

/// @brief Send the log message to application's log for the connection specified by "connection_info_ptr->app_file_log_handle".
#define TEST_LOG_CONNECTION(log_level, ...) \
    CdiLogger(connection_info_ptr->app_file_log_handle, kLogComponentGeneric, log_level, __FUNCTION__, __LINE__, \
              __VA_ARGS__)

/// @brief The number of bytes in a test pattern word.
#define BYTES_PER_PATTERN_WORD      (sizeof(uint64_t))

/// @brief The maximum payload count in the test application, the upper byte is reserved.
#define MAX_TEST_PAYLOAD_COUNT      (0x00FFFFFF)

/// @brief Number of bytes in CDI audio sample. CDI requests 24-bit int for audio, so needs three bytes.
#define CDI_BYTES_PER_AUDIO_SAMPLE  (3)

/// Forward reference.
typedef struct TestConnectionInfo TestConnectionInfo;
/// Forward reference.
typedef struct TestDynamicState* TestDynamicHandle;

/**
 * @brief A structure for storing data to be sent with a payload as user_cb_data.
 */
typedef struct {
    /// Pointer to the TestConnection info data structure that holds state information for the given connection.
    TestConnectionInfo* test_connection_info_ptr;

    /// Time payload transmission started.
    uint64_t tx_payload_start_time;

    /// Zero-based stream index.
    int stream_index;

    /// Memory pool for Tx payload pointed to by tx_payload_sgl_ptr (see below).
    CdiPoolHandle tx_pool_handle;

    /// Pointer to SGL from the pool tx_pool_handle (see above) which describes the current Tx payload buffer.
    CdiSgList* tx_payload_sgl_ptr;
} TestTxUserData;

/**
 * @brief A structure for storing all connection info related to a specific stream, such as data buffer pointers
 * and file handles.
 */
typedef struct {
    /// File handle for file for reading payload data.
    CdiFileID user_data_read_file_handle;

    /// File handle for file to write received payload data to.
    CdiFileID user_data_write_file_handle;

    /// When riff_file=true then next_payload_size will be the size of the next payload to send.
    /// Otherwise next_payload_size is always equal to stream_settings->payload_size.
    int next_payload_size;

    /// Rx expected payload data buffer pointer.
    void* rx_expected_data_buffer_ptr;

    /// Payload buffer size in bytes (rounded-up from payload data size to allow for pattern creation).
    int payload_buffer_size;

    /// Tx payload memory pool buffer size in bytes required to hold all Tx payload buffers.
    int tx_pool_buffer_size;

    /// Handle of memory pool used to hold Tx payloads.
    CdiPoolHandle tx_pool_handle;

    /// The current number of payloads where config data has not been sent. Resets to 0 when matches
    /// test_settings->config_skip.
    int config_payload_skip_count;

    /// The current payload count for this stream.
    int payload_count;

    /// Start time for the connection. For TX this is the time of the first payload sent. For RX it is the time from the
    /// PTP timestamp of the first payload received.
    CdiPtpTimestamp connection_start_time;

    /// For receiver, the last AVM baseline configuration received. Used to log to output when AVM changes.
    CdiAvmBaselineConfig last_baseline_config;

    /// Last PTP origination timestamp.
    CdiPtpTimestamp last_ptp_timestamp;

    uint64_t total_audio_samples; ///< Total number of audio samples processed.
} TestConnectionStreamInfo;

/**
 * @brief A structure for storing all info related to a specific connection, including test settings, connection
 * configuration data from the SDK, and state information for the test connection.
 */
struct TestConnectionInfo {
    /// Lock used to protect access to connection connection_handle.
    CdiCsID connection_handle_lock;

    /// The connection handle returned by the Cdi...TxCreate or Cdi...RxCreate functions.
    CdiConnectionHandle connection_handle;

    /// Array of Tx stream handles associated with this connection. Each handle is returned by the
    /// CdiAvmTxStreamEndpointCreate function.
    CdiEndpointHandle tx_stream_endpoint_handle_array[CDI_MAX_ENDPOINTS_PER_CONNECTION];

    /// This connection's index;
    int my_index;

    /// The config data structure returned by the CdiRawTxCreate or CdiRawRxCreate functions.
    union {
        CdiRxConfigData     rx; ///<Rx Configuration data.
        CdiTxConfigData     tx; ///<Tx Configuration data.
    } config_data;

    /// Pointer to thread-local storage.
    void* local_data_ptr;

    /// Pointer to the test_settings data structure provided by the user to describe the test parameters for this
    /// connection.
    TestSettings* test_settings_ptr;

    /// Signal to indicate when this connection is done with a payload. For a transmitter, this is set when confirmation
    /// of a payload being transmitted has been received via TestTxProcessCoreCallbackData(). For a receiver, it is
    /// set whenever a payload is received via TestRxProcessCoreCallbackData().
    CdiSignalType payload_done_signal;

    /// The current total payload count.
    int payload_count;

    /// The total number of requested payloads.
    int total_payloads;

    /// The current stream counter. We will use this to track which stream we are operating on at a give time.
    int current_stream_count;

    /// Flag to indicate if this connection has passed or failed its testing.
    bool pass_status;

    /// Counter to count payload transmission errors. Used to determine whether to quit or continue based on
    /// --keep_alive setting.
    uint64_t num_payload_errors;

    /// Signal to indicate when this connection is done testing (when payload_count matches total_payloads).
    CdiSignalType done_signal;

    /// Thread ID for this connection.
    CdiThreadID thread_id;

    /// Shutdown signal for this thread.
    CdiSignalType connection_shutdown_signal;

    /// Signal used for connection state changes.
    CdiSignalType connection_state_change_signal;

    /// Current status of the connection.
    volatile CdiConnectionStatus connection_status;

    /// Current status of the streams in a connection.
    CdiConnectionStatus connection_status_stream_array[CDI_MAX_ENDPOINTS_PER_CONNECTION];

    /// The connection handle returned by the CdiFifoCreate function,
    /// used for communicating between callback function and helper threads.
    CdiFifoHandle fifo_handle;

    /// Test application Log file handle for this connection.
    CdiLogHandle app_file_log_handle;

    /// SDK Log file handle for this connection.
    CdiLogHandle sdk_file_callback_log_handle;

    /// Handle of memory pool used to hold TestTxUserData structures.
    CdiPoolHandle tx_user_data_pool_handle;

    /// Number of payloads that were transmitted late.
    uint64_t tx_late_payload_count;

    /// Overall maximum time in microseconds to transmit a payload.
    uint32_t transfer_time_max_overall;

    /// Overall minimum time in microseconds to transmit a payload.
    uint32_t transfer_time_min_overall;

    /// Number of payload counter stats in payload_counter_stats_array.
    int number_stats;

    /// A copy of the last stats. Updated at the end of TestStatisticsCallback().
    CdiPayloadCounterStats payload_counter_stats_array[CDI_MAX_ENDPOINTS_PER_CONNECTION];

    /// Total CPU load for all endpoints associated with this connection.
    int total_poll_thread_load;

    /// Array of stream info data structures for storing stream-specific variables.
    TestConnectionStreamInfo stream_info[CDI_MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION];

    /// Pattern counter used to generate unique value in each payload.
    uint64_t pattern_count;

    /// Instance of test dynamic component related to this connection.
    TestDynamicHandle test_dynamic_handle;
};

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Wait until the connection has been established.
 *
 * @param connection_info_ptr Pointer to connection info data for which to wait for a connection.
 * @param timeout_seconds Timeout in seconds.
 *
 * @return Returns true if successful, false if the timeout occurred while waiting.
 */
bool TestWaitForConnection(TestConnectionInfo* connection_info_ptr, int timeout_seconds);

/**
 * Handle the connection callback.
 *
 * @param   cb_data_ptr  Pointer to core connection callback data.
 */
void TestConnectionCallback(const CdiCoreConnectionCbData* cb_data_ptr);

/**
 * Handle the statistics callback.
 *
 * @param   cb_data_ptr  Pointer to statistics callback data.
 */
void TestStatisticsCallback(const CdiCoreStatsCbData* cb_data_ptr);

/**
 * Increment the connection's payload counter and mark done if we hit the user-specified total_payloads.
 *
 * @param connection_info_ptr Pointer to connection info data for which we want to increment the payload count.
 * @param stream_index Stream index.
 */
void TestIncPayloadCount(TestConnectionInfo* connection_info_ptr, int stream_index);

/**
 * Prepare next set of payload data. This is either reading the next payload from the file or incrementing the payload
 * identifier value when using patterns. This function is shared for managing both Tx and Rx buffer patterns.
 *
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   payload_buffer_size     Payload buffer size, rounded up to multiple of 8 bytes.
 * @param   read_file_handle_ptr    Pointer to the user read data file handle which this function will set.
 * @param   buffer_ptr              Pointer to buffer of payload size to be filled with initial pattern words if using
 *                                  patterns.
 *
 * @return                          if successful return true, otherwise returns false.
 */
bool PreparePayloadData(StreamSettings* stream_settings_ptr, int payload_buffer_size, CdiFileID* read_file_handle_ptr,
                        void* buffer_ptr);

/**
 * Prepare next set of payload data. This is either reading the next payload from the file or incrementing the payload
 * identifier value when using patterns.
 *
 * @param   connection_info_ptr     Pointer to test connection information.
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   payload_id              Payload identifier.
 * @param   read_file_handle        The file handle for reading the next pattern from, if the pattern type is from file.
 * @param   sgl_ptr                 Pointer to an SGL describing the buffer to be filled with payload data.
 *
 * @return                          if successful return true, otherwise returns false.
 */
bool GetNextPayloadDataSgl(const TestConnectionInfo* connection_info_ptr, const StreamSettings* stream_settings_ptr,
    int payload_id, CdiFileID read_file_handle, CdiSgList* sgl_ptr);

/**
 * Prepare next set of payload data. This is either reading the next payload from the file or incrementing the payload
 * identifier value when using patterns.
 *
 * @param   connection_info_ptr     Pointer to test connection information.
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   stream_info_ptr         Pointer to stream state.
 *
 * @return                          if successful return true, otherwise returns false.
 */
bool GetNextPayloadDataLinear(const TestConnectionInfo* connection_info_ptr, const StreamSettings* stream_settings_ptr,
    TestConnectionStreamInfo* stream_info_ptr);

/**
 * Create a unique log file name for this application's connection and associate it with the current thread. This
 * allows use of the CDI_LOG_THREAD() macro to direct log messages to the logger.
 *
 * @param connection_info_ptr Pointer to test connection information.
 * @param log_method_data_ptr Pointer to log method data.
 * @param sdk_log_filename_buffer_str Pointer to buffer for return SDK log filename.
 *
 * @return bool Returns true if successful, otherwise false is returned.
 */
bool TestCreateConnectionLogFiles(TestConnectionInfo* connection_info_ptr, CdiLogMethodData* log_method_data_ptr,
                                  char* sdk_log_filename_buffer_str);

/**
 * Check whether the current payload number is less that the total payloads allowed.
 *
 * @param current_payload_num Number that represents the current payload number.
 * @param total_payloads Number that represents the total number of payloads for this connection (0 is infinite).
 *
 * @return bool Indicates if the current payload number is less than the total number of payloads. This is always
 * true if infinite payloads.
 */
bool IsPayloadNumLessThanTotal(int current_payload_num, int total_payloads);

/**
 * @brief Get the next video PTP timestamp to use in the payload origination_ptp_timestamp.
 *
 * @param connection_info_ptr Pointer to test connection information.
 * @param stream_info_ptr     Pointer to stream info.
 * @param ptp_rate_count      Current PTP rate counter value.
 *
 * @return A PTP timestamp for the next payload to send.
 */
CdiPtpTimestamp GetVideoPtpTimestamp(const TestConnectionInfo* connection_info_ptr,
                                     const TestConnectionStreamInfo* stream_info_ptr, int ptp_rate_count);

/**
 * @brief Get the next audio PTP timestamp to use in the payload origination_ptp_timestamp.
 *
 * @param stream_settings_ptr Pointer to stream settings info.
 * @param stream_info_ptr     Pointer to stream info.
 * @param audio_data_size     Size of audio data in bytes.
 *
 * @return A PTP timestamp for the next payload to send.
 */
CdiPtpTimestamp GetAudioPtpTimestamp(const StreamSettings* stream_settings_ptr,
                                     TestConnectionStreamInfo* stream_info_ptr, int audio_data_size);

/**
 * @brief Log timestamp, if enabled.
 *
 * @param stream_settings_ptr Pointer to stream settings.
 * @param stream_info_ptr       Pointer to stream info.
 * @param current_ptp_timestamp Current PTP timestamp value to log.
 */
void LogTimestamps(const StreamSettings* stream_settings_ptr, TestConnectionStreamInfo* stream_info_ptr,
                   CdiPtpTimestamp current_ptp_timestamp);

#endif // TEST_CONTROL_H__
