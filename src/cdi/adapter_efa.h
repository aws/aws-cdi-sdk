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
#include "rdma/fi_cm.h"
#include "rdma/fi_endpoint.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief V-table interface to the libfabric API. This allows multiple versions of libfabric to be used within this SDK.
 *
 */
typedef struct {
    uint32_t version_major; ///< Statically compiled libfabric major version number.
    uint32_t version_minor; ///< Statically compiled libfabric minor version number.
    uint32_t (*fi_version)(void); ///< Pointer to function.
    struct fi_info* (*fi_allocinfo)(void); ///< Pointer to function.
    int (*fi_av_insert)(struct fid_av *av, const void *addr, size_t count,
	                    fi_addr_t *fi_addr, uint64_t flags, void *context); ///< Pointer to function.

    int (*fi_av_open)(struct fid_domain *domain, struct fi_av_attr *attr,
	                  struct fid_av **av, void *context); ///< Pointer to function.
    int (*fi_av_remove)(struct fid_av *av, fi_addr_t *fi_addr, size_t count, uint64_t flags); ///< Pointer to function.
    int (*fi_close)(struct fid *fid); ///< Pointer to function.
    int (*fi_cq_open)(struct fid_domain *domain, struct fi_cq_attr *attr,
	                  struct fid_cq **cq, void *context); ///< Pointer to function.
    ssize_t (*fi_cq_read)(struct fid_cq *cq, void *buf, size_t count); ///< Pointer to function.
    ssize_t (*fi_cq_readerr)(struct fid_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags); ///< Pointer to function.
    int (*fi_domain)(struct fid_fabric *fabric, struct fi_info *info,
	                 struct fid_domain **domain, void *context); ///< Pointer to function.
    int (*fi_enable)(struct fid_ep *ep); ///< Pointer to function.
    int (*fi_endpoint)(struct fid_domain *domain, struct fi_info *info,
	                   struct fid_ep **ep, void *context); ///< Pointer to function.
    int (*fi_ep_bind)(struct fid_ep *ep, struct fid *bfid, uint64_t flags); ///< Pointer to function.
    int (*fi_fabric)(struct fi_fabric_attr *attr, struct fid_fabric **fabric, void *context); ///< Pointer to function.
    void (*fi_freeinfo)(struct fi_info *info); ///< Pointer to function.
    int (*fi_getinfo)(uint32_t version, const char *node, const char *service,
	                  uint64_t flags, const struct fi_info *hints,
	                  struct fi_info **info); ///< Pointer to function.
    int (*fi_getname)(fid_t fid, void *addr, size_t *addrlen); ///< Pointer to function.
    int (*fi_mr_reg)(struct fid_domain *domain, const void *buf, size_t len,
	                 uint64_t access, uint64_t offset, uint64_t requested_key,
	                 uint64_t flags, struct fid_mr **mr, void *context); ///< Pointer to function.
    void* (*fi_mr_desc)(struct fid_mr *mr); ///< Pointer to function.
    ssize_t (*fi_recvmsg)(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags); ///< Pointer to function.
    ssize_t (*fi_sendmsg)(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags); ///< Pointer to function.
    const char* (*fi_strerror)(int errnum); ///< Pointer to function.
} LibfabricApi;

/**
 * @brief This defines a structure that contains all of the state information that is specific to the Tx side of a
 * single EFA endpoint.
 */
typedef struct {
    CdiSignalType tx_start_signal;           ///< Signal used to wakeup the thread to do work.
    struct fid_mr* tx_user_payload_memory_region_ptr; ///< Pointer to Tx user payload data memory region.
    struct fid_mr* tx_internal_memory_region_ptr;  ///< Pointer to Tx internal packet header data memory region.
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
    volatile bool fabric_initialized;         ///< True of libfabric has been initialized

    /// @brief Key used for memory registration. Must be unique for each fi_mr_reg(). Only used if FI_MR_PROV_KEY for the
    /// domain is not enabled. Currently, this value is only used by the socket provider.
    uint64_t mr_key;

    uint8_t local_ipv6_gid_array[MAX_IPV6_GID_LENGTH]; ///< Pointer to local device GID for this endpoint.
    uint8_t remote_ipv6_gid_array[MAX_IPV6_GID_LENGTH]; ///< Pointer to remote device GID related to this endpoint.
    int dest_control_port;                    ///< Destination control port. For socket-based we use the next higher
                                              /// port number for the data port.
    LibfabricApi* libfabric_api_next_ptr;     ///< Pointer to next version of libfabric API V-table to use.
    LibfabricApi* libfabric_api_ptr;          ///< Pointer to current libfabric API V-table.
} EfaEndpointState;

/**
 * @brief Structure used to hold EFA connection state data.
 */
typedef struct {
    AdapterConnectionState* adapter_con_ptr; ///< Pointer to adapter connection data.
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
 *
 * @param endpoint_ptr Pointer to EFA endpoint state data.
 */
void EfaRxPacketPoolFree(EfaEndpointState* endpoint_ptr);

/**
 * @brief Set the protocol version for the specified endpoint. The protocol version actually used is negotiated using
 * the specified remote version and the current version of the CDI-SDK.
 *
 * @param endpoint_ptr Pointer to endpoint.
 * @param remote_version_ptr Pointer to remote protocol version data.
 *
 * @return True if successful, otherwise false is returned.
 */
bool EfaAdapterEndpointProtocolVersionSet(EfaEndpointState* endpoint_ptr, const CdiProtocolVersionNumber* remote_version_ptr);

#endif // ADAPTER_EFA_H__
