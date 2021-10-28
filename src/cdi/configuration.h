// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions used to define the build configuration of the CDI SDK's implementation.
 */

#ifndef CDI_CONFIGURATION_H__
#define CDI_CONFIGURATION_H__

//*********************************************************************************************************************
//***************************************** IMPLEMENTATION VARIANTS AND OPTION ****************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//********************************************* FEATURES TO AID DEBUGGING *********************************************
//*********************************************************************************************************************

/// @brief Enable to debug packet sequences. NOTE: This generates a lot of debug output.
//#define DEBUG_PACKET_SEQUENCES

/// @brief Enable to debug poll thread sleep time. NOTE: This generates a lot of debug output.
//#define DEBUG_POLL_THREAD_SLEEP_TIME

/// @brief Enable to debug Tx packet SGL entry pool free item count.
//#define DEBUG_TX_PACKET_SGL_ENTRY_POOL_FREE_COUNT

/// @brief This is an example of how to use the queue debug function. It enables queue debugging of the
/// tx_packet_queue_handle in adapter.c. NOTE: This feature is currently only available in a DEBUG build.
//#define DEBUG_ENABLE_QUEUE_DEBUGGING

/// @brief Enable additional debug logging of the control_work_request_pool_handle pool used in adapter_efa_probe.c.
/// NOTE: This feature is currently only available in a DEBUG build.
//#define DEBUG_ENABLE_POOL_DEBUGGING_EFA_PROBE

/// @brief Options to enable debug output for logic defined in rx_reorder.c.
//#define DEBUG_RX_REORDER_ALL
//#define DEBUG_RX_REORDER_MIN
//#define DEBUG_RX_REORDER_ERROR

/// @brief Option to enable compiling and debug output for logic defined in t_digest.c.
//#define DEBUG_T_DIGEST_UNIT_TEST
//#define DEBUG_T_DIGEST_LOGGING
//#define DEBUG_T_DIGEST_ARRAYS

/// @brief Dump Tx SGL and related SGL entries for each packet.
//#define DEBUG_TX_PACKET_SGL_ENTRIES

/// @brief Dump Rx raw SGL entries as part of RxPollFreeBuffer().
//#define DEBUG_RX_DUMP_RAW_SGL_ENTRIES

/// @brief Log Rx payload SGL entry free counts used in RxPollFreeBuffer().
//#define DEBUG_RX_PAYLOAD_SGL_ENTRY_FREE_COUNT

/// @brief After a connection has been established, disable all connection probe monitoring. This allows breakpoints and
/// other delays to occur without causing the probe to reset the connection.
//#define DISABLE_PROBE_MONITORING

/// @brief Enable the define below to set the libfabric log level. Default is FI_LOG_WARN.
//#define LIBFABRIC_LOG_LEVEL         (FI_LOG_DEBUG)

/// @brief Log messages to aid in debugging Rx Buffer feature.
//#define DEBUG_RX_BUFFER

//*********************************************************************************************************************
//******************************************* MAX SIZES FOR STATIC DATA/ARRAYS ****************************************
//*********************************************************************************************************************

/// @brief Multiplication factor used to increase buffer sizes from HD to 4K payloads.
#define HD_TO_4K_FACTOR     (4)

#define NO_GROW_COUNT (0) ///< @brief This is used for pools that will not grow when they become empty.
#define NO_GROW_SIZE (0)  ///< @brief This is used for pools that will not grow when they become empty.

/// @brief Maximum number of out of order packets that can be received.
#define MAX_RX_OUT_OF_ORDER                 (128)
/// @brief Maximum number out of order packets buffer can be increased by.
#define MAX_RX_OUT_OF_ORDER_GROW            (8)

/// @brief Maximum length of error string message.
#define MAX_ERROR_STRING_LENGTH             (1024)

/// @brief Maximum IP string length.
#define MAX_IP_STRING_LENGTH                (64)

/// @brief Maximum EFA device GID length. Contains GID + QPN (see efa_ep_addr).
#define MAX_IPV6_GID_LENGTH                 (32)

/// @brief Maximum IPV6 address string length.
#define MAX_IPV6_ADDRESS_STRING_LENGTH      (64)

/// @brief Maximum length of memory pool name that is stored internally in pool.c.
#define MAX_POOL_NAME_LENGTH                (64)

/// @brief Maximum length of the FIFO name that is stored internally in fifo.c.
#define MAX_FIFO_NAME_LENGTH                (64)

/// @brief Maximum number of payloads for a single connection.
#define MAX_PAYLOADS_PER_CONNECTION        (100)

/// @brief Initial number of work requests tx connection.
#define MAX_TX_PACKET_WORK_REQUESTS_PER_CONNECTION     (3000*HD_TO_4K_FACTOR)
/// @brief Number of work requests the tx connection may be increased by.
#define MAX_TX_PACKET_WORK_REQUESTS_PER_CONNECTION_GROW (500)

