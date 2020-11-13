// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions of types and the one global variable used internally by the SDK's
 * implementation. These are details that do not need to be exposed to the user programs via the API.
 */

#ifndef CDI_PRIVATE_H__
#define CDI_PRIVATE_H__

#include <stdbool.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_avm_api.h"
#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_pool_api.h"
#include "cdi_queue_api.h"
#include "cloudwatch_sdk_metrics.h"
#include "fifo_api.h"
#include "list_api.h"
#include "payload.h"
#include "singly_linked_list_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Forward reference of structure to create pointers later.
typedef struct EndpointManagerGlobalState* EndpointManagerGlobalHandle;

/// @brief Used to define a stream identifier that is not being used. Stream identifiers that are exposed through the
/// CDI API are 16-bit values. The values used internally are int types (32-bits), so we can identify an external
/// stream ID from this sentinel value.
#define STREAM_IDENTIFIER_NOT_USED      (int)(-1)

/**
 * @brief Structure to hold variables that would otherwise be global in order to keep them contained in one manageable
 * location. All members will be explicitly zeroed at program startup.
 */
typedef struct {
    bool sdk_initialized;                     ///< True if SDK has been initialized.
    CdiCsID adapter_handle_list_lock;         ///< Lock used to protect access to the adapter handle list.
    CdiList adapter_handle_list;              ///< List of network adapter CdiAdapterHandle objects.
    CdiLoggerHandle logger_handle;            ///< Handle to logger.
    CdiLogHandle global_log_handle;           ///< Handle to global log.
    CloudWatchSdkMetricsHandle cw_sdk_handle; ///< Handle of CloudWatch SDK metrics component.
    CloudWatchSdkMetricsHandle metrics_gathering_sdk_handle; ///< Handle of metrics gathering SDK metrics component.
    CdiSglEntry empty_sgl_entry;              ///< Empty scatter-gather-list entry.

    // NOTE: Add initialization to global_context variable's definition in internal.c for any new members added to this
    // structure.
} CdiGlobalContext;

/**
 * @brief The one global variable defined by the SDK containing all of its state information.
 */
extern CdiGlobalContext cdi_global_context;

/// Forward reference of structure to create pointers later.
typedef struct CdiAdapterState CdiAdapterState;
/// Forward reference of structure to create pointers later.
typedef struct AdapterConnectionState AdapterConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct AdapterEndpointState AdapterEndpointState;
/// Forward reference of structure to create pointers later.
typedef struct AdapterEndpointState* AdapterEndpointHandle;
/// Forward reference of structure to create pointers later.
typedef struct CdiConnectionState CdiConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct CdiEndpointState CdiEndpointState;
/// Forward reference of structure to create pointers later.
typedef struct EndpointManagerState EndpointManagerState;
/// Forward reference of structure to create pointers later.
typedef struct EndpointManagerState* EndpointManagerHandle;
/// Forward reference of structure to create pointers later.
typedef struct StatisticsState StatisticsState;
/// Forward reference of structure to create pointers later.
typedef struct StatisticsState* StatisticsHandle;
/// Forward reference of structure to create pointers later.
typedef struct StatisticsState* StatisticsHandle;
/// Forward reference of structure to create pointers later.
typedef struct CdiStatsCallbackState* CdiStatsCallbackHandle;

/**
 * @brief This enumeration is used in the CdiConnectionState and CdiEndpointState structures to indicate which of the
 * two state structures is contained in the union. Unusual numbers were chosen to decrease the likelihood of a pointer
 * to a random location in memory from being interpreted as a valid handle.
 */
typedef enum {
    kHandleTypeTx = 0x5a, ///< Transmitter type handle.
    kHandleTypeRx = 0xa5, ///< Receiver type handle.
} ConnectionHandleType;

/**
 * @brief This defines a structure that contains all of the data required to use the application registered payload
 * callback functions.
 */
