// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for the EFA adapter.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h"

#include <inttypes.h> // for PRIu16
#include <netdb.h> // For gethostbyname
#include <arpa/inet.h> // For inet_ntoa
#ifdef _LINUX
#include <dlfcn.h>
#endif

#include "adapter_efa_probe_control.h"
#include "internal.h"
#include "internal_log.h"
#include "internal_tx.h"
#include "internal_utility.h"
#include "libfabric_api.h"
#include "private.h"
#include "cdi_os_api.h"


//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Calculate the maximum TCP payload size by starting with the jumbo frame size supported by AWS networks and
/// subtracting space for the IP and TCP headers. The space for the Ethernet headers has already been subtracted since
/// the true jumbo frame size supported is 9023. Reference:
/// https://aws.amazon.com/about-aws/whats-new/2018/10/aws-direct-connect-now-supports-jumbo-frames-for-amazon-virtual-private-cloud-traffic/
/// https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/network_mtu.html
#define MAX_TCP_PACKET_SIZE (9001 - 20 - 20)

/// @brief Maximum string length of an integer value.
#define MAX_INT_STRING_LENGTH   (32)

/// @brief Variable for testing parts of the code dealing with libfabric's message prefix mode. The default should be
/// zero. But even with prefix mode turned off and the default set to non-zero, all tests should pass. This may help in
/// determining whether an issue with message prefix mode is likely caused by a bug in the SDK or a bug in libfabric.
#define DEFAULT_MSG_PREFIX_SIZE (0)