/// @brief Initial number of header entries in a tx payload.
#define TX_PACKET_HEADER_POOL_SIZE_PER_CONNECTION      (50*HD_TO_4K_FACTOR)

/// @brief Number of entries the tx header list may be increased by.
#define TX_PACKET_HEADER_POOL_SIZE_PER_CONNECTION_GROW (15)

/// @brief Initial number of SGL entries in a tx payload.
#define TX_PACKET_SGL_ENTRY_SIZE_PER_CONNECTION        (3000*HD_TO_4K_FACTOR)
/// @brief Number of entries the tx payload SGL list may be increased by.
#define TX_PACKET_SGL_ENTRY_SIZE_PER_CONNECTION_GROW   (500)

/// @brief Maximum number of transmit packets per payload. Additional objects are needed due to the asynchronous nature
/// of the API. Multiple payload transmissions may overlap.
#define MAX_TX_PACKETS_PER_CONNECTION                  (3000*HD_TO_4K_FACTOR)
/// @brief Number of entries the tx packet queue may be increased by.
#define TX_PACKET_POOL_SIZE_GROW                       (100)

/// @brief Maximum number of batches of transmit packets allowed to send to an endpoint. Transmit packets are sent in
/// ever increasingly sized batches so the number of batches is approximately log[base2](packets).
#define MAX_TX_PACKET_BATCHES_PER_CONNECTION           (12*HD_TO_4K_FACTOR)
/// @brief Number of entries the tx packet queue may be increased by.
#define TX_PACKET_SEND_QUEUE_SIZE_GROW                 (10)

/// @brief Maximum number of SGL entries for a single transmit packet.
#define MAX_TX_SGL_PACKET_ENTRIES                      (4)

/// @brief Maximum number of packets that can be simultaneously queued for transmission without receiving a
/// corresponding completion event (ACK or error).
#define SIMULTANEOUS_TX_PACKET_LIMIT                   (50)

/// @brief Maximum number of completion queue messages to process in a single Tx poll call.
#define MAX_TX_BULK_COMPLETION_QUEUE_MESSAGES          (SIMULTANEOUS_TX_PACKET_LIMIT)

/// @brief Maximum number of completion queue messages to process in a single Rx poll call.
#define MAX_RX_BULK_COMPLETION_QUEUE_MESSAGES          (50)

/// @brief Initial number of rx packets in a connection.
#define MAX_RX_PACKETS_PER_CONNECTION                  (3000*HD_TO_4K_FACTOR)
/// @brief Number of entries the rx packet connection list may be increased by.
#define MAX_RX_PACKETS_PER_CONNECTION_GROW             (500)

/// @brief Initial number of rx sockets.
#define RX_SOCKET_BUFFER_SIZE                          (1000)
/// @brief Number of entries the rx socket list may be increased by.
#define RX_SOCKET_BUFFER_SIZE_GROW                     (100)

/// @brief Size of the endpoint command queue used by the Endpoint Manager.
#define MAX_ENDPOINT_COMMAND_QUEUE_SIZE                (10)

/// @brief Maximum number of times a pool may grow in size before an error occurs.
#define MAX_POOL_GROW_COUNT                            (5)

/// @brief Maximum number of times a queue may grow in size before an error occurs.
#define MAX_QUEUE_GROW_COUNT                           (5)

/// @brief The space reserved for the libfabric message prefix in our packet header. This must be set to be
/// equal or larger than the largest prefix size needed by the EFA provider. It must be a multiple of 8.
/// See https://ofiwg.github.io/libfabric/v1.13.0/man/fi_msg.3.html#notes
#define MAX_MSG_PREFIX_SIZE                            (22 * 8)

//*********************************************************************************************************************
//********************************************* SETTINGS FOR EFA ADAPTER **********************************************
//*********************************************************************************************************************

/// @brief Timeout used when stopping an EFA endpoint. Value is in millisconds.
#define EFA_ENDPOINT_STOP_TIMEOUT_MSEC          (2000)

/// @brief Number of Tx packets to cache before notifying libfabric to ring the NIC's doorbell.
#define EFA_TX_PACKET_CACHE_SIZE                (16)

/// @brief Number of Rx buffer posts to cache before notifying libfabric to ring the NIC's doorbell.
#define EFA_RX_PACKET_BUFFER_CACHE_SIZE         (16)

/// @brief Number of read completion queue entries. Current libfabric default is 50.
#define EFA_CQ_READ_SIZE                        (50)

//*********************************************************************************************************************
//********************************************** SETTINGS FOR EFA PROBE ***********************************************
//*********************************************************************************************************************

/// @brief Number of probe Rx packet buffers to reserve per connection.
#define PROBE_RX_PACKET_BUFFERS_PER_CONNECTION  (100)