typedef struct {
    CdiListEntry list_entry; ///< Allows this structure to be used as part of a list.

    CdiReturnStatus payload_status_code; ///< Status of the payload.

    CdiCoreExtraData core_extra_data;    ///< Core extra data

    uint16_t extra_data_size;            ///< Size of extra data.
    uint8_t extra_data_array[MAX_CDI_PACKET_EXTRA_DATA]; ///< Extra data sent along in the CDI header packet.

    CdiSgList payload_sgl;               ///< Scatter-Gather List for the payload.

    /// @brief For Tx connection, the payload user callback parameter that was provided in CdiCoreTxPayloadConfig.
    CdiUserCbParameter tx_payload_user_cb_param;

    /// @brief For Tx connection, payload state data that needs to be freed.
    TxPayloadState* tx_payload_state_ptr;

    /// @brief Pointer to error message string. It uses a pool, so must be freed after the user-registered callback
    /// function has been invoked.
    char* error_message_str;
} AppPayloadCallbackData;

/// Forward reference of structure to create pointers later.
typedef struct TxPayloadState TxPayloadState;
/**
 * @brief This defines a structure that contains all of the state information for sending a single payload.
 */
struct TxPayloadState {
    CdiSgList source_sgl;                       ///< Scatter-Gather List of payload entries to free.
    uint64_t start_time;                        ///< Time payload Tx started.
    uint32_t max_latency_microsecs;             ///< Maximum latency in microseconds of time to transfer the payload.
    /// @brief The size of the units (pixels, audio samples, etc.) in bytes making up the payload. This is to ensure
    /// units are not split between packets within a payload.
    int pattern_size_bytes;

    AppPayloadCallbackData app_payload_cb_data; ///< Used to hold data for application payload callback.

    CdiPayloadCDIPacketState payload_packet_state; ///< CDI packet state data.
    int data_bytes_transferred;                 ///< Number of application payload data bytes transferred.
    CdiSinglyLinkedList completed_packets_list; ///< List of packets for current payload that have been acknowledged.

    CdiEndpointHandle cdi_endpoint_handle;  ///< CDI endpoint to use to send this payload.
};

/**
 *  @brief Generic type used to pass Tx/Rx callbacks to internal functions.
 *
 *  @param Pointer to callback type specific data.
 */
typedef void (*CdiCallback)(const void* param_ptr);

/**
 * @brief This defines a structure that contains all of the state information for the sending side of a single flow.
 */
typedef struct {
    /// @brief Copy of the destination IP address string. The pointer in config_data (see below) points to this value.
    char copy_dest_ip_addr_str[MAX_IP_STRING_LENGTH];
    /// @brief Copy of the configuration data. Copies of strings are made and are then referenced in this structure.
    CdiTxConfigData config_data;
    CdiCallback cb_ptr;                         ///< Callback function address.

    CdiQueueHandle payload_queue_handle;        ///< Queue of TxPayloadState structures.

    /// @brief Memory pool for payload state (TxPayloadState).
    CdiPoolHandle payload_state_pool_handle;

    /// @brief Memory pool for payload SGL entries (CdiSglEntry). Not thread-safe.
    CdiPoolHandle payload_sgl_entry_pool_handle;

    /// @brief Memory pool for work requests (TxPacketWorkRequest). Not thread-safe.
    CdiPoolHandle work_request_pool_handle;

    /// @brief Memory pool for packet SGL entries (CdiSglEntry). Not thread-safe.
    CdiPoolHandle packet_sgl_entry_pool_handle;

    /// @brief Queue of completed work requests that need their resources freed (TxPacketWorkRequest*).
    CdiQueueHandle work_req_comp_queue_handle;

    CdiCsID payload_num_lock;                   ///< Lock used to protect incrementing the payload number.
    uint8_t payload_num;                        ///< Payload number.
} TxConState;

/// Forward reference of structure to create pointers later.
typedef struct CdiMemoryState CdiMemoryState;
/// Forward reference of structure to create pointers later.
typedef struct SdkLogState* SdkLogHandle;

