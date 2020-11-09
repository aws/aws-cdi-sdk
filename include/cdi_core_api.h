// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

#ifndef CDI_CORE_API_H__
#define CDI_CORE_API_H__

/**
 * @file
 * @brief
 * This file declares the public API data types, structures and functions that comprise the CDI low-level transport
 * SDK API.
 */

// Doxygen for CDI-CORE
/*! @page Core_home_page CDI Core (CDI-CORE) API Home Page
 * @tableofcontents
 *
 * @section Core_arch CDI-CORE Architecture
 *
 * The diagram shown below provides a overview of the CDI-CORE architecture.
 *
 * @image html "high_level_architecture.jpg"
 *
 * @section Core_main_api CDI-CORE Application Programming Interface (API)
 *
 * The API is declared in: @ref cdi_core_api.h
*/

/*
 * With older distros (Ubuntu 16.04 in particular), "struct timespec" often is undefined prior to references to it.
 * C source files likely include time.h indirectly and sometimes directly prior to its inclusion below and if
 * _POSIX_C_SOURCE is undefined or defined to be less than 200112L, it does not get defined through the inclusion here.
 * The only solution is to ensure that it is defined with the required value early in each compilation unit.
 *
 * The block below ensures that _POSIX_C_SOURCE is defined with a value of 200112L or greater, erring out if not.
 */
#if !defined(_POSIX_C_SOURCE)
/// @brief This define ensures that "struct timespec" is defined prior to using it.
#define _POSIX_C_SOURCE 200112L
#else
#if _POSIX_C_SOURCE < 200112L
/*
 * If the compiler hits this #error, define _POSIX_C_SOURCE to the value 200112L or a newer supported value in the
 * definitions passed in from the command line.
 */
#error _POSIX_C_SOURCE must be >= 200112L
#endif  // _POSIX_C_SOURCE < 200112L
#endif  // !defined(_POSIX_C_SOURCE)

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cdi_log_enums.h"
#include "cdi_utility_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

// NOTE: Do not change the ordering of the three defines below. They are used internally by the CDI SDK makefile or
// scripts to extract version information.

/// @brief CDI version.
#define CDI_SDK_VERSION             1

/// @brief CDI major version.
#define CDI_SDK_MAJOR_VERSION       0

/// @brief CDI minor version.
#define CDI_SDK_MINOR_VERSION       0

/// @brief Define to limit the max number of allowable Tx or Rx connections that can be created in the SDK.
#define MAX_SIMULTANEOUS_CONNECTIONS                (30)

/// @brief Define to limit the max number of allowable Tx or Rx endpoints for a single connection that can be created in
/// the SDK.
#define MAX_ENDPOINTS_PER_CONNECTION                (5)

/// @brief Define to limit the max number of allowable payloads that can be simultaneously sent on a single connection
/// in the SDK. NOTE: This value is used to mask the MSBs of array indices so this value must be a power of two.
#define MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION  (8)

/// @brief The number of entries the payloads per connection queues may grow.
#define MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION_GROW (2)

/// @brief Define to limit the max number of allowable payload SGL entries that can be simultaneously sent on a single
/// connection in the SDK. 3500 SGL entries supports 4K at 10-bits packed using 2110-20.
#define MAX_SIMULTANEOUS_TX_PAYLOAD_SGL_ENTRIES_PER_CONNECTION   (MAX_SIMULTANEOUS_TX_PAYLOADS_PER_CONNECTION*3500)

/// @brief The number of entries the payload SGL entries per connection queues may grow.
#define MAX_SIMULTANEOUS_TX_PAYLOAD_SGL_ENTRIES_PER_CONNECTION_GROW (2)

/// @brief Define to limit the max number of allowable payloads that can be simultaneously received on a single
/// connection in the SDK. This number should be larger than the respective Tx limit since more payloads can
/// potentially be in flight in the receive logic. This is because Tx packets can get acknowledged to the transmitter
/// before being fully processed by the receiver, allowing the transmitter to send more.
/// This number must also be as large or larger than the maximum SRD packet ordering window so that we can be sure
/// we make enough room in our state arrays for tracking all possible payloads that could be in flight at the same time.
/// NOTE: This value must be a power of two because it is used to mask the MSBs of array indices. @see RxPacketReceive
#define MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION  (32)

/// @brief Define to limit the max number of payloads that can arrive out of order and be put back in order.
#define MAX_RX_PAYLOAD_OUT_OF_ORDER_BUFFER  (32)

/// @brief The number of entries the Rx Payload is allowed to
/// grow if a PoolIncrease is called.
#define MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION_GROW (2)

/// @brief Maximum connection name string length.
#define MAX_CONNECTION_NAME_STRING_LENGTH           (128)

/// @brief Maximum stream name string length.
#define MAX_STREAM_NAME_STRING_LENGTH               (MAX_CONNECTION_NAME_STRING_LENGTH+10)

/// @brief Maximum log filename string length.
#define MAX_LOG_FILENAME_LENGTH                     (1024)