/// Produce a log message when a libfabric call unexpectedly failed.
#define CHECK_LIBFABRIC_RC(function, ret) \
    do { \
        if (0 < ret) { \
            SDK_LOG_GLOBAL(kLogError, #function " returned [%d] unexpectedly.", ret); \
        } else if (0 > ret) { \
            SDK_LOG_GLOBAL(kLogError, #function " returned [%d,%s] unexpectedly.", ret, \
                           endpoint_ptr->libfabric_api_ptr->fi_strerror(-ret)); \
        } \
        if (0 != ret) { \
            rs = kCdiStatusFatal; \
        } \
    } while (0)

/// @brief The define below is from libfabric_new/prov/psm3/inc/rdma/fi_ext.h
#define FI_OPT_EFA_RNR_RETRY   -(0xefa << 16)

/// Forward declaration of function.
static CdiReturnStatus EfaConnectionCreate(AdapterConnectionHandle handle, int port_number,
                                           const char* bind_ip_addr_str);
/// Forward declaration of function.
static CdiReturnStatus EfaConnectionDestroy(AdapterConnectionHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointOpen(AdapterEndpointHandle endpoint_handle, const char* remote_address_str,
                                       int port_number, const char* bind_ip_addr_str);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointPoll(AdapterEndpointHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointReset(AdapterEndpointHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointStart(AdapterEndpointHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointClose(AdapterEndpointHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaAdapterShutdown(CdiAdapterHandle adapter);

/**
 * @brief Structure used to hold EFA adapter state data.
 */
typedef struct {
    bool is_socket_based;  ///< true for socket-based and false for EFA-based.
    CdiAdapterHandle control_interface_adapter_handle;  ///< Handle of adapter used by control interface.
    LibfabricApi* libfabric_api_1_9_ptr; ///< Pointer to libfabric API 1.9 V-table.
    LibfabricApi* libfabric_api_new_ptr; ///< Pointer to libfabric API new V-table.

    /// @brief Lock used to protect access to libfabric for endpoint open/close.
    CdiCsID libfabric_lock;
} EfaAdapterState;

/// @brief Forward declaration
static CdiReturnStatus LibFabricEndpointClose(EfaEndpointState* endpoint_ptr);

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/**
 * @brief Define the virtual table API interface for this adapter.
 */
static struct AdapterVirtualFunctionPtrTable efa_endpoint_functions = {
    .CreateConnection = EfaConnectionCreate,
    .DestroyConnection = EfaConnectionDestroy,
    .Open = EfaEndpointOpen,
    .Close = EfaEndpointClose,
    .Poll = EfaEndpointPoll,
    .GetTransmitQueueLevel = EfaGetTransmitQueueLevel,
    .Send = EfaTxEndpointSend,
    .RxBuffersFree = EfaRxEndpointRxBuffersFree,
    .GetPort = NULL, // Not implemented
    .Reset = EfaEndpointReset,
    .Start = EfaEndpointStart,
    .Shutdown = EfaAdapterShutdown,
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Determine maximum payload size that fits in a single link-level packet. Set both the adapter's maximum_payload_bytes
 * and msg_prefix_size.
 *
 * @param fi_ptr Pointer to name of variable to set.
 * @param endpoint_ptr Pointer to the EFA endpoint state to be configured.
 */
static CdiReturnStatus SetMaximumEfaPayloadSize(struct fi_info* fi_ptr, EfaEndpointState* endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Documentation for fi_getinfo says we should compare the integer values for portability.
    int major = endpoint_ptr->libfabric_api_ptr->version_major;
    int minor = endpoint_ptr->libfabric_api_ptr->version_minor;

    // Get MTU size from adapter.
    size_t mtu = fi_ptr->nic->link_attr->mtu;
    int maximum_payload_size = 0;

    // In version 1.9 and prior, the EFA provider reported link-level MTU. In later versions it accounts for
    // provider-specific headers and reports the maximum payload size.
    if ((major == 1 && minor >= 10) || major > 1) {
        maximum_payload_size = mtu;
    } else {
        // See logic in rxr_get_rts_data_size().
        // (MTU - 64 bytes for SRD headers)
        maximum_payload_size = mtu - 64;
    }

    // In prefix mode msg_prefix_size > 0 and we must provide buffer space for the EFA provider.
    int msg_prefix_size = CDI_MAX(DEFAULT_MSG_PREFIX_SIZE, (int)fi_ptr->ep_attr->msg_prefix_size);
    if (MAX_MSG_PREFIX_SIZE < msg_prefix_size) {
        SDK_LOG_GLOBAL(kLogFatal, "Libfabric requires a message prefix size larger than supported by the SDK.");
        SDK_LOG_GLOBAL(kLogFatal, "MAX_MSG_PREFIX_SIZE must be at least [%d] (currently [%d]).", msg_prefix_size,
                       MAX_MSG_PREFIX_SIZE);
        rs = kCdiStatusFatal;
    } else {
        maximum_payload_size -= msg_prefix_size;
        SDK_LOG_GLOBAL(kLogInfo, "EFA adapter MTU [%zu], maximum payload size [%d], message prefix size [%zu]",
            mtu, maximum_payload_size, msg_prefix_size);
    }

    // msg_prefix_size must be a nonnegative multiple of 8
    assert(endpoint_ptr->adapter_endpoint_ptr->msg_prefix_size >= 0);
    assert((endpoint_ptr->adapter_endpoint_ptr->msg_prefix_size & 7) == 0);
    assert(maximum_payload_size > 0);
    assert(fi_ptr->tx_attr->iov_limit > 0);

    endpoint_ptr->adapter_endpoint_ptr->maximum_payload_bytes = maximum_payload_size;
    endpoint_ptr->adapter_endpoint_ptr->msg_prefix_size = msg_prefix_size;
    // Get Tx IOV Limit (maximum number of SGL entries for a payload).
    endpoint_ptr->adapter_endpoint_ptr->maximum_tx_sgl_entries = fi_ptr->tx_attr->iov_limit;

    return rs;
}

/**
 * Allocate memory for a libfabric hints structure, initialize it for the EFA adapter and return a pointer to the next
 * structure.
 *
 * @param libfabric_api_ptr Pointer to libfabric V-table API.
 * @param is_socket_based Specifies whether the adapter is socket-based (true) or EFA-based (false).
 *
 * @return Pointer to new hints structure. Returns NULL if unable to allocate memory.
 */
static struct fi_info* CreateHints(LibfabricApi* libfabric_api_ptr, bool is_socket_based)
{
    char* provider_name = NULL;
    if (is_socket_based) {
        provider_name = "sockets";
    } else {
        provider_name = "efa";
    }

    struct fi_info* hints_ptr = libfabric_api_ptr->fi_allocinfo();

    if (hints_ptr) {
        hints_ptr->fabric_attr->prov_name = provider_name;
        hints_ptr->ep_attr->type = FI_EP_RDM;
        hints_ptr->domain_attr->resource_mgmt = FI_RM_ENABLED;
        hints_ptr->caps = FI_MSG;
        hints_ptr->mode = FI_CONTEXT;
        // If Libfabric version is > 1.9, then enable zero-copy by enabling message prefix mode.
        if (!is_socket_based && (libfabric_api_ptr->version_major > 1 ||
            (libfabric_api_ptr->version_major == 1 && libfabric_api_ptr->version_minor > 9))) {
            hints_ptr->mode = FI_MSG_PREFIX;
        }
        hints_ptr->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_VIRT_ADDR;
        if (!is_socket_based) {
            // Socket provider does not generate keys, and will use zero for all key values. This will cause a duplicate
            // key error when trying to use fi_mr_reg() more than once. For other providers (ie. EFA) enable key
            // generation.
            hints_ptr->domain_attr->mr_mode |= FI_MR_PROV_KEY;
        }

        // Not using FI_THREAD_SAFE, to prevent use of locks. NOTE: This means that single-thread access to libfabric
        // must be used.
        hints_ptr->domain_attr->threading = FI_THREAD_DOMAIN;

        hints_ptr->tx_attr->comp_order = FI_ORDER_NONE;
        hints_ptr->rx_attr->comp_order = FI_ORDER_NONE;
    }

    return hints_ptr;
}

/**
 * @brief Set libfabric API version for an endpoint.
 *
 * @param endpoint_ptr Pointer to EFA endpoint to set version of libfabric.
 * @param libfabric_api_ptr Pointer to libfabric API version to set.
 */
static void LibfabricVersionSet(EfaEndpointState* endpoint_ptr, LibfabricApi* libfabric_api_ptr)
{
    endpoint_ptr->libfabric_api_ptr = libfabric_api_ptr;
    EfaAdapterState* efa_adapter_state_ptr =
        (EfaAdapterState*)endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    SDK_LOG_GLOBAL(kLogInfo, "Set Libfabric version[%d.%d]", endpoint_ptr->libfabric_api_ptr->version_major,
                   endpoint_ptr->libfabric_api_ptr->version_minor);

    struct fi_info* hints_ptr = CreateHints(endpoint_ptr->libfabric_api_ptr, efa_adapter_state_ptr->is_socket_based);
    assert(hints_ptr); // Should never occur.

    if (efa_adapter_state_ptr->is_socket_based) {
        endpoint_ptr->adapter_endpoint_ptr->maximum_payload_bytes = MAX_TCP_PACKET_SIZE;
        endpoint_ptr->adapter_endpoint_ptr->maximum_tx_sgl_entries = MAX_TX_SGL_PACKET_ENTRIES;
    } else {
        uint64_t flags = 0;
        struct fi_info* fi_ptr;

        // Ensure that all log files are flushed before using fi_getinfo() below for the EFA adapter. See
        // comment above about fork().
        CdiLoggerFlushAllFileLogs();
        uint32_t version = endpoint_ptr->libfabric_api_ptr->fi_version();
        fi_ptr = NULL;
        int ret = endpoint_ptr->libfabric_api_ptr->fi_getinfo(version, NULL, NULL, flags, hints_ptr, &fi_ptr);
        if (0 != ret) {
            SDK_LOG_GLOBAL(kLogError, "fi_getinfo() failed for local EFA device. Ret[%d]", ret);
        } else {
            SetMaximumEfaPayloadSize(fi_ptr, endpoint_ptr);
            endpoint_ptr->libfabric_api_ptr->fi_freeinfo(fi_ptr);
            fi_ptr = NULL;
        }
    }

    if (hints_ptr) {
        hints_ptr->fabric_attr->prov_name = NULL; // Value is statically allocated, so don't want libfabric to free it.
        endpoint_ptr->libfabric_api_ptr->fi_freeinfo(hints_ptr);
        hints_ptr = NULL;
    }

    assert(endpoint_ptr->adapter_endpoint_ptr->maximum_payload_bytes > 0);
}

/**
 * Open a libfabric connection to the specified endpoint.
 *
 * @param endpoint_ptr Pointer to EFA endpoint to open.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus LibFabricEndpointOpen(EfaEndpointState* endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (endpoint_ptr->libfabric_api_ptr != endpoint_ptr->libfabric_api_next_ptr) {
        // Set current libfabric API version to use.
        LibfabricVersionSet(endpoint_ptr, endpoint_ptr->libfabric_api_next_ptr);
    }

    AdapterConnectionState* adapter_con_state_ptr = endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr;
    EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    // Make thread-safe to protect access to libfabric for endpoint open/close.
    CdiOsCritSectionReserve(efa_adapter_state_ptr->libfabric_lock);

    bool is_socket_based = efa_adapter_state_ptr->is_socket_based;
    bool is_transmitter = (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction);

    uint64_t flags = 0;

    // Start with the EFA defaults, then override if socket-based.
    const char* node_str = NULL;
    char* service_str = NULL;
    char port_str[16];
    if (is_socket_based) {
        service_str = port_str;
        static int data_port_offset = 1;
        const int data_port = data_port_offset++ + endpoint_ptr->dest_control_port;
        const int port_ret = snprintf(port_str, sizeof port_str, "%"PRIu16, data_port);
        if (port_ret < 0 || (port_ret >= (int)(sizeof port_str))) {
            return kCdiStatusFatal;
        }
        // To support multiple endpoints with the sockets provider we cycle through offsets
        // 1...CDI_MAX_ENDPOINTS_PER_CONNECTION from the control port.
        if (data_port_offset > CDI_MAX_ENDPOINTS_PER_CONNECTION) {
            data_port_offset = 1;
        }
    }

    if (is_transmitter) {
        // Transmitter.
        flags = 0;
        if (is_socket_based) {
            node_str = EndpointManagerEndpointRemoteIpGet(endpoint_ptr->adapter_endpoint_ptr->cdi_endpoint_handle);
        } else {
            node_str = NULL;
        }
    } else {
        // Receiver.
        flags = FI_SOURCE;
        if (is_socket_based) {
            // The libfabric socket provider no longer provides a local IP address prior to the socket connection being
            // established, so we must get it here using OS APIs.
            char host_name_str[256];
            if (0 != gethostname(host_name_str, sizeof(host_name_str))) {
                SDK_LOG_GLOBAL(kLogError, "Failed to get host name of socket interface.");
                return kCdiStatusFatal;
            }
            struct hostent* host_entry_ptr = gethostbyname(host_name_str);
            node_str = inet_ntoa(*((struct in_addr*)host_entry_ptr->h_addr_list[0]));
        } else {
            node_str = NULL;
        }
    }

    if (kCdiStatusOk == rs) {
        if (is_socket_based) {
            endpoint_ptr->adapter_endpoint_ptr->maximum_payload_bytes = MAX_TCP_PACKET_SIZE;
        }
        // NOTE: Configuration for EFA is done dynamically in EfaAdapterEndpointProtocolVersionSet().
    }

    struct fi_info* hints_ptr = CreateHints(endpoint_ptr->libfabric_api_ptr, is_socket_based);
    if (NULL == hints_ptr) {
        rs = kCdiStatusAllocationFailed;
    }

    if (kCdiStatusOk == rs) {
        uint32_t version = (endpoint_ptr->libfabric_api_ptr->fi_version)();
        int ret = endpoint_ptr->libfabric_api_ptr->fi_getinfo(version, node_str, service_str, flags, hints_ptr,
                    &endpoint_ptr->fabric_info_ptr);
        CHECK_LIBFABRIC_RC(fi_getinfo, ret);
    }

    if (kCdiStatusOk == rs && !is_socket_based) {
        // The SDK does not expect to receive packets in order. For best performance don't require packet ordering.
        endpoint_ptr->fabric_info_ptr->tx_attr->msg_order = FI_ORDER_NONE;
        endpoint_ptr->fabric_info_ptr->rx_attr->msg_order = FI_ORDER_NONE;
        endpoint_ptr->fabric_info_ptr->ep_attr->max_msg_size =
            endpoint_ptr->adapter_endpoint_ptr->maximum_payload_bytes;
    }

    if (kCdiStatusOk == rs) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_fabric(endpoint_ptr->fabric_info_ptr->fabric_attr,
                    &endpoint_ptr->fabric_ptr, NULL);
        CHECK_LIBFABRIC_RC(fi_fabric, ret);
    }

    if (kCdiStatusOk == rs) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_domain(endpoint_ptr->fabric_ptr, endpoint_ptr->fabric_info_ptr,
                    &endpoint_ptr->domain_ptr, NULL);
        CHECK_LIBFABRIC_RC(fi_domain, ret);
    }

    if (kCdiStatusOk == rs) {
        struct fi_cq_attr completion_queue_attr = {
            .wait_obj = FI_WAIT_NONE,
            .format = FI_CQ_FORMAT_DATA
        };

        if (is_transmitter) {
            // For transmitter.
            completion_queue_attr.size = endpoint_ptr->fabric_info_ptr->tx_attr->size;
        } else {
            // For receiver.
            completion_queue_attr.size = endpoint_ptr->fabric_info_ptr->rx_attr->size;
        }

        int ret = endpoint_ptr->libfabric_api_ptr->fi_cq_open(endpoint_ptr->domain_ptr, &completion_queue_attr,
                         &endpoint_ptr->completion_queue_ptr, &endpoint_ptr->completion_queue_ptr);
        CHECK_LIBFABRIC_RC(fi_cq_open, ret);
    }

    if (kCdiStatusOk == rs) {
        // Attributes of the address vector to associate with the endpoint.
        struct fi_av_attr address_vector_attr = {
            .type = FI_AV_TABLE,
            .count = 1
        };

        int ret = endpoint_ptr->libfabric_api_ptr->fi_av_open(endpoint_ptr->domain_ptr, &address_vector_attr,
                    &endpoint_ptr->address_vector_ptr, NULL);
        CHECK_LIBFABRIC_RC(fi_av_open, ret);
        // We use remote_fi_addr in EfaTxEndpointStop to check if fi_av_insert was called.
        endpoint_ptr->remote_fi_addr = FI_ADDR_UNSPEC;
    }

    if (kCdiStatusOk == rs) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_endpoint(endpoint_ptr->domain_ptr, endpoint_ptr->fabric_info_ptr,
                    &endpoint_ptr->endpoint_ptr, NULL);
        CHECK_LIBFABRIC_RC(fi_endpoint, ret);
    }

    // Windows does not support this option. It is configured by default as the previous 1.9.x version of libfabric.
#ifndef _WIN32
    // Set RNR (Remote Not Ready) retry counter to match libfabric 1.9.x setting, which forced the EFA hardware to
    // continuously retry to send packets even if the remote is not ready. If this is not done, newer versions of
    // libfabric will cause FI_EAGAIN to be returned from fi_sendmsg() whenever resources are not available on the
    // remote to receive new packets.
    if (kCdiStatusOk == rs && !is_socket_based && is_transmitter && endpoint_ptr->libfabric_api_ptr->version_minor > 9) {
        size_t rnr_retry = 7; // Force hardware to continuously retry. See EFA_RNR_INFINITE_RETRY.
        int ret = fi_setopt(&endpoint_ptr->endpoint_ptr->fid, FI_OPT_ENDPOINT, FI_OPT_EFA_RNR_RETRY, &rnr_retry, sizeof(rnr_retry));
        CHECK_LIBFABRIC_RC(fi_setopt, ret);
    }
#endif

    // Bind address vector.
    if (kCdiStatusOk == rs) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_ep_bind(endpoint_ptr->endpoint_ptr,
                    &endpoint_ptr->address_vector_ptr->fid, 0);
        CHECK_LIBFABRIC_RC(fi_ep_bind, ret);
    }

    if (kCdiStatusOk == rs) {
        flags = is_transmitter ? FI_TRANSMIT : FI_RECV;
        int ret = endpoint_ptr->libfabric_api_ptr->fi_ep_bind(endpoint_ptr->endpoint_ptr,
                    &endpoint_ptr->completion_queue_ptr->fid, flags);
        CHECK_LIBFABRIC_RC(fi_ep_bind, ret);
    }

    if (kCdiStatusOk == rs) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_enable(endpoint_ptr->endpoint_ptr);
        CHECK_LIBFABRIC_RC( fi_enable, ret);
    }

    if (kCdiStatusOk == rs) {
        if (is_transmitter) {
            CdiAdapterState* adapter_state_ptr =
                endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr;
            if (0 == adapter_state_ptr->adapter_data.tx_buffer_size_bytes) {
                SDK_LOG_GLOBAL(kLogError, "Payload transmit buffer size cannot be zero. Set tx_buffer_size_bytes when"
                               " using CdiCoreNetworkAdapterInitialize().");
                rs = kCdiStatusInvalidParameter;
            } else {
                // Register the Tx payload buffer with libfabric.
                if (!(hints_ptr->domain_attr->mr_mode & FI_MR_PROV_KEY))  {
                    CdiOsAtomicInc64(&endpoint_ptr->mr_key); // We are generating keys, so increment it.
                }
                int ret = endpoint_ptr->libfabric_api_ptr->fi_mr_reg(endpoint_ptr->domain_ptr,
                                adapter_state_ptr->tx_payload_buffer_allocated_ptr,
                                adapter_state_ptr->tx_payload_buffer_allocated_size, FI_SEND, 0, endpoint_ptr->mr_key,
                                0, &endpoint_ptr->tx_state.tx_user_payload_memory_region_ptr, NULL);
                CHECK_LIBFABRIC_RC(fi_mr_reg, ret);
                if (NULL == endpoint_ptr->tx_state.tx_user_payload_memory_region_ptr) {
                    SDK_LOG_GLOBAL(kLogError, "fi_mr_reg failed to register Tx user payload memory.");
                    rs = kCdiStatusFatal;
                }
            }
            if (kCdiStatusOk == rs) {
                assert(adapter_con_state_ptr->tx_header_buffer_allocated_size); // Value is calculated at compile time.
                // Register the Tx header buffer with libfabric.
                if (!(hints_ptr->domain_attr->mr_mode & FI_MR_PROV_KEY))  {
                    CdiOsAtomicInc64(&endpoint_ptr->mr_key); // We are generating keys, so increment it.
                }
                int ret = endpoint_ptr->libfabric_api_ptr->fi_mr_reg(endpoint_ptr->domain_ptr,
                                adapter_con_state_ptr->tx_header_buffer_allocated_ptr,
                                adapter_con_state_ptr->tx_header_buffer_allocated_size, FI_SEND, 0,
                                endpoint_ptr->mr_key, 0, &endpoint_ptr->tx_state.tx_internal_memory_region_ptr, NULL);
                CHECK_LIBFABRIC_RC(fi_mr_reg, ret);
                if (NULL == endpoint_ptr->tx_state.tx_internal_memory_region_ptr) {
                    SDK_LOG_GLOBAL(kLogError, "fi_mr_reg failed to register Tx internal packet header memory.");
                    rs = kCdiStatusFatal;
                }
            }
        } else {
            // The endpoint must be enabled before creating the packet pool for both socket and EFA based receivers. The
            // receiver-not-ready (RNR) logic in libfabric will prevent the transmitter from sending before the receiver
            // is ready.
            rs = EfaRxPacketPoolCreate(endpoint_ptr);
        }
    }

    if (kCdiStatusOk == rs) {
        // Get local endpoint address. NOTE: This may not return a valid address until after fi_enable() has been used.
        size_t name_length = sizeof(endpoint_ptr->local_ipv6_gid_array);
        int ret = endpoint_ptr->libfabric_api_ptr->fi_getname(&endpoint_ptr->endpoint_ptr->fid,
                    (void*)&endpoint_ptr->local_ipv6_gid_array, &name_length);
        CHECK_LIBFABRIC_RC(fi_getname, ret);
    }

    if (kCdiStatusOk == rs) {
        bool is_transmitter = (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction);
        char gid_name_str[MAX_IPV6_ADDRESS_STRING_LENGTH];
        DeviceGidToString(endpoint_ptr->local_ipv6_gid_array,
                            sizeof(endpoint_ptr->local_ipv6_gid_array), gid_name_str, sizeof(gid_name_str));
        CDI_LOG_HANDLE(endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->log_handle, kLogDebug,
                        "Using local EFA device GID[%s] (%s).", gid_name_str, is_transmitter ? "Tx" : "Rx");
    }

    if (hints_ptr) {
        hints_ptr->fabric_attr->prov_name = NULL; // Value is statically allocated, so don't want libfabric to free it.
        endpoint_ptr->libfabric_api_ptr->fi_freeinfo(hints_ptr);
    }

    if (kCdiStatusOk == rs) {
        endpoint_ptr->fabric_initialized = true;
    } else {
        LibFabricEndpointClose(endpoint_ptr);
    }

    CdiOsCritSectionRelease(efa_adapter_state_ptr->libfabric_lock);

    return rs;
}

/**
 * Close a libfabric connection to the specified endpoint.
 *
 * @param endpoint_ptr Pointer to EFA endpoint to close.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus LibFabricEndpointClose(EfaEndpointState* endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    AdapterConnectionState* adapter_con_state_ptr = endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr;
    EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    // Make thread-safe to protect access to libfabric for endpoint open/close.
    CdiOsCritSectionReserve(efa_adapter_state_ptr->libfabric_lock);

    endpoint_ptr->fabric_initialized = false;

    bool is_transmitter = (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction);

    {
        char gid_name_str[MAX_IPV6_ADDRESS_STRING_LENGTH];
        DeviceGidToString(endpoint_ptr->local_ipv6_gid_array,
                          sizeof(endpoint_ptr->local_ipv6_gid_array), gid_name_str, sizeof(gid_name_str));
        CDI_LOG_HANDLE(endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->log_handle, kLogDebug,
                       "Closing local EFA device GID[%s] Libfabric version[%d.%d] (%s).", gid_name_str,
                       endpoint_ptr->libfabric_api_ptr->version_major, endpoint_ptr->libfabric_api_ptr->version_minor,
                       is_transmitter ? "Tx" : "Rx");
    }

    if (endpoint_ptr->endpoint_ptr) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->endpoint_ptr->fid);
        CHECK_LIBFABRIC_RC(fi_close, ret);
        endpoint_ptr->endpoint_ptr = NULL;
    }

    if (endpoint_ptr->address_vector_ptr) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->address_vector_ptr->fid);
        CHECK_LIBFABRIC_RC(fi_close, ret);
        endpoint_ptr->address_vector_ptr = NULL;
    }

    if (endpoint_ptr->completion_queue_ptr) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->completion_queue_ptr->fid);
        CHECK_LIBFABRIC_RC(fi_close, ret);
        endpoint_ptr->completion_queue_ptr = NULL;
    }

    if (is_transmitter) {
        if (endpoint_ptr->tx_state.tx_internal_memory_region_ptr) {
            int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->tx_state.tx_internal_memory_region_ptr->fid);
            CHECK_LIBFABRIC_RC(fi_close, ret);
            endpoint_ptr->tx_state.tx_internal_memory_region_ptr = NULL;
        }
        if (endpoint_ptr->tx_state.tx_user_payload_memory_region_ptr) {
            int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->tx_state.tx_user_payload_memory_region_ptr->fid);
            CHECK_LIBFABRIC_RC(fi_close, ret);
            endpoint_ptr->tx_state.tx_user_payload_memory_region_ptr = NULL;
        }
    } else {
        EfaRxPacketPoolFree(endpoint_ptr);
    }

    if (endpoint_ptr->domain_ptr) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->domain_ptr->fid);
        CHECK_LIBFABRIC_RC(fi_close, ret);
        endpoint_ptr->domain_ptr = NULL;
    }

    if (endpoint_ptr->fabric_ptr) {
        int ret = endpoint_ptr->libfabric_api_ptr->fi_close(&endpoint_ptr->fabric_ptr->fid);
        CHECK_LIBFABRIC_RC(fi_close, ret);
        endpoint_ptr->fabric_ptr = NULL;
    }

    if (endpoint_ptr->fabric_info_ptr) {
        endpoint_ptr->libfabric_api_ptr->fi_freeinfo(endpoint_ptr->fabric_info_ptr);
        endpoint_ptr->fabric_info_ptr = NULL;
    }

    CdiOsCritSectionRelease(efa_adapter_state_ptr->libfabric_lock);

    return rs;
}

/**
 * Stop the specified endpoint. This only stops and frees low-level libfabric and EFA device driver resources. Other
 * resources that were created when the connection was created are not affected.
 *
 * @param endpoint_ptr Pointer to the adapter endpoint to stop.
 * @param reopen If true re-opens the libfabric endpoint, otherwise does not re-open it.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaAdapterEndpointStop(EfaEndpointState* endpoint_ptr, bool reopen)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction) {
        EfaTxEndpointStop(endpoint_ptr);
    }

    // Close libfabric endpoint resources.
    LibFabricEndpointClose(endpoint_ptr);

    if (reopen) {
        // Re-open the libfabric endpoint here so we can get the endpoint's address. For the EFA, this will return the
        // device GID and QPN, creating a unique value for each endpoint. See "efa_ep_addr" in the EFA provider (efa.h).
        // This is done so the GID can be sent to the remote using the control interface. The remote GID is required in
        // order to open a Tx EFA endpoint.
        rs = LibFabricEndpointOpen(endpoint_ptr);
    }

    return rs;
}

/**
 * Create an EFA connection using the specified adapter.
 *
 * @param handle Handle of adapter connection to open.
 * @param port_number Control interface port to use for the connection.
 * @param bind_ip_addr_str Pointer to string IP address to bind to.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaConnectionCreate(AdapterConnectionHandle handle, int port_number,
                                           const char* bind_ip_addr_str)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    EfaConnectionState* efa_con_ptr = CdiOsMemAllocZero(sizeof(*efa_con_ptr));
    handle->type_specific_ptr = efa_con_ptr; // Set here before Control Interface is created, since it uses it.

    if (efa_con_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    } else {
        efa_con_ptr->adapter_con_ptr = handle;
    }

    if (kCdiStatusOk == rs) {
        // Create a single control interface that will be shared across all endpoints associated with this
        // connection. Each control command that is received must contain data unique to each endpoint to ensure
        // the command is routed to the correct endpoint.
        ControlInterfaceConfigData config_data = {
            .control_interface_adapter_handle = EfaAdapterGetAdapterControlInterface(handle),
            .msg_from_endpoint_func_ptr = ProbeControlMessageFromBidirectionalEndpoint,
            .msg_from_endpoint_param_ptr = handle,
            .log_handle = handle->log_handle,
            .tx_dest_ip_addr_str = NULL, // Don't specify IP, so socket always uses bind().
            // For Tx, use 0 for port so ephemeral port is generated by the OS via bind().
            .port_number = (kEndpointDirectionSend == handle->direction) ? 0 : port_number,
            .bind_ip_addr_str = bind_ip_addr_str
        };
        rs = ControlInterfaceCreate(&config_data, &handle->control_interface_handle);

        // Control interface is independent of the adapter endpoint, so start it now.
        if (kCdiStatusOk == rs) {
            // Start Rx control interface.
            CdiAdapterStartEndpoint(ControlInterfaceGetEndpoint(handle->control_interface_handle));
        }
    }

    if (kCdiStatusOk != rs) {
        EfaConnectionDestroy(handle);
    }

    return rs;
}

/**
 * Destroy an EFA connection to the specified adapter connection.
 *
 * @param handle Handle of adapter connection to close.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaConnectionDestroy(AdapterConnectionHandle handle)
{
    AdapterConnectionState* adapter_con_ptr = (AdapterConnectionState*)handle;
    if (adapter_con_ptr) {
        EfaConnectionState* efa_con_ptr = (EfaConnectionState*)adapter_con_ptr->type_specific_ptr;
        if (efa_con_ptr) {
            if (adapter_con_ptr->control_interface_handle) {
                ControlInterfaceDestroy(adapter_con_ptr->control_interface_handle);
                adapter_con_ptr->control_interface_handle = NULL;
            }

            CdiPoolPutAll(adapter_con_ptr->tx_extra_header_pool_handle);
            CdiPoolDestroy(adapter_con_ptr->tx_extra_header_pool_handle);
            adapter_con_ptr->tx_extra_header_pool_handle = NULL;

            CdiPoolPutAll(adapter_con_ptr->tx_header_pool_handle);
            CdiPoolDestroy(adapter_con_ptr->tx_header_pool_handle);
            adapter_con_ptr->tx_header_pool_handle = NULL;

            CdiOsMemFree(efa_con_ptr);
            adapter_con_ptr->type_specific_ptr = NULL;
        }
    }

    return kCdiStatusOk;
}

/**
 * Open a EFA connection to the specified adapter endpoint.
 *
 * @param endpoint_handle Handle of adapter endpoint to open.
 * @param remote_address_str Pointer to remote address string.
 * @param port_number Port number for endpoint.
 * @param bind_ip_addr_str Pointer to bind address string.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaEndpointOpen(AdapterEndpointHandle endpoint_handle, const char* remote_address_str,
                                       int port_number, const char* bind_ip_addr_str)
{
    (void)bind_ip_addr_str;

    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    EfaEndpointState* endpoint_ptr = CdiOsMemAllocZero(sizeof(EfaEndpointState));
    if (endpoint_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        // Must initialize this data before any thread resources are setup that might use the type_specific_ptr.
        endpoint_handle->type_specific_ptr = endpoint_ptr;
        endpoint_ptr->adapter_endpoint_ptr = endpoint_handle;
        endpoint_ptr->dest_control_port = port_number;

        // Default to using libfabric 1.9, unless it is not available.
        EfaAdapterState* efa_adapter_state_ptr =
            (EfaAdapterState*)endpoint_handle->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;
            if (efa_adapter_state_ptr->libfabric_api_1_9_ptr) {
                endpoint_ptr->libfabric_api_next_ptr = efa_adapter_state_ptr->libfabric_api_1_9_ptr;
            } else {
                endpoint_ptr->libfabric_api_next_ptr = efa_adapter_state_ptr->libfabric_api_new_ptr;
           }
    }

    if (kCdiStatusOk == rs) {
        // Open the libfabric endpoint here so we can get the endpoint's address. For the EFA, this will return the
        // device GID and QPN, when combined create a unique value for each endpoint. See "efa_ep_addr" in the EFA
        // provider (efa.h).
        rs = LibFabricEndpointOpen(endpoint_ptr);
    }

    if (kCdiStatusOk == rs) {
        if (kEndpointDirectionSend == endpoint_handle->adapter_con_state_ptr->direction) {
            rs = EfaTxEndpointOpen(endpoint_ptr, remote_address_str, port_number);
        } else {
            rs = EfaRxEndpointOpen(endpoint_ptr);
        }
    }

    if (kCdiStatusOk != rs) {
        EfaEndpointClose(endpoint_handle); // Frees endpoint_handle->type_specific_ptr.
        endpoint_ptr = NULL;
    }

    return rs;
}

/**
 * Used to poll pending EFA events and process them.
 *
 * @param endpoint_handle Pointer endpoint state data.
 *
 * @return either kCdiStatusInternalIdle or kCdiStatusOk if successful, otherwise a value that indicates the nature of
 *         the failure is returned. kCdiStatusInternalIdle means that the function performed no productive work while
 *         kCdiStatusOk says that did.
 */
static CdiReturnStatus EfaEndpointPoll(AdapterEndpointHandle endpoint_handle)
{
    CdiReturnStatus rs = kCdiStatusInternalIdle;
    // NOTE: This is only called within the SDK, so no special logging macros needed for logging.
    EfaEndpointState* endpoint_ptr = (EfaEndpointState*)endpoint_handle->type_specific_ptr;

    if (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction) {
        // No need to do any Tx polling if there are no Tx packets in flight to check for completions. NOTE: The Tx
        // Libfabric endpoint is not setup immediately when then endpoint is restarted, so having this check here
        // ensures the endpoint is setup before trying to use it (ie. check completion queues).
        if (endpoint_handle->tx_in_flight_ref_count) {
            rs = EfaTxEndpointPoll(endpoint_ptr);
        }
    } else {
        rs = EfaRxEndpointPoll(endpoint_ptr);
    }

    return rs;
}

/**
 * Reset an EFA connection for the specified adapter endpoint.
 *
 * @param endpoint_handle Handle of adapter endpoint to reset.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaEndpointReset(AdapterEndpointHandle endpoint_handle)
{
    // NOTE: This is only called within the SDK, so no special logging macros needed for logging.
    EfaEndpointState* endpoint_ptr = (EfaEndpointState*)endpoint_handle->type_specific_ptr;

    if (kEndpointDirectionSend == endpoint_handle->adapter_con_state_ptr->direction) {
        EfaTxEndpointReset(endpoint_ptr);
    } else {
        EfaRxEndpointReset(endpoint_ptr);
    }

    // Don't restart endpoints here. Wait until after probe has successfully connected before restarting. Probe will use
    // EfaEndpointStart() to start the endpoint after the protocol version has successfully been negotiated. This
    // prevents in-flight packet acks from erroneously being received from a previously established connection. In this
    // case, in rxr_cq_insert_addr_from_rts() the packet type can be RXR_CONNACK_PKT instead of RXR_RTS_PKT.
    EfaAdapterEndpointStop(endpoint_ptr, false); // false= Don't restart.

    ProbeEndpointResetDone(endpoint_ptr->probe_endpoint_handle);

    return kCdiStatusOk;
}

/**
 * Start an EFA connection for the specified adapter endpoint.
 *
 * @param endpoint_handle Handle of adapter endpoint to start.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaEndpointStart(AdapterEndpointHandle endpoint_handle)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // NOTE: This is only called within the SDK, so no special logging macros needed for logging.
    EfaEndpointState* endpoint_ptr = (EfaEndpointState*)endpoint_handle->type_specific_ptr;
    EfaAdapterState* efa_adapter_state_ptr =
        (EfaAdapterState*)endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    CdiOsCritSectionReserve(efa_adapter_state_ptr->libfabric_lock);
    if (endpoint_ptr->libfabric_api_ptr != endpoint_ptr->libfabric_api_next_ptr) {
        // Libfabric version has changed, so stop the endpoint and then reopen it using the desired version of
        // libfabric. The new version is applied in LibFabricEndpointOpen().
        EfaAdapterEndpointStop(endpoint_ptr, true); // TRUE= re-open the endpoint.
    }
    // Open the libfabric endpoint if it is not currently open.
    if (NULL == endpoint_ptr->fabric_ptr) {
        rs = LibFabricEndpointOpen(endpoint_ptr);
    }
    CdiOsCritSectionRelease(efa_adapter_state_ptr->libfabric_lock);

    if (kCdiStatusOk == rs) {
        ProbeEndpointStart(endpoint_ptr->probe_endpoint_handle);
    }

    return rs;
}

/**
 * Close an EFA connection to the specified adapter endpoint.
 *
 * @param endpoint_handle Handle of adapter endpoint to close.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaEndpointClose(AdapterEndpointHandle endpoint_handle)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    if (endpoint_handle && endpoint_handle->type_specific_ptr) {
        EfaEndpointState* private_state_ptr = (EfaEndpointState*)endpoint_handle->type_specific_ptr;
        EfaAdapterEndpointStop(private_state_ptr, false); // false= don't re-open the libfabric endpoint.

        // Close libfabric endpoint resources.
        LibFabricEndpointClose(private_state_ptr);

        CdiReturnStatus function_rs = kCdiStatusOk;
        if (kEndpointDirectionSend == endpoint_handle->adapter_con_state_ptr->direction) {
            function_rs = EfaTxEndpointClose(private_state_ptr);
        } else {
            function_rs = EfaRxEndpointClose(private_state_ptr);
        }
        if (kCdiStatusOk != function_rs) {
            rs = function_rs;
        }
        // Free the EFA endpoint specific state memory.
        CdiOsMemFree(private_state_ptr);
        endpoint_handle->type_specific_ptr = NULL;
    }

    return rs;
}

/**
 * Shutdown the selected adapter instance.
 *
 * @param adapter_handle Handle of adapter to shutdown.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaAdapterShutdown(CdiAdapterHandle adapter_handle)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    if (adapter_handle != NULL) {
        EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)adapter_handle->type_specific_ptr;
        if (efa_adapter_state_ptr) {
            if (efa_adapter_state_ptr->control_interface_adapter_handle) {
                rs = NetworkAdapterDestroyInternal(efa_adapter_state_ptr->control_interface_adapter_handle);
            }
            CdiOsCritSectionDelete(efa_adapter_state_ptr->libfabric_lock);
            CdiOsMemFree(efa_adapter_state_ptr);
            efa_adapter_state_ptr = NULL;
            adapter_handle->type_specific_ptr = NULL;
        }

        // NOTE: The SGL entries in this pool is stored within the pool buffer, so no additional resource freeing
        // needs to be done here.
        CdiPoolPutAll(adapter_handle->probe_work_request_pool_handle);
        CdiPoolDestroy(adapter_handle->probe_work_request_pool_handle);
        adapter_handle->probe_work_request_pool_handle = NULL;

        if (adapter_handle->tx_payload_buffer_allocated_ptr) {
            if (adapter_handle->tx_payload_buffer_is_hugepages) {
                CdiOsMemFreeHugePage(adapter_handle->tx_payload_buffer_allocated_ptr,
                                     adapter_handle->tx_payload_buffer_allocated_size);
                adapter_handle->tx_payload_buffer_is_hugepages = false;
            } else {
                CdiOsMemFree(adapter_handle->tx_payload_buffer_allocated_ptr);
            }
            adapter_handle->tx_payload_buffer_allocated_ptr = NULL;
        }
    }

    return rs;
}

/**
 * Set an environment variable.
 *
 * @param name_str Pointer to name of variable to set.
 * @param value Integer value to set.
 *
 * @return kCdiStatusOk if successful, otherwise kCdiStatusFatal is returned.
 */
static CdiReturnStatus EnvironmentVariableSet(const char* name_str, int value)
{
    CdiReturnStatus rs = kCdiStatusOk;
    char value_str[MAX_INT_STRING_LENGTH];

    snprintf(value_str, sizeof(value_str), "%d", value); // Convert value to a string.

    if (!CdiOsEnvironmentVariableSet(name_str, value_str)) {
        SDK_LOG_GLOBAL(kLogError, "Failed to set environment variable[%s=%s]", name_str, value_str);
        rs = kCdiStatusFatal;
    } else {
        SDK_LOG_GLOBAL(kLogInfo, "Set environment variable[%s=%s]", name_str, value_str);
    }

    return rs;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus EfaNetworkAdapterInitialize(CdiAdapterState* adapter_state_ptr, bool is_socket_based)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    assert(NULL != adapter_state_ptr);

    CdiReturnStatus rs = kCdiStatusOk;

    EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)CdiOsMemAllocZero(sizeof(EfaAdapterState));
    if (NULL == efa_adapter_state_ptr) {
        rs = kCdiStatusNotEnoughMemory;
    } else {
        efa_adapter_state_ptr->is_socket_based = is_socket_based;
    }

    if (kCdiStatusOk == rs) {
        // Create a critical section used to protect access to libfabric endpoint open/close state data.
        if (!CdiOsCritSectionCreate(&efa_adapter_state_ptr->libfabric_lock)) {
            rs = kCdiStatusNotEnoughMemory;
        }
    }

    // Set environment variables used by libfabric.
    if (kCdiStatusOk == rs) {
        // Set values specific to EFA provider.
        //
        // Disable the shared memory provider, which we are not using. If it is enabled, it will use
        // rxr_check_cma_capability(), which does a fork() and causes a double flush of cached write data to any open
        // files that have not been flushed using fflush(). In case this feature is used, the logic below flushes all
        // open CdiLogger log files using the call to CdiLoggerFlushAllFileLogs().
        rs = EnvironmentVariableSet("FI_EFA_ENABLE_SHM_TRANSFER", false);
        if (kCdiStatusOk == rs) {
            // Set the number of read completion queue entries. Current libfabric default is 50.
            rs = EnvironmentVariableSet("FI_EFA_CQ_READ_SIZE", EFA_CQ_READ_SIZE);
            //EnvironmentVariableSet("FI_EFA_CQ_SIZE", 1024); // default is 1024 (see EFA_DEF_CQ_SIZE)
        }
#ifdef LIBFABRIC_LOG_LEVEL
        if (kCdiStatusOk == rs) {
            rs = EnvironmentVariableSet("FI_LOG_LEVEL", LIBFABRIC_LOG_LEVEL); // Set the libfabric log level.
        }
#endif
    }

    // In order to provide support for legacy versions of the SDK, we must use libfabric v1.9. The protocol changed in
    // libfabric after 1.9 and it is not backwards compatible. So, we dynamically load both libfabric 1.9 and the newer
    // version. Depending on the SDK version used by the remote endpoint, the appropriate version of libfabric can be
    // used.
    if (kCdiStatusOk == rs) {
        rs = LoadLibfabric1_9(&efa_adapter_state_ptr->libfabric_api_1_9_ptr);
        if (kCdiStatusOk != rs) {
            CDI_LOG_THREAD(kLogError, "Failed to load libfabric 1.9 [%s]. Reason[%s].", LIBFABRIC_1_9_FILENAME_STRING,
                           CdiCoreStatusToString(rs));
#ifdef _LINUX
            CDI_LOG_THREAD(kLogError, "dlerror() returned[%s].", dlerror());
#endif
        } else if (!is_socket_based) {
            // Ensure this version of libfabric is compatible with the underlying adapter hardware.
            struct fi_info* hints_ptr = CreateHints(efa_adapter_state_ptr->libfabric_api_1_9_ptr, is_socket_based);
            assert(hints_ptr); // Should never occur.

            uint64_t flags = 0;
            struct fi_info* fi_ptr;

            // Ensure that all log files are flushed before using fi_getinfo() below for the EFA adapter. See
            // comment above about fork().
            CdiLoggerFlushAllFileLogs();
            uint32_t version = efa_adapter_state_ptr->libfabric_api_1_9_ptr->fi_version();
            fi_ptr = NULL;
            int ret = efa_adapter_state_ptr->libfabric_api_1_9_ptr->fi_getinfo(version, NULL, NULL, flags, hints_ptr, &fi_ptr);
            efa_adapter_state_ptr->libfabric_api_1_9_ptr->fi_freeinfo(fi_ptr);
            fi_ptr = NULL;
            hints_ptr->fabric_attr->prov_name = NULL; // Value is statically allocated, so don't want libfabric to free it.
            efa_adapter_state_ptr->libfabric_api_1_9_ptr->fi_freeinfo(hints_ptr);
            hints_ptr = NULL;
            if (0 != ret) {
                SDK_LOG_GLOBAL(kLogWarning, "Libfabric 1.9 being disabled. fi_getinfo() failed for local EFA device. Ret[%d]", ret);
                efa_adapter_state_ptr->libfabric_api_1_9_ptr = NULL;
            }
        }
    }
    if (kCdiStatusOk == rs) {
        rs = LoadLibfabricMainline(&efa_adapter_state_ptr->libfabric_api_new_ptr);
        if (kCdiStatusOk != rs) {
            CDI_LOG_THREAD(kLogError, "Failed to load libfabric new [%s]. Reason[%s].",
                           LIBFABRIC_NEW_FILENAME_STRING, CdiCoreStatusToString(rs));
#ifdef _LINUX
            CDI_LOG_THREAD(kLogError, "dlerror() returned[%s].", dlerror());
#endif
        }
    }

    // Determine memory required for probe EFA packet work requests, which contain EFA packet buffers.
    // ProbePacketWorkRequest are used for sending probe EFA packets. NOTE: Only the packet data must reside in the DMA
    // Tx memory region, but since the additional data in the work request is small just put the entire work request in
    // it to reduce code complexity.
    uint32_t probe_work_request_entries = EFA_PROBE_PACKET_BUFFER_COUNT;
    uint32_t probe_work_request_size = sizeof(ProbePacketWorkRequest);
    uint32_t probe_packet_buffer_size_needed =  CdiPoolGetSizeNeeded(probe_work_request_entries,
                                                                     probe_work_request_size);

    // tx_buffer_size_bytes must be nonzero when the adapter is going to be used for Tx connection.
    uint32_t tx_buffer_size_needed = adapter_state_ptr->adapter_data.tx_buffer_size_bytes + probe_packet_buffer_size_needed;
    if (tx_buffer_size_needed) {
        // If necessary, round up to next even-multiple of hugepages byte size.
        uint64_t allocated_size = NextMultipleOf(tx_buffer_size_needed, CDI_HUGE_PAGES_BYTE_SIZE);
        if (kCdiStatusOk == rs && allocated_size) {
            // If necessary, round up to next even-multiple of hugepages byte size.
            void* mem_ptr = CdiOsMemAllocHugePage(allocated_size);
            // Set flag so we know how to later free Tx buffer.
            adapter_state_ptr->tx_payload_buffer_is_hugepages = NULL != mem_ptr;
            if (NULL == mem_ptr) {
                // Fallback using heap memory.
                mem_ptr = CdiOsMemAlloc(allocated_size);
                if (NULL == mem_ptr) {
                    allocated_size = 0; // Since allocation failed, set allocated size to zero.
                    rs = kCdiStatusNotEnoughMemory;
                }
            }
            adapter_state_ptr->tx_payload_buffer_allocated_size = allocated_size;
            adapter_state_ptr->tx_payload_buffer_allocated_ptr = mem_ptr;

            if (kCdiStatusOk == rs) {
                if (!CdiPoolCreateUsingExistingBuffer("Send EFA ProbePacketWorkRequest Pool",
                        probe_work_request_entries, probe_work_request_size,
                        true, // Make thread-safe, since each connection contains its own probe thread.
                        mem_ptr, probe_packet_buffer_size_needed, NULL,
                        &adapter_state_ptr->probe_work_request_pool_handle)) {
                    rs = kCdiStatusAllocationFailed;
                }
                mem_ptr = ((uint8_t*)mem_ptr) + probe_packet_buffer_size_needed;
            }

            // Set returned pointer to start of Tx buffer available to application.
            adapter_state_ptr->adapter_data.ret_tx_buffer_ptr = (uint8_t*)mem_ptr;
        }
    }

    if (kCdiStatusOk == rs) {
        rs = ControlInterfaceInitialize(adapter_state_ptr->adapter_ip_addr_str,
                                        &efa_adapter_state_ptr->control_interface_adapter_handle);
    }

    adapter_state_ptr->type_specific_ptr = efa_adapter_state_ptr;
    if (kCdiStatusOk == rs) {
        adapter_state_ptr->functions_ptr = &efa_endpoint_functions;
    } else {
        EfaAdapterShutdown(adapter_state_ptr);
    }

    return rs;
}

CdiReturnStatus EfaAdapterProbeEndpointCreate(EfaEndpointState* endpoint_ptr,
                                                ProbeEndpointHandle* ret_probe_handle)
{
    AdapterEndpointState* adapter_endpoint_ptr = endpoint_ptr->adapter_endpoint_ptr;
    CdiReturnStatus rs = ProbeEndpointCreate(adapter_endpoint_ptr,
                                             adapter_endpoint_ptr->adapter_con_state_ptr->log_handle,
                                             ret_probe_handle);
    return rs;
}

CdiReturnStatus EfaAdapterEndpointStart(EfaEndpointState* endpoint_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction) {
        rs = EfaTxEndpointStart(endpoint_ptr);
    }

    return rs;
}

CdiAdapterHandle EfaAdapterGetAdapterControlInterface(AdapterConnectionState* adapter_con_state_ptr)
{
    EfaAdapterState* efa_adapter_ptr = (EfaAdapterState*)adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;
    return efa_adapter_ptr->control_interface_adapter_handle;
}

bool EfaAdapterEndpointProtocolVersionSet(EfaEndpointState* endpoint_ptr,
                                          const CdiProtocolVersionNumber* remote_version_ptr)
{
    bool ret = true;
    EfaAdapterState* efa_adapter_state_ptr =
        (EfaAdapterState*)endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    // If remote version of SDK protocol 2.2 or lower, then use legacy libfabric 1.9 if it is available. Otherwise use
    // libfabric new. NOTE: The state is not actually changed until the next time LibFabricEndpointOpen() is called.
    // This is done to ensure the poll-thread is not active while the change is being made using the Endpoint Manager.
    if (remote_version_ptr->version_num > 2 ||
        (remote_version_ptr->version_num == 2 && remote_version_ptr->major_version_num > 2)) {
        endpoint_ptr->libfabric_api_next_ptr = efa_adapter_state_ptr->libfabric_api_new_ptr;
    } else {
        if (NULL == efa_adapter_state_ptr->libfabric_api_1_9_ptr) {
            CDI_LOG_THREAD(kLogError,
            "The underlying hardware does not support libfabric 1.9. Remote host requires SDK 3.0 or later.");
            ret = false;
        }
        endpoint_ptr->libfabric_api_next_ptr = efa_adapter_state_ptr->libfabric_api_1_9_ptr;
    }

    if (ret) {
        EndpointManagerProtocolVersionSet(endpoint_ptr->probe_endpoint_handle->app_adapter_endpoint_handle->cdi_endpoint_handle,
                                          remote_version_ptr);
    }

    return ret;
}