/**
 * @brief This defines a structure that contains all of the state information for the receiving side of a payload's work
 * request. The lifespan of the data begins when packet #0 (CDI header) of a Rx CDI packet is received and ends when
 * the user-registered CdiRawRxCallback() Rx callback function is invoked.
 */
typedef struct {
    CdiPayloadType payload_type;              ///< Payload type from CDI packet #0 header (app or keep alive).
    CdiMemoryState* payload_memory_state_ptr; ///< Pointer to memory state of the Rx payload.

    AppPayloadCallbackData app_payload_cb_data; ///< Used to hold data for application payload callback.

    uint64_t start_time;            ///< Time payload Rx started.
    uint32_t max_latency_microsecs; ///< Maximum latency in microseconds of time to transmit the payload.
} RxPayloadWorkRequestState;

/// Forward reference of structure to create pointers later.
typedef struct CdiReorderList CdiReorderList;
/**
 * @brief Structure containing top, bottom pointers and sequence numbers of sgls that comprise payload.
 * when the entire payload is received, this will be one list.
 */
struct CdiReorderList {
    CdiReorderList* prev_ptr;  ///< Previous pointer to neighboring lists for this sgl.
    CdiReorderList* next_ptr;  ///< Next pointer to neighboring lists for this sgl.
    uint16_t top_sequence_num; ///< Sequence number of the packet sitting at the top of this sgl.
    uint16_t bot_sequence_num; ///< Sequence number of the packet sitting at the bottom of this sgl.
    CdiSgList sglist;          ///< Sgl list in this reorder list.
};

/**
 * @brief This defines a structure that contains all of the state information for the receiving side of a payload. The
 * data is only required internally by the RxPacketReceive() function and not used elsewhere.
 */
typedef struct {
    CdiPayloadState payload_state;    ///< Current processing state of this payload (ie. idle, in progress, etc).

    RxPayloadWorkRequestState work_request_state; ///< Rx work request state

    uint8_t payload_num;              ///< Payload number obtained from CDI packet #0 header.

    /// @brief Packet sequence number obtained from each CDI header, packet_sequence_num = 0xffff is illegal and
    /// represents the packet_sequence_num has not yet been updated.
    uint16_t packet_sequence_num;

    bool suspend_warnings;            ///< Use this flag to suspend packet warnings for a payload.
    int expected_payload_data_size;   ///< Expected total payload size in bytes obtained from CDI packet #0 header.
    int data_bytes_received;          ///< Number of payload bytes received.
    CdiReorderList* reorder_list_ptr; ///< Pointer to what will end up being the single SGL that comprises the payload
    uint8_t* linear_buffer_ptr;       ///< Address to be used if assembling into a linear buffer.
} RxPayloadState;

/**
 * @brief This defines a structure that contains all of the state information for an rx connection.
 * The data is only required internally by the RxPacketReceive() function and not used elsewhere.
 */