/// @brief Enables internal SDK debug info for Scatter-Gather-List entries.
#define DEBUG_INTERNAL_SGL_ENTRIES

/// @brief When Rx Buffer delay is enabled using "-1", this is the delay used in milliseconds. This is 4 video frames at
/// 60FPS (4*16.6ms= 66.4ms). This value is the recommended buffer size for transport between instances that are not
/// in a cluster placement group.
#define ENABLED_RX_BUFFER_DELAY_DEFAULT_MS          (67)

/// @brief Maximum Rx buffer delay in milliseconds. This is approximately 6 video frames at 60FPS (6*16.6ms= ~100ms).
#define MAXIMUM_RX_BUFFER_DELAY_MS                  (100)

/// @brief The millisecond divisor used to calculate how many additional packet buffers to allocate for the Rx buffer.
/// A value of 10 here corresponds to 100FPS (10ms).
#define RX_BUFFER_DELAY_BUFFER_MS_DIVISOR           (10)

// Declare forward references for internal structures that are not directly available through the API.
struct CdiAdapterState;
struct CdiConnectionState;
struct CdiMemoryState;
/// @brief Forward structure declaration to create pointer to log data when used.
typedef struct CdiLogMethodData CdiLogMethodData;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a network adapter. Each handle represents a
 * instance of a network adapter.
 */
typedef struct CdiAdapterState* CdiAdapterHandle;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a transmitter or receiver connection. Each handle
 * represents a single data flow.
 */
typedef struct CdiConnectionState* CdiConnectionHandle;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a transmitter or receiver endpoint. Each handle
 * represents a single data flow.
 */
typedef struct CdiEndpointState* CdiEndpointHandle;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for holding private SDK data that relates to memory.
 */
typedef struct CdiMemoryState* CdiMemoryHandle;

/**
 * @brief Type used as user defined data that is passed to the registered user RX/TX callback functions.
 */
typedef void* CdiUserCbParameter;

/**
 * @brief Values used for API function return codes.
 *
 * NOTE: Additions to and removals from this enumeration need to be reflected in CdiCoreStatusToString().
 */
typedef enum {
    /// The API function succeeded.
    kCdiStatusOk                    = 0,

    /// An unspecified, unrecoverable error occurred.
    kCdiStatusFatal                 = 1,

    /// An attemt to allocated memory from the heap failed.
    kCdiStatusNotEnoughMemory       = 2,

    /// The appropriate SDK initialization function has not yet been called.
    kCdiStatusNotInitialized        = 3,

    /// A chunk of data was not delivered to the receiver before its deadline was reached.
    kCdiStatusMaxLatencyExceeded    = 4,

    /// The handle passed in to an SDK function is not valid.
    kCdiStatusInvalidHandle         = 5,

    /// A parameter passed in to an SDK function is not valid.
    kCdiStatusInvalidParameter      = 6,

    /// This status is only returned by CdiTxPayload() when the connection to the receiver has not been established or
    /// when the receiver has become disconnected due to either network problems or the receiving host becoming
    /// non-responsive.
    kCdiStatusNotConnected          = 7,

    /// The operation failed due to a queue resource being full.
    kCdiStatusQueueFull             = 8,

    /// The operation failed due to the connection type is not valid for the requested operation.
    kCdiStatusInvalidConnectionType = 9,

    /// A payload was received, but an error occurred. The payload is being discarded.
    kCdiStatusRxPayloadError        = 10,

    /// A payload was received, but it was not using the protocol specified by the received connection. This means
    /// either a AVM payload was received on a RAW connection or a RAW payload was received on a AVM connection.
    kCdiStatusRxWrongProtocolType   = 11,

    /// Unable to create a log file.
    kCdiStatusCreateLogFailed       = 12,

    /// Unable to create a thread.
    kCdiStatusCreateThreadFailed    = 13,

    /// The current connection is shutting down, so resources that may be actively being used (E.g. a FIFO) will abort
    /// and return this status.
    kCdiStatusShuttingDown          = 14,

    /// An attempt was made to perform an Rx function on a Tx connection, or vice-versa.
    kCdiStatusWrongDirection        = 15,

    /// When performing a port query, the function failed.
    kCdiStatusGetPortFailed         = 16,

    /// Attempt to use a connection that is stopped.
    kCdiStatusNotReady              = 17,

    /// Tx data failed to post to endpoint queue.
    kCdiStatusSendFailed            = 18,

    /// Attempt to allocate a non-memory resource failed.
    kCdiStatusAllocationFailed      = 19,

    /// Attempt to open a connection (E.g. socket) failed.
    kCdiStatusOpenFailed            = 20,

    /// Attempt was made to create an identical endpoint that is already in use.
    kCdiStatusDuplicate             = 21,

    /// Invalid SGL found when processing.
    kCdiStatusInvalidSgl            = 22,

    /// An endpoint state change occurred.
    kCdiStatusEndpointManagerState  = 23,

    /// Buffer is not large enough to hold data.
    kCdiStatusBufferOverflow        = 24,

    /// No packets were received for a payload.
    kCdiStatusRxPayloadMissing      = 25,

    /// The size of an internal array that does not dynamically grow was exceeded.
    kCdiStatusArraySizeExceeded     = 26,

    /// An unspecified, recoverable error occurred.
    kCdiStatusNonFatal              = 27,

    /// The SDK was built without CloudWatch being enabled. The CloudWatch SDK is required and must be specifically
    /// referenced as part of building the SDK. See RELEASE_NOTES file or Setup/Install Guide for details.
    kCdiStatusCloudWatchNotEnabled  = 28,

    /// A CloudWatch request was throttled. Need to try the request again at a later time.
    kCdiStatusCloudWatchThrottling  = 29,

    /// A CloudWatch request failed due to invalid credentials. Verify that the access and secret keys have been setup
    /// correctly.
    kCdiStatusCloudWatchInvalidCredentials = 30,
    // NOTE: Additions to and removals from this enumeration need to be reflected in CdiCoreStatusToString().

    /// Internal only status: the function succeeded but did nothing productive.
    kCdiStatusInternalIdle          = 31,

    /// An attempt was made to create a duplicate adapter entry.
    kCdiStatusAdapterDuplicateEntry = 32,

    /// An attempt was made to use a profile that is not supported.
    kCdiStatusProfileNotSupported   = 33,
} CdiReturnStatus;

