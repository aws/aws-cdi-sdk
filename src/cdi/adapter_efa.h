// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter_efa.c.
 */

#ifndef ADAPTER_EFA_H__
#define ADAPTER_EFA_H___

// Enable this define so we can use libfabric header files. With _GNU_SOURCE enabled, the following defines will be set
// by the system include files:
//__USE_XOPEN2K, __USE_XOPEN2K8, __USE_MISC, __USE_GNU, __USE_SVID, __USE_XOPEN_EXTENDED, __USE_POSIX199309
#define _GNU_SOURCE

#include "adapter_api.h"
#include "adapter_control_interface.h"
#include "adapter_efa_probe.h"
#include "cdi_raw_api.h"
#include "private.h"

#include "rdma/fabric.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This defines a structure that contains all of the state information that is specific to the Tx side of a
 * single EFA endpoint.
 */
typedef struct {
    CdiSignalType tx_start_signal;           ///< Signal used to wakeup the thread to do work.
    struct fid_mr* memory_region_ptr;        ///< Pointer to Tx memory region
    uint16_t tx_packets_sent_since_flush;    ///< Number of Tx packets that have been sent since last flush.
    /// Number of Tx packets that are in process (sent but haven't received ACK/error response). This member must be
    /// only written in the context of PollThread.
    int tx_packets_in_process;
} EfaTxState;

/**
 * @brief This defines a structure that contains all of the state information that is specific to the Rx side of a
 * single EFA endpoint.
 */
typedef struct {
    /// @brief Memory pool for Rx packet SGL entries (CdiSglEntry). Not thread-safe.
    CdiPoolHandle packet_sgl_entries_pool_handle;
    void* allocated_buffer_ptr;             ///< Address of receive packets memory buffer; needed for freeing.
    int allocated_buffer_size;              ///< Total size of allocated packets buffer; needed for freeing.
    bool allocated_buffer_was_from_heap;    ///< True if no huge pages were available; needed for freeing.
    struct fid_mr* memory_region_ptr;       ///< Pointer to Rx memory region.
} EfaRxState;

/**
 * @brief Structure used to hold EFA endpoint state data.
 */
typedef struct {
    AdapterEndpointState* adapter_endpoint_ptr; ///< Pointer to adapter endpoint data (here for convenience).
    union {
        /// The internal state of the structure if adapter_endpoint_ptr->direction is kEndpointDirectionSend.
        EfaTxState tx_state;
        /// The internal state of the structure if adapter_endpoint_ptr->direction is kEndpointDirectionReceive.
        EfaRxState rx_state;
    };

    int tx_control_dest_port; ///< Transmitter control interface destination port.

    ProbeEndpointHandle probe_endpoint_handle; ///< Handle of probe for this endpoint.

    /// Data for completion events. Used by PollThread().
    struct fid_cq* completion_queue_ptr;      ///< Pointer to libfabric completion queue

    /// Pointer to libfabric structures used by the endpoint.
    struct fi_info* fabric_info_ptr;          ///< Pointer to description of a libfabric endpoint
    struct fid_fabric* fabric_ptr;            ///< Pointer to fabric provider
    struct fid_domain* domain_ptr;            ///< Pointer to fabric access domain
    struct fid_ep* endpoint_ptr;              ///< Pointer to fabric endpoint (transport level communication portal)
    struct fid_av* address_vector_ptr;        ///< Pointer to address vector map (high-level to fabric address map)
    fi_addr_t remote_fi_addr;                 ///< Remote memory address (we don't use so it is always FI_ADDR_UNSPEC)

    uint8_t local_ipv6_gid_array[MAX_IPV6_GID_LENGTH]; ///< Pointer to local device GID for this endpoint.
    uint8_t remote_ipv6_gid_array[MAX_IPV6_GID_LENGTH]; ///< Pointer to remote device GID related to this endpoint.
    int dest_control_port;                    ///< Destination control port. For socket-based we use the next higher
                                              /// port number for the data port.
} EfaEndpointState;

/**
 * @brief Structure used to hold EFA connection state data.
 */
typedef struct {
    AdapterConnectionState* adapter_con_ptr; ///< Pointer to adapter connection data.
    ControlInterfaceHandle control_interface_handle; ///< Handle of control interface for the connection.

    /// Memory pool of send control work requests (ProbeControlPacketWorkRequest).
    CdiPoolHandle control_work_request_pool_handle;
} EfaConnectionState;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create probe for the specified EFA endpoint.
 *
 * @param endpoint_ptr Pointer to the adapter endpoint to create a probe for.
 * @param ret_probe_handle Pointer to returned probe handle.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
CdiReturnStatus EfaAdapterProbeEndpointCreate(EfaEndpointState* endpoint_ptr,
                                              ProbeEndpointHandle* ret_probe_handle);

/**
 * Start the specified endpoint. This only allocates and starts low-level libfabric and EFA device driver resources.
 * Other resources have already been created and started when the connection was created.
 *
 * @param endpoint_ptr Pointer to the EFA endpoint to start.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
CdiReturnStatus EfaAdapterEndpointStart(EfaEndpointState* endpoint_ptr);

/**
 * Get handle of adapter control inteface related to the specified EFA endpoint.
 *
 * @param adapter_con_state_ptr Pointer to adapter connection state data.
 *
 * @return Handle of control interface adapter.
 */
CdiAdapterHandle EfaAdapterGetAdapterControlInterface(AdapterConnectionState* adapter_con_state_ptr);

// EFA Tx functions

/// @see CdiAdapterOpenEndpoint
CdiReturnStatus EfaTxEndpointOpen(EfaEndpointState* endpoint_ptr, const char* remote_address_str, int dest_port);

/// @see CdiAdapterPollEndpoint
CdiReturnStatus EfaTxEndpointPoll(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterResetEndpoint
CdiReturnStatus EfaTxEndpointReset(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterCloseEndpoint
CdiReturnStatus EfaTxEndpointClose(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterGetTransmitQueueLevel
EndpointTransmitQueueLevel EfaGetTransmitQueueLevel(const AdapterEndpointHandle handle);

/// @see CdiAdapterEnqueueSendPacket
CdiReturnStatus EfaTxEndpointSend(const AdapterEndpointHandle handle, const Packet* packet_ptr, bool flush_packets);

/// @see EfaAdapterEndpointStart
CdiReturnStatus EfaTxEndpointStart(EfaEndpointState* endpoint_ptr);

/// @see EfaAdapterEndpointStop
void EfaTxEndpointStop(EfaEndpointState* endpoint_ptr);

// EFA Rx functions

/// @see CdiAdapterOpenEndpoint
CdiReturnStatus EfaRxEndpointOpen(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterPollEndpoint
CdiReturnStatus EfaRxEndpointPoll(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterResetEndpoint
CdiReturnStatus EfaRxEndpointReset(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterCloseEndpoint
CdiReturnStatus EfaRxEndpointClose(EfaEndpointState* endpoint_ptr);

/// @see CdiAdapterFreeBuffer
CdiReturnStatus EfaRxEndpointRxBuffersFree(const AdapterEndpointHandle handle, const CdiSgList* sgl_ptr);

/**
 * Create pool of Rx packet buffers for the endpoint.
 *
 * @param endpoint_state_ptr Pointer to endpoint.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
CdiReturnStatus EfaRxPacketPoolCreate(EfaEndpointState* endpoint_state_ptr);

/**
 * Frees the previously allocated receive packet buffer pool for the endpoint.
 */
void EfaRxPacketPoolFree(EfaEndpointState* endpoint_ptr);

#endif // ADAPTER_EFA_H__