typedef struct {
    /// @brief Copy of the configuration data. Copies of strings are made and are then referenced in this structure.
    CdiRxConfigData config_data;
    CdiCallback cb_ptr;              ///< Callback function address.

    CdiPoolHandle payload_memory_state_pool_handle; ///< Memory pool for payload memory state entries (CdiMemoryState).
    CdiPoolHandle payload_sgl_entry_pool_handle; ///< Memory pool for payload SGL entries (CdiSglEntry).

    /// @brief Memory pool for payload SGL entries that arrive out of order (CdiReorderList).
    CdiPoolHandle reorder_entries_pool_handle;

    /// @brief Monotonic time when first payload arrived. This value is only used when Rx buffer_delay_ms is non-zero.
    /// Used to calculate time deltas for future payloads. NOTE: The value may be reset when a payload arrives that is
    /// not within the expected range.
    uint64_t payload_base_monotonic_time_us;

    /// @brief UTC time when first payload arrived. This value is only used when Rx buffer_delay_ms is non-zero. Used to
    /// calculate time deltas for future payloads. NOTE: The value may be reset when a payload arrives that is not
    /// within the expected range.
    uint64_t payload_base_utc_us;

    /// @brief Timestamp of first payload. This value is only used when the Rx buffer_delay_ms is non-zero. Used to
    /// calculate timestamp deltas for future payloads. NOTE: The value may be reset when a payload arrives that is not
    /// within the expected range.
    uint64_t payload_base_timestamp_us;

    /// @brief Pool used to hold payload state data (AppPayloadCallbackData) that is stored in ptp_ordered_payload_list
    /// ordered by PTP. NOTE: This pool is only used if Rx payload buffering is enabled for the connection (@see
    /// CdiRxConfigData.buffer_delay_ms).
    CdiPoolHandle ordered_payload_pool_handle;

    /// @brief Pool used to hold state data while receiving payloads.
    CdiPoolHandle rx_payload_state_pool_handle;

    /// @brief List of AppPayloadCallbackData structures order by PTP (lowest PTP value first). NOTE: This list is only
    /// used if Rx payload buffering is enabled for the connection (@see CdiRxConfigData.buffer_delay_ms).
    CdiList ptp_ordered_payload_list;
} RxConState;

/**
 * @brief This defines a structure that contains the state information for the sending side's endpoint of a single flow.
 */
typedef struct {
    CdiCsID payload_num_lock;                   ///< Lock used to protect incrementing the payload number.
    uint8_t payload_num;                        ///< Payload number.
} TxEndpointState;

/**
 * @brief This defines a structure that contains the state information for an Rx endpoint. The data is only required
 * internally by the RxPacketReceive() API and not used elsewhere.
 */
typedef struct {
    uint8_t payload_index;               ///< The index of the next expected payload's entry in payload_state.
    /// @brief The number of the expected payload number for the beginning of the payload buffer window.
    /// Sized to match payload_num from CDI header packet #0.
    uint8_t payload_num_window_min;

    CdiQueueHandle free_buffer_queue_handle; ///< Circular queue of CdiSgList structures.

    /// @brief Current state of the payload number being processed. Array is addressed by payload_num, which is a
    /// uint8_t.
    RxPayloadState* payload_state_array_ptr[1<<(sizeof(uint8_t)<<3)];
} RxEndpointState;

/**
 * @brief Structure definition behind the connection handles shared with the user's application program. Its contents
 * are opaque to the user's program where it only has a pointer to a declared but not defined structure.
 */
struct CdiEndpointState {
    /// The instance of the connection this Tx/Rx object is associated with.
    CdiConnectionState* connection_state_ptr;

    /// The instance of the adapter endpoint object underlying this endpoint.
    AdapterEndpointState* adapter_endpoint_ptr;

    /// @brief Used to identify the source data stream that should be routed to this endpoint. This allows multiple
    /// streams to be carried on a single connection and uniquely routed to different endpoints. If the value is
    /// STREAM_IDENTIFIER_NOT_USED, then all stream data is routed to this endpoint. For send type endpoints, the value
    /// is set when the endpoint is created. For receive type endpoints, the value is provided via the control inteface
    /// from the remote transmitter's endpoint as part of the endpoint probe process.
    int stream_identifier;

    /// @brief Name of the stream. It is used as an identifier when generating log messages, connection callbacks and
    /// statistics data.
    char stream_name_str[MAX_STREAM_NAME_STRING_LENGTH];

    union {
        /// The internal state of the structure if ConnectionHandleType is kHandleTypeTx.
        TxEndpointState tx_state;
        /// The internal state of the structure if ConnectionHandleType is kHandleTypeRx.
        RxEndpointState rx_state;
    };

    /// The accumulated statistics for this endpoint.
    CdiTransferStats transfer_stats;
};