/// @brief A structure for holding a PTP timestamp defined in seconds and nanoseconds. This PTP time as defined by
/// SMPTE ST 2059-2 and IEEE 1588-2008 with the exception that the seconds field is an unsigned 32 bit integer instead
/// of the specified 48 bit integer.
//typedef struct CdiPtpTimestamp CdiPtpTimestamp;
typedef struct CdiPtpTimestamp {
    /// The number of seconds since the SMPTE Epoch which is 1970-01-01T00:00:00.
    uint32_t seconds;
    /// The number of fractional seconds as measured in nanoseconds. The value in this field is always less than 10^9.
    uint32_t nanoseconds;
} CdiPtpTimestamp;

/// @brief Extra data that is sent along with payloads to the receiver. It will be provided to the receiver through
/// registered user receive data callback functions.
typedef struct {
    /// @brief Origination timestamp to associate with the payload. This timestamp is a PTP timestamp as outlined
    /// by SMPTE ST 2059-2. The one exception is the seconds field is stored as an unsigned 32 bit integer instead of
    /// the specified unsigned 48 bit integer.
    CdiPtpTimestamp origination_ptp_timestamp;

    /// @brief User-defined data to associate with the payload.
    uint64_t payload_user_data;
} CdiCoreExtraData;

/// @brief A structure used to configure a transmit payload.
typedef struct {
    /// @brief Extra data that was sent along with the payload.
    CdiCoreExtraData core_extra_data;

    /// @brief User defined callback parameter specified when using one of the Cdi...Tx...Payload() API functions. This
    /// allows the application to associate a TX payload to a single TX registered user callback function.
    CdiUserCbParameter user_cb_param;

    /// @brief The size in bits of the units being transferred. This ensures a single unit is not split across sgl
    /// entries. As an example a 10 bit pixel would be set to 10 to ensure that no pixels are split.
    int unit_size;
} CdiCoreTxPayloadConfig;

/**
 * @brief This enumeration is used to indicate the current state of a connection.
 * NOTE: Any changes made here MUST also be made to "connection_status_key_array" in cdi_utility_api.c.
 */
typedef enum {
    kCdiConnectionStatusDisconnected, ///< Disconnected. The SDK is trying to establish the connection.
    kCdiConnectionStatusConnected,    ///< Connected and ready for use.
} CdiConnectionStatus;

/**
 * @brief A structure of this type is passed as the parameter to CdiCoreConnectionCallback(). It contains data related
 * to the status of a single connection.
 */
typedef struct {
    /// @brief Current status of the connection.
    CdiConnectionStatus status_code;

    /// @brief If the connection is not connected, this will point to a NULL terminated error message string. The memory
    /// containing the error string is allocated by the SDK and will be freed upon return from the callback function.
    /// The application needs to copy the message to its own memory before returning if it needs it to be retained.
    const char* err_msg_str;

    /// @brief Used to identify the source data stream number associated with this connection.
    int stream_identifier;

    /// @brief Used to identify the handle of the stream endpoint associated with this connection.
    CdiEndpointHandle endpoint_handle;

    /// @brief User defined connection callback parameter. For a transmitter, this value is set as part of the
    /// CdiTxConfigData data provided as a parameter to one of the Cdi...TXCreate() API functions. For a receiver, this
    /// value is set as part of the CdiRxConfigData data provided to one of the Cdi...RxCreate() API functions.
    CdiUserCbParameter connection_user_cb_param;
} CdiCoreConnectionCbData;