/// @brief Maximum number of control interface commands per connection.
#define MAX_PROBE_CONTROL_COMMANDS_PER_CONNECTION (20)

/// @brief Size of control interface transfer buffer size in bytes.
#define CONTROL_INTERFACE_TX_BUFFER_SIZE_BYTES  (4096)

/// @brief This value is used by the receiver to define how many times a reset command is sent without receiving any
/// responses before destroying the Rx endpoint.
#define RX_RESET_COMMAND_MAX_RETRIES             (3)

/// @brief Defines how often a reset command is sent to the remote target using the control interface. The value is in
/// milliseconds.
#define SEND_RESET_COMMAND_FREQUENCY_MSEC       (2000)

/// @brief Once a command has been sent to the Endpoint Manager, this defines how long to wait before it completes.
#define ENDPOINT_MANAGER_COMPLETION_TIMEOUT_MSEC (1000)

/// @brief Once a connection has been established, this defines how often the transmitter sends a ping to command to the
/// receiver using the control interface. The value is in milliseconds.
#define SEND_PING_COMMAND_FREQUENCY_MSEC        (3000)

/// @brief This value is used by the transmitter to define how many times a command is sent without receiving an ACK
/// reply before going into connection reset mode.
#define TX_COMMAND_MAX_RETRIES                  (3)

/// @brief This value is used by the transmitter to define how long it waits for an ACK response to a command that it
/// sent. If the timeout expires, the same command will be sent up to the amount specified by #TX_COMMAND_MAX_RETRIES.
/// Once the specified number of attempts has been exhausted, the transmitter will go into connection reset mode. The
/// value is in milliseconds.
#define TX_COMMAND_ACK_TIMEOUT_MSEC             (500)

/// @brief Defines how long the receiver waits for a ping command from the remote target before changing to connection
/// reset mode. The value is in milliseconds.
#define RX_PING_MONITOR_TIMEOUT_MSEC            (SEND_PING_COMMAND_FREQUENCY_MSEC+(TX_COMMAND_ACK_TIMEOUT_MSEC*(TX_COMMAND_MAX_RETRIES+1)))

/// @brief Defines the EFA interface probe packet data size.
#define EFA_PROBE_PACKET_DATA_SIZE              (1024)

/// @brief Defines the number of EFA interface probe packets that must be successfully transmitted before advancing to
/// the connected mode.
#define EFA_PROBE_PACKET_COUNT                  (1000)

/// @brief Defines how long the transmitter should wait for all the probe packet ACKs to be received after the
/// transmitter has received the kProbeCommandConnected command from the receiver.
#define EFA_TX_PROBE_ACK_TIMEOUT                (100)

/// @brief Defines how many times to retry EFA_TX_PROBE_ACK_TIMEOUT before going into connection reset mode.
#define EFA_TX_PROBE_ACK_MAX_RETRIES            (5)

/// @brief Defines how long to wait for the EFA interface probe to complete before changing to connection reset mode.
/// The value is in milliseconds.
#define EFA_PROBE_MONITOR_TIMEOUT_MSEC          (3000)

/// @brief The byte pattern used for the data portion of EFA probe packets.
#define EFA_PROBE_PACKET_DATA_PATTERN           (0x41)

/// @brief The default timeout value used by ProbeControlThread(). The value is in milliseconds.
#define DEFAULT_TIMEOUT_MSEC                    (1000)

/// The number of linear receive buffers allocated per connection opened with rx_buffer_type set to kCdiLinearBuffer.
/// The application program cannot hold on to more than this number of buffers before returning them through the
/// CdiCoreRxFreeBuffer() function.
#define RX_LINEAR_BUFFER_COUNT                  (5)

//*********************************************************************************************************************
//********************************************* SETTINGS FOR CLOUDWATCH ***********************************************
//*********************************************************************************************************************
#ifndef CDI_NO_MONITORING
/// When defined, publishing metrics to CloudWatch can be enabled through the API. This macro must also be defined to
/// use the metrics gathering service.
#define CLOUDWATCH_METRICS_ENABLED

/// @brief Default CloudWatch namespace to use.
#define CLOUDWATCH_DEFAULT_NAMESPACE_STRING    ("CloudDigitalInterface")

/// @brief Maximum string length used to represent strings specific to CloudWatch (ie. namespace, region and dimension
/// domain name).
#define MAX_CLOUDWATCH_STRING_LENGTH           (256)

/// @brief The maximum depth of the CloudWatch statistics FIFO.
#define CLOUDWATCH_STATS_FIFO_DEPTH            (1000)

//*********************************************************************************************************************
//***************************************** SETTINGS FOR METRICS GATHERING ********************************************
//*********************************************************************************************************************

/// This macro enables sending metrics to the AWS CDI metrics gathering service.
#define METRICS_GATHERING_SERVICE_ENABLED
#endif // CDI_NO_MONITORING

#endif // CDI_CONFIGURATION_H__