/**
 * @brief Structure definition behind the connection handles shared with the user's application program. Its contents
 * are opaque to the user's program where it only has a pointer to a declared but not defined structure.
 */
struct CdiConnectionState {
    /// Used to store an instance of this object in a list using this element as the list item.
    CdiListEntry list_entry;
    /// Set to kMagicCon when allocated, checked at every API function to help ensure validity.
    uint32_t magic;

    /// Signal used to start connection threads. A separate signal is used for the adapter endpoints (see
    /// AdapterEndpointState.start_signal).
    CdiSignalType start_signal;
    CdiSignalType shutdown_signal; ///< Signal used to shutdown connection threads.

    /// @brief Handle of Endpoint Manager for this connection. Manages the list of endpoints associated with this
    /// connection.
    EndpointManagerHandle endpoint_manager_handle;

    /// The instance of the adapter this Tx/Rx object is associated with.
    CdiAdapterState* adapter_state_ptr;

    /// The instance of the adapter connection object underlying this connection.
    AdapterConnectionState* adapter_connection_ptr;

    /// The ID of the thread that services payload messages from the related adapter.
    CdiThreadID app_payload_message_thread_id;

    /// Queue of payload AppPayloadCallbackData structures.
    CdiQueueHandle app_payload_message_queue_handle;

    /// Pool of linear buffers in which to store incoming payloads if the connection was created with kCdiLinearBuffer.
    CdiPoolHandle linear_buffer_pool;

    /// Indicates which structure of the union is valid.
    ConnectionHandleType handle_type;
    union {
        /// The internal state of the structure if ConnectionHandleType is kHandleTypeTx.
        TxConState tx_state;
        /// The internal state of the structure if ConnectionHandleType is kHandleTypeRx.
        RxConState rx_state;
    };

    /// Pointer to statistics state data.
    StatisticsState* stats_state_ptr;

    /// Handle of currently user-registered statistics callback.
    CdiStatsCallbackHandle stats_cb_handle;

    /// Connection protocol type.
    ConnectionProtocolType protocol_type;

    /// Data for payload worker thread. Either used for TxPayloadThread().
    CdiThreadID payload_thread_id;             ///< Payload thread identifier

    char saved_connection_name_str[MAX_CONNECTION_NAME_STRING_LENGTH]; ///< Name of the connection.

    CdiLogHandle log_handle; ///< Logger handle used for this connection. If NULL, the global logger is used.

    CdiPoolHandle error_message_pool; ///< Pool used to hold error message strings.
};

/// Somewhat random number to aid in detecting invalid TX/RX handles.
static const uint32_t kMagicCon = 0x8ab73d45;

/// Somewhat random number to aid in detecting invalid memory handles.
static const uint32_t kMagicMem = 0x9bc84e56;

/**
 * @brief This defines a structure used to contain all of the state information for a linear buffer.
 */
typedef struct {
    void* virtual_address;      ///< Pointer to structure to free buffer(s).
    uint64_t physical_address;  ///< Physical address.
    uint32_t byte_size;         ///< Size of buffer in bytes.
} MemoryLinearState;

/**
 * @brief Structure definition behind the Scatter-Gather List internal data handles shared with the user's application
 * program. Its contents are opaque to the user's program where it only has a pointer to a declared but not defined
 * structure.
 */
struct CdiMemoryState {
    /// Set to kMagicMem when allocated, checked at every API function to help ensure validity.
    uint32_t magic;

    CdiEndpointHandle cdi_endpoint_handle; ///< Which endpoint this belongs to.

    CdiBufferType buffer_type;         ///< Indicates which structure of the union is valid.
    MemoryLinearState linear_state;    ///< The internal state of the structure if handleType is HandleTypeLinear.

    RxPayloadState* payload_state_ptr; ///< Received payload state pointer, used to return memory allocation to pool.

    CdiSgList endpoint_packet_buffer_sgl; ///< The SGL and entries to be returned to the endpoint's free lists.
};

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#endif  // CDI_PRIVATE_H__