/**
 * @brief Prototype of connection callback function. The user code must implement a function with this prototype and
 *        provide it in CdiTxConfigData or CdiRxConfigData structure when using one of the Cdi...Create() API functions.
 *
 * This callback function is invoked whenever the connection status changes.
 *
 * @param data_ptr A pointer to an CdiCoreConnectionCbData structure.
 */
typedef void (*CdiCoreConnectionCallback)(const CdiCoreConnectionCbData* data_ptr);

/**
 * @brief A structure of this type is passed as part of the data to the TX/RX registered user callback functions. It
 * contains data related to the transmission of a single payload.
 */
typedef struct {
    /// @brief If payload was successfully sent and received by the receiver, value will be kCdiStatusOk.
    /// Otherwise,indicates the general reason for the error condition. Use err_msg_str for a detailed error message
    /// string.
    CdiReturnStatus status_code;

    /// @brief If an error or timeout occurred while transmitting the payload, this will point to a NULL terminated
    /// error message string. The memory containing the error string is allocated by the SDK and will be freed upon
    /// return from the callback function. The application needs to copy the message to its own memory before returning
    /// if it needs it to be retained.
    const char* err_msg_str;

    /// @brief The handle of the instance which was created using a previous call to one of the Cdi...Create() API
    /// functions.
    CdiConnectionHandle connection_handle;

    /// @brief Extra data that was sent along with the payload.
    CdiCoreExtraData core_extra_data;

    /// @brief User defined callback parameter. For a transmitter, this value is set as part of the
    /// CdiCoreTxPayloadConfig data provided as a parameter to one of the Cdi...TX..Payload() API functions. For a
    /// receiver, this value is set as part of the CdiRxConfigData data provided to one of the Cdi...RxCreate()
    /// APIs functions.
    CdiUserCbParameter user_cb_param;
} CdiCoreCbData;

/**
 * @brief This selector determines the type of network adapter in the API function.
 * NOTE: Any changes made here MUST also be made to "adapter_type_key_array" in cdi_avm_api.c.
 */
typedef enum {
    /// @brief This adapter type is the typical choice for high throughput, reliable delivery of data. In order to use
    /// it, the host must meet a number of requirements documented elsewhere.
    kCdiAdapterTypeEfa,

    /// @brief This adapter type is mainly useful for testing. It does not provide the same level of throughput as EFA
    /// does but it does not require any special type of EC2 instance.
    kCdiAdapterTypeSocket,

    /// @brief This adapter type is mainly useful for testing. This is similar to kCdiAdapterTypeSocket except that it
    /// uses libfabric to perform the work of sending over the socket.
    kCdiAdapterTypeSocketLibfabric
} CdiAdapterTypeSelection;

/**
 * @brief Configuration data used by the CdiCoreNetworkAdapterInitialize() API function.
 */
typedef struct {
    /// @brief The IP address to use for the local network interface dedicated to the SDK's use. NOTE: This must be the
    /// dotted form of an IPv4 address. DHCP and/or DNS may be supported in the future.
    const char* adapter_ip_addr_str;

    /// @brief The size in bytes of a memory region for holding payload data to transmit. A special memory type is used
    /// so both CPU and DMA hardware can access the memory. The application manages how the buffer is partitioned and
    /// used. NOTE: The value should be at least twice the total size of the maximum payload size of each transmit
    /// connection that will be created using the Cdi...TxCreate() API functions. This allows the application to setup
    /// data for a payload while a previous payload is being transmitted.
    uint64_t tx_buffer_size_bytes;

    /// @brief Returned pointer to start of the allocated transmit buffer. Size is specified using
    /// tx_buffer_size_kbytes.
    void* ret_tx_buffer_ptr;

    /// @brief The type of adapter to use/initialize.
    CdiAdapterTypeSelection adapter_type;
} CdiAdapterData;

/**
 * @brief Transfer statistics data specific to payloads that contain counters that increment for the duration of
 * the connection. They are never reset.
 */
typedef struct {
    /// @brief Current number of payloads successfully transferred since the connection was created.
    int num_payloads_transferred;

    /// @brief The number of payloads that have been dropped due to timeout conditions since the connection was created.
    /// Payloads are typically dropped because of network connectivity issues but will also occur when the receiving
    /// host is unresponsive among other possible causes.
    int num_payloads_dropped;

    /// @brief Number of payloads that were transmitted late since the connection was created.
    int num_payloads_late;
} CdiPayloadCounterStats;

/**
 * @brief Transfer statistics data specific to payloads that are reset at the start of each time interval as specified
 * using #CdiStatsConfigData.stats_period_seconds. A snapshot of the current values is made and provided through the
 * user-registered callback function CdiCoreStatsCallback().
 */
typedef struct {
    /// @brief Minimum time to transfer a payload over the time interval.
    uint32_t transfer_time_min;

    /// @brief Maximum time to transfer a payload over the time interval.
    uint32_t transfer_time_max;

    /// @brief Number of payloads transferred over the time interval.
    int transfer_count;

    /// @brief Accumulating sum of time to transfer payloads over the time interval.
    uint64_t transfer_time_sum;

    /// @brief The median time to transfer a payload over the time interval.
    uint32_t transfer_time_P50;

    /// @brief The 90th percentile time to transfer a payload over the time interval.
    uint32_t transfer_time_P90;

    /// @brief The 99th percentile time to transfer a payload over the time interval.
    uint32_t transfer_time_P99;
} CdiPayloadTimeIntervalStats;

/**
 * @brief Transfer statistics data specific to an adapter endpoint. Used in the CdiTransferStats structure as a
 * parameter of the user-registered CdiCoreStatsCallback() API function.
 */
typedef struct {
    /// Number of times the connection has been lost.
    uint32_t dropped_connection_count;

    /// Number of probe command retries due to dropped/lost control packets. The control protocol is UDP based and does
    /// not use the SRD hardware. This provides a secondary channel of communication.
    uint32_t probe_command_retry_count;

    /// The true load on the polling thread's CPU core in units of hundredths of a percent. The normal range of this
    /// value is between 0 and 10000 (0% to 100.00%) but it may be -1 to indicate a computation error. This value is
    /// determined by computing the portion of each five second window that is spent doing productive work, as opposed
    /// to spinning while it has nothing to do.
    int poll_thread_load;

    bool connected; ///< true if connected, false if not connected.
} CdiAdapterEndpointStats;

/**
 * @brief Transfer statistics data. Used as a parameter of the user-registered CdiCoreStatsCallback() API function.
 */
typedef struct {
    /// @brief Time when last statistic of the set was gathered. Units is in milliseconds since epoch.
    uint64_t timestamp_in_ms_since_epoch;

    /// @brief A string that defines the name of the stream. This is a copy of the string, since the associated endpoint
    /// can be destroyed while this data is queuing to CloudWatch.
    char stream_name_str[MAX_STREAM_NAME_STRING_LENGTH];

    CdiPayloadCounterStats payload_counter_stats; ///< Statistics data specific to payloads that don't reset.
    CdiPayloadTimeIntervalStats payload_time_interval_stats; ///< Statistics data specific to payloads that reset.
    CdiAdapterEndpointStats endpoint_stats; ///< Statistics data specific to adapter endpoints.
} CdiTransferStats;

/**
 * @brief A structure of this type is passed as the parameter to CdiCoreStatsCallback(). It contains data related
 * to the statistics of a single connection.
 */
typedef struct {
    int stats_count; ///< Number of items in transfer_stats_array.
    CdiTransferStats* transfer_stats_array; ///< Array of the accumulated statistics.

    /// @brief User defined statistics callback parameter. This value is set as part of the CdiStatsConfigData structure
    /// when creating a connection or using CdiCoreStatsReconfigure().
    CdiUserCbParameter stats_user_cb_param;
} CdiCoreStatsCbData;

/**
 * @brief Prototype of statistics callback function. The user code must implement a function with this prototype and
 *        provide it as a parameter to the CdiCoreStatsRegisterCallback() API function.
 *
 * This callback function is invoked whenever statistics gathering interval has expired.
 *
 * @param data_ptr A pointer to an CdiCoreStatsCbData structure.
 */
typedef void (*CdiCoreStatsCallback)(const CdiCoreStatsCbData* data_ptr);

/**
 * @brief A structure that is used to hold statistics gathering configuration data.
 */
typedef struct {
    /// @brief How often to gather statistics and make available through the user-registered statistics callback
    /// function (see stats_cb_ptr). Statistics will also be sent directly to a CloudWatch Endpoint, if enabled (see
    /// #CdiCoreConfigData.cloudwatch_config_ptr).
    uint32_t stats_period_seconds;

    /// @brief If CloudWatch has been configured, use this value to disable/enable sending statistics to it.
    bool disable_cloudwatch_stats;
} CdiStatsConfigData;

/**
 * @brief Configuration data used by one of the Cdi...TxCreate() API functions.
 */
typedef struct {
    /// @brief Handle of the adapter to use for this connection. The handle is returned by the
    /// CdiCoreNetworkAdapterInitialize() API function.
    CdiAdapterHandle adapter_handle;

    /// @brief The IP address of the host which is to receive the flow from this transmitter. NOTE: This must be the
    /// dotted form of an IPv4 address. DNS may be supported in the future.
    const char* dest_ip_addr_str;

    /// @brief The port number to use at the receiving host. The range of valid values is 1 to 65535, inclusive and must
    /// match the value configured for the receiving connection.
    int dest_port;

    /// @brief The core to dedicate to this connection's packet send polling thread. A value of -1 disables pinning the
    /// thread to a specific core otherwise the value must be between 0 (inclusive) and the number of CPU cores
    /// (exclusive) in the host. The packet send thread continuously polls the underlying hardware for a time to send
    /// packets when the packet send queue is not empty so it can consume a large portion of the available time on
    /// whatever CPU it's running whether pinned or not.
    int thread_core_num;

    /// @brief Pointer to name of the connection. It is used as an identifier when generating log messages that are
    /// specific to this connection. If NULL, a name is internally generated. Length of name must not exceed
    /// MAX_CONNECTION_NAME_STRING_LENGTH.
    const char* connection_name_str;

    /// @brief Pointer to log configuration data for this connection.
    CdiLogMethodData* connection_log_method_data_ptr;

    /// @brief Address of the user function to call whenever the status of a connection changes.
    CdiCoreConnectionCallback connection_cb_ptr;

    /// @brief User defined callback parameter passed to the user registered connection callback function (see
    /// connection_cb_ptr). This allows the application to associate a TX connection to a single registered user
    /// callback function.
    CdiUserCbParameter connection_user_cb_param;

    /// @brief Address of the user function to call whenever a new set of statistics is available.
    CdiCoreStatsCallback stats_cb_ptr;

    /// @brief User defined callback parameter passed to the user registered statistics callback function (see
    /// stats_cb_ptr). This allows the application to associate statistics to a single registered user
    /// callback function.
    CdiUserCbParameter stats_user_cb_param;

    /// @brief Configuration data for gathering statistics. The data can be changed at runtime using the
    /// CdiCoreStatsReconfigure() API function.
    CdiStatsConfigData stats_config;
} CdiTxConfigData;

/**
 * @brief This typedef is declared here so we can use it as a pointer element within the structure.
 */
typedef struct CdiSglEntry CdiSglEntry;
/**
 * @brief This structure represents a single, contiguous region of memory as part of a scatter-gather list.
 */
struct CdiSglEntry {
    /// @brief The starting address of the data.
    void* address_ptr;

    /// @brief The size of the data in bytes.
    int size_in_bytes;

    /// @brief Handle to private data used within the SDK that relates to this SGL entry. Do not use or modify this
    /// value.
    void* internal_data_ptr;

#ifdef DEBUG_INTERNAL_SGL_ENTRIES
    uint16_t packet_sequence_num; ///< Packet sequence number for the payload.
    uint8_t payload_num;          ///< Payload number this CDI packet is associated with.
#endif

    /// @brief The next entry in the list or NULL if this is the final entry in the list.
    CdiSglEntry* next_ptr;
};

/**
 * @brief This structure defines a scatter-gather list (SGL) which is used to represent an array of data comprising one
 * or more contiguous regions of memory.
 */
typedef struct {
    /// @brief Total size of data in the list, in units of bytes. This value can be calculated by walking the sgl_array,
    /// but is provided here for convenience and efficiency. NOTE: This value must be the same as the value calculated
    /// from walking the list and summing the size_in_bytes for each CdiSglEntry.
    int total_data_size;

    /// @brief Pointer to the first entry in the singly-linked list of SGL entries.
    CdiSglEntry* sgl_head_ptr;

    /// @brief Pointer to the last entry in the singly-linked list of SGL entries.
    CdiSglEntry* sgl_tail_ptr;

    /// @brief Handle to internal data used within the SDK that relates to this SGL. Do not use or modify this value.
    void* internal_data_ptr;
} CdiSgList;

/**
 * @brief Values used to determine type of receive buffer to configure for a receiver connection.
 * NOTE: Any changes made here MUST also be made to "buffer_type_key_array" in cdi_avm_api.c.
 */
typedef enum {
    /// @brief Use a linear buffer to store received payload data. Depending on hardware capabilities, this may require
    /// memcpy.
    kCdiLinearBuffer       = 0,

    /// @brief Use scatter-gather buffers to store received payload data.
    kCdiSgl                = 1,
} CdiBufferType;

/**
 * @brief Configuration data used by one of the Cdi...RxCreate() API functions.
 */
typedef struct {
    /// @brief Handle of the adapter to use for this connection. The handle is returned by the
    /// CdiCoreNetworkAdapterInitialize() API function.
    CdiAdapterHandle adapter_handle;

    /// @brief Destination port. Value must match the dest_port specified by the transmitter which must be configured to
    /// send to the same port number. If running in an unprivileged process, this value must be in the range of
    /// unprivileged port numbers.
    int dest_port;

    /// @brief The core to dedicate to this connection's packet reception polling thread. A value of -1 disables pinning
    /// the thread to a specific core otherwise the value must be between 0 (inclusive) and the number of CPU cores
    /// (exclusive) in the host. The packet receive thread continuously polls the underlying hardware for packets so it
    /// always consumes 100% of the available time on whatever CPU it's running whether pinned or not.
    int thread_core_num;

    /// @brief Type of RX buffer to use for incoming data.
    CdiBufferType rx_buffer_type;

    /// @brief Number of milliseconds to delay invoking the user-registered callback function for incoming payloads. Use
    /// 0 to disable, -1 to enable and the SDK automatic default value (ENABLED_RX_BUFFER_DELAY_DEFAULT_MS) or use a
    /// value up to MAXIMUM_RX_BUFFER_DELAY_MS.
    int buffer_delay_ms;

    /// @brief Size in bytes of the linear receive buffer used by this RX connection. This buffer is reserved from the
    /// RX buffer allocated as part of initialization of the adapter (see adapter_rx_linear_buffer_size). NOTE: This
    /// value is only used if rx_buffer_type = kCdiLinearBuffer.
    uint64_t linear_buffer_size;

    /// @brief The max number of allowable payloads that can be simultaneously received on a single connection in the
    /// SDK. This number should be larger than the respective Tx limit since more payloads can potentially be in flight
    /// in the receive logic. This is because Tx packets can get acknowledged to the transmitter before being fully
    /// processed by the receiver, allowing the transmitter to send more. This number must also be as large or larger
    /// than the maximum SRD packet ordering window so that we can be sure we make enough room in our state arrays for
    /// tracking all possible payloads that could be in flight at the same time.
    /// NOTE: If unspecified (0), then MAX_SIMULTANEOUS_RX_PAYLOADS_PER_CONNECTION will be used.
    int max_simultaneous_rx_payloads_per_connection;

    /// @brief User defined callback parameter passed to a registered user RX callback function. This allows the
    /// application to associate an RX connection to a single RX callback function.
    CdiUserCbParameter user_cb_param;

    /// @brief Pointer to name of the connection. It is used as an identifier when generating log messages that are
    /// specific to this connection. If NULL or points to '\0', it is given the name generated from the SDK found in the
    /// saved_connection_name_str member of CdiConnectionState.
    const char* connection_name_str;

    /// @brief Pointer to log configuration data for this connection.
    CdiLogMethodData* connection_log_method_data_ptr;

    /// @brief Address of the user function to call whenever the status of a connection changes.
    CdiCoreConnectionCallback connection_cb_ptr;

    /// @brief User defined callback parameter passed to the user registered RX connection callback function (see
    /// connection_cb_ptr). This allows the application to associate a RX connection to a single registered user
    /// callback function.
    CdiUserCbParameter connection_user_cb_param;

    /// @brief Address of the user function to call whenever a new set of statistics is available.
    CdiCoreStatsCallback stats_cb_ptr;

    /// @brief User defined callback parameter passed to the user registered statistics callback function (see
    /// stats_cb_ptr). This allows the application to associate statistics to a single registered user
    /// callback function.
    CdiUserCbParameter stats_user_cb_param;

    /// @brief Configuration data for gathering statistics. The data can be changed at runtime using the
    /// CdiCoreStatsReconfigure() API function.
    CdiStatsConfigData stats_config;
} CdiRxConfigData;

/**
 * @brief A structure that is used to hold statistics gathering configuration data that is specific to CloudWatch.
 *
 * NOTE: For periods ((see #CdiStatsConfigData.stats_period_seconds) less than 60 seconds, high resolution storage for
 * metrics will be enabled. This means metrics are stored at 1-second resolution. If false, metrics are stored at
 * 1-minute resolution (CloudWatch default value).
 */
typedef struct {
    /// @brief Pointer to a string that defines the CloudWatch namespace used to hold metrics generated by CDI. If
    /// NULL is used, then the string defined by CLOUDWATCH_DEFAULT_NAMESPACE_STRING is used.
    const char* namespace_str;

    /// @brief Pointer to a string that defines the EC2 region where the CloudWatch container is located. If NULL, the
    /// region where CDI is running will be used.
    const char* region_str;

    /// @brief Pointer to a string that defines a dimension called "Domain" that is associated with each metric. This
    /// value is required and cannot be NULL.
    const char* dimension_domain_str;
} CloudWatchConfigData;

/**
 * @brief SDK configuration data used by the CdiCoreInitialize() API function.
 */
typedef struct {
    /// @brief Specifies the default set of log messages to use.
    CdiLogLevel default_log_level;

    /// @brief Pointer to global log method configuration data.
    const CdiLogMethodData* global_log_method_data_ptr;

    /// @brief Pointer to configuration data specific to CloudWatch. The statics gathering period is uniquely defined
    /// for each connection (see #CdiStatsConfigData.stats_period_seconds) when the connection is created and can be
    /// changed at any time using CdiCoreStatsReconfigure(). If this value is NULL, then CloudWatch will not be used.
    const CloudWatchConfigData* cloudwatch_config_ptr;
} CdiCoreConfigData;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SDK.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param core_config_ptr Pointer to core configuration data that is used to initialize the SDK. Value is required and
 *                        cannot be NULL. Copies of the data in the configuration data structure are made as needed. A
 *                        local variable can be used for composing the structure since its contents are not needed after
 *                        this function returns.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreInitialize(const CdiCoreConfigData* core_config_ptr);

/**
 * Create an instance of a network adapter. When done, must call CdiCoreShutdown().
 *
 * NOTE: Currently if the shared memory provider is enabled in the EFA Adapter (see FI_EFA_ENABLE_SHM_TRANSFER in
 * CDI), libfabric uses fork() to determine capability (see rxr_check_cma_capability() in the EFA provider). This
 * causes a double flush of cached write data of any open files. By default, the shared memory provider is disabled. If
 * enabled, all open files must have used fflush() prior to using this API function. The CDI SDK always flushes all
 * open log files that were created using the CDI logger API functions.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param adapter_data_ptr Pointer to network adapter configuration data. Copies of the data in the configuration data
 *                         structure are made as needed. A local variable can be used for composing the structure since
 *                         its contents are not needed after this function returns.
 * @param ret_handle_ptr Pointer to returned instance handle (used as parameter for other API functions).
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreNetworkAdapterInitialize(CdiAdapterData* adapter_data_ptr,
                                                              CdiAdapterHandle* ret_handle_ptr);

/**
 * Free the receive buffer that was used in one of the Cdi...RxCallback() API functions.
 *
 * @param sgl_ptr The scatter-gather list containing the memory to be freed.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreRxFreeBuffer(const CdiSgList* sgl_ptr);

/**
 * Gather received data represented by a scatter-gather list into a contiguous buffer. The caller is responsible for
 * ensuring that the destination buffer is large enough to hold the data.
 *
 * A subset of the entire payload of the SGL can be extracted by using a non-zero value for offset and/or a smaller
 * number for byte_count than sgl->total_data_size. The number of bytes copied is the smaller of (sgl->total_data_size -
 * offset) or byte_count.
 *
 * @param sgl_ptr The scatter-gather list containing the data to be gathered.CdiReservePayloadMemory
 * @param offset Number of bytes to skip in SGL before starting the copy.
 * @param dest_data_ptr Where to write the gathered data in linear format. Address must be withing the memory region
 *                      specified for mem_handle.
 * @param byte_count The number of bytes to copy.
 *
 * @return The number of bytes copied. This will be less than byte_count if fewer than that number of bytes are present
 *         in the source SGL starting from the specified offset. A value of -1 indicates that a fatal error was
 *         encountered such as NULL values for sgl_ptr or dest_data_ptr.
 */
CDI_INTERFACE int CdiCoreGather(const CdiSgList* sgl_ptr, int offset, void* dest_data_ptr, int byte_count);

/**
 * Configure transfer statistics.
 *
 * @param handle Connection handle returned by Cdi...TxCreate() API functions.
 * @param config_ptr Pointer to statistics configuration data. Copies of the data in the configuration data structure
 *                   are made as needed. A local variable can be used for composing the structure since its contents are
 *                   not needed after this function returns.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreStatsReconfigure(CdiConnectionHandle handle, const CdiStatsConfigData* config_ptr);

/**
 * Destroy a specific TX or RX connection and free resources that were created for it.
 *
 * @param handle Connection handle returned by Cdi...TxCreate() API functions.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreConnectionDestroy(CdiConnectionHandle handle);

/**
 * Application is shutting down. Stop open connections and free-up all resources being used by the SDK.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiCoreShutdown(void);

/**
 * Get the current synced AWS network UTC time in a timespec structure. This function will be kept up to date with the
 * best practices for getting high accuracy time from Amazon Time Sync Service as improved accuracy time is available.
 * All EC2 instances that call this function should be using the Amazon Time Sync Service. Amazon Time Sync Service
 * setup directions for Linux can be found at:
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/set-time.html#configure-amazon-time-service. For Windows follow
 * the directions at: https://docs.aws.amazon.com/AWSEC2/latest/WindowsGuide/windows-set-time.html
 *
 * @param ret_time_ptr Address where to write the UTC timestamp in a timespec structure as defined by time.h.
 */
void CdiCoreGetUtcTime(struct timespec* ret_time_ptr);

/**
 * @brief Get the current TAI time as a PTP timestamp.
 *
 * There seems to be no trivial solutions for automatically getting the current leap second adjustment. All leap second
 * adjustments occur on either December 31st or June 30th. The next possible leap second introduction is June 30th 2021.
 * This function applies a fixed 37 second adjustment to UTC time to get TAI time.
 *
 * @param ret_ptp_time_ptr Pointer to CdiPtpTimestamp to return the current PtpTimestamp to. This may be NULL.
 * @return The same value that is written to the location pointed to by ret_ptp_time_ptr.
 */
CdiPtpTimestamp CdiCoreGetPtpTimestamp(CdiPtpTimestamp* ret_ptp_time_ptr);

/**
 * Get the current synced AWS network UTC time in microseconds. It uses CdiCoreGetUtcTime().
 *
 * @return Current UTC time in microseconds.
 */
uint64_t CdiCoreGetUtcTimeMicroseconds(void);

/**
 * Get the current TAI time in microseconds.
 *
 * @return Current TAI time in microseconds.
 */
uint64_t CdiCoreGetTaiTimeMicroseconds();

/**
 * Returns a short string that briefly describes the meaning of an CDI status code.
 *
 * @param status The value to be converted to a string.
 *
 * @return const char* A character array which describes the requested code.
 */
const char* CdiCoreStatusToString(CdiReturnStatus status);

#ifdef __cplusplus
}
#endif

#endif // CDI_CORE_API_H__
