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

#include "adapter_efa_probe_rx.h"
#include "internal.h"
#include "internal_log.h"
#include "internal_tx.h"
#include "private.h"
#include "cdi_os_api.h"

#include "rdma/fi_cm.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

#ifndef FT_FIVERSION
/// @brief The current FI Version.
#define FT_FIVERSION FI_VERSION(1,9)
#endif

/// @brief Calculate the maximum TCP payload size by starting with the jumbo frame size supported by AWS networks and
/// subtracting space for the IP and TCP headers. The space for the Ethernet headers has already been subtracted since
/// the true jumbo frame size supported is 9023. Reference:
/// https://aws.amazon.com/about-aws/whats-new/2018/10/aws-direct-connect-now-supports-jumbo-frames-for-amazon-virtual-private-cloud-traffic/
/// https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/network_mtu.html
#define MAX_TCP_PACKET_SIZE (9001 - 20 - 20)

/// @brief Maximum string length of an integer value.
#define MAX_INT_STRING_LENGTH   (32)

/// Forward declaration of function.
static CdiReturnStatus EfaConnectionCreate(AdapterConnectionHandle handle, int port_number);
/// Forward declaration of function.
static CdiReturnStatus EfaConnectionDestroy(AdapterConnectionHandle handle);
/// Forward declaration of function.
static CdiReturnStatus EfaEndpointOpen(AdapterEndpointHandle endpoint_handle, const char* remote_address_str,
                                       int port_number);
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
} EfaAdapterState;

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
 * Allocate memory for a libfabric hints structure, initialize it for the EFA adapter and return a pointer to the next
 * structure.
 *
 * @param is_socket_based Specifies whether the adapter is socket-based (true) or EFA-based (false).
 *
 * @return Pointer to new hints structure. Returns NULL if unable to allocate memory.
 */
static struct fi_info* CreateHints(bool is_socket_based)
{
    char* provider_name = NULL;
    if (is_socket_based) {
        provider_name = "sockets";
    } else {
        provider_name = "efa";
    }

    struct fi_info* hints_ptr = fi_allocinfo();

    if (hints_ptr) {
        hints_ptr->fabric_attr->prov_name = provider_name;
        hints_ptr->ep_attr->type = FI_EP_RDM;
        hints_ptr->domain_attr->resource_mgmt = FI_RM_ENABLED;
        hints_ptr->caps = FI_MSG;
        hints_ptr->mode = FI_CONTEXT;
        hints_ptr->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_VIRT_ADDR;

        // Not using FI_THREAD_SAFE, to prevent use of locks. NOTE: This means that single-thread access to  libfabric
        // must be used.
        hints_ptr->domain_attr->threading = FI_THREAD_DOMAIN;

        hints_ptr->tx_attr->comp_order = FI_ORDER_NONE;
        hints_ptr->rx_attr->comp_order = FI_ORDER_NONE;
    }

    return hints_ptr;
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
    EfaAdapterState* efa_adapter_state_ptr =
        (EfaAdapterState*)endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;
    bool is_socket_based = efa_adapter_state_ptr->is_socket_based;
    bool is_transmitter = (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction);

    uint64_t flags = 0;
    int ret = 0;

    // Start with the EFA defaults, then override if socket-based.
    const char* node_str = NULL;
    char* service_str = NULL;
    char port_str[16];
    if (is_socket_based) {
        service_str = port_str;

        const int data_port = 1 + endpoint_ptr->dest_control_port;
        const int port_ret = snprintf(port_str, sizeof port_str, "%"PRIu16, data_port);
        if (port_ret < 0 || (port_ret >= (int)(sizeof port_str))) {
            return kCdiStatusFatal;
        }
    }

    if (is_transmitter) {
        // Transmitter.
        flags = 0;
        if (is_socket_based) {
            node_str = endpoint_ptr->remote_ip_str;
        } else {
            node_str = NULL;
        }
    } else {
        // Receiver.
        flags = FI_SOURCE;
        node_str = NULL;
    }

    struct fi_info* hints_ptr = CreateHints(is_socket_based);
    if (NULL == hints_ptr) {
        return kCdiStatusAllocationFailed;
    }

    if (0 == ret) {
        ret = fi_getinfo(FT_FIVERSION, node_str, service_str, flags, hints_ptr, &endpoint_ptr->fabric_info_ptr);
    }

    if (0 == ret) {
        ret = fi_fabric(endpoint_ptr->fabric_info_ptr->fabric_attr, &endpoint_ptr->fabric_ptr, NULL);
    }

    if (0 == ret) {
        ret = fi_domain(endpoint_ptr->fabric_ptr, endpoint_ptr->fabric_info_ptr,
                        &endpoint_ptr->domain_ptr, NULL);
    }

    struct fi_cq_attr completion_queue_attr = {
        .wait_obj = FI_WAIT_NONE,
        .format = FI_CQ_FORMAT_DATA
    };

    if (0 == ret) {
        if (is_transmitter) {
            // For transmitter.
            completion_queue_attr.size = endpoint_ptr->fabric_info_ptr->tx_attr->size;
        } else {
            // For receiver.
            completion_queue_attr.size = endpoint_ptr->fabric_info_ptr->rx_attr->size;
        }

        ret = fi_cq_open(endpoint_ptr->domain_ptr, &completion_queue_attr,
                         &endpoint_ptr->completion_queue_ptr, &endpoint_ptr->completion_queue_ptr);
    }

    // Attributes of the address vector to associate with the endpoint.
    struct fi_av_attr address_vector_attr = {
        .type = FI_AV_TABLE,
        .count = 1
    };

    if (0 == ret) {
        ret = fi_av_open(endpoint_ptr->domain_ptr, &address_vector_attr, &endpoint_ptr->address_vector_ptr,
                         NULL);
    }

    if (0 == ret) {
        ret = fi_endpoint(endpoint_ptr->domain_ptr, endpoint_ptr->fabric_info_ptr,
                          &endpoint_ptr->endpoint_ptr, NULL);
    }

    // Bind address vector.
    if (0 == ret) {
        ret = fi_ep_bind(endpoint_ptr->endpoint_ptr, &endpoint_ptr->address_vector_ptr->fid, 0);
    }

    if (0 == ret) {
        flags = is_transmitter ? FI_TRANSMIT : FI_RECV;
        ret = fi_ep_bind(endpoint_ptr->endpoint_ptr, &endpoint_ptr->completion_queue_ptr->fid, flags);
    }

    if (0 == ret) {
        if (is_transmitter) {
            CdiAdapterState* adapter_state_ptr =
                endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr;
            // Register the Tx buffer with libfabric.
            ret = fi_mr_reg(endpoint_ptr->domain_ptr, adapter_state_ptr->adapter_data.ret_tx_buffer_ptr,
                            adapter_state_ptr->adapter_data.tx_buffer_size_bytes, FI_SEND, 0, 0, 0,
                            &endpoint_ptr->tx_state.memory_region_ptr, NULL);
        } else if (!is_socket_based) {
            // For the EFA a packet pool must be created before marking the endpoint enabled or else packets end up in
            // a shared buffer that is never emptied and can overrun.
            rs = EfaRxPacketPoolCreate(endpoint_ptr);
            if (kCdiStatusOk != rs) {
                ret = -1;
            }
        }
    }

    if (0 == ret) {
        ret = fi_enable(endpoint_ptr->endpoint_ptr);
        // For Socket based receivers the endpoint must be enabled before creating the packet pool. This is because the
        // socket receiver code in libfabric's sock_ep_recvmsg() will return an error (FI_EOPBADSTATE) if
        // EfaRxPacketPoolCreate() is called befor fi_enable because rx_ctx->enabled is false on line 91 of sock_msg.c.
        if (!is_transmitter && is_socket_based) {
            rs = EfaRxPacketPoolCreate(endpoint_ptr);
            if (kCdiStatusOk != rs) {
                ret = -1;
            }
        }
    }

    if (0 == ret) {
        // Get local endpoint address. NOTE: This may not return a valid address until after fi_enable() has been used.
        size_t name_length = sizeof(endpoint_ptr->local_ipv6_gid_array);
        ret = fi_getname(&endpoint_ptr->endpoint_ptr->fid, (void*)&endpoint_ptr->local_ipv6_gid_array,
                         &name_length);

        if (0 == ret) {
            char gid_name_str[MAX_IPV6_ADDRESS_STRING_LENGTH];
            DeviceGidToString(endpoint_ptr->local_ipv6_gid_array,
                              sizeof(endpoint_ptr->local_ipv6_gid_array), gid_name_str, sizeof(gid_name_str));
            CDI_LOG_HANDLE(endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->log_handle, kLogDebug,
                           "Using local EFA device GID[%s].", gid_name_str);
        }
    }

    if (hints_ptr) {
        hints_ptr->fabric_attr->prov_name = NULL;
        fi_freeinfo(hints_ptr);
    }

    if (0 != ret && kCdiStatusOk == rs) {
        rs = kCdiStatusFatal;
    }

    return rs;
}

/**
 * Close a libfabric connection to the specified endpoint.
 *
 * @param endpoint_ptr Pointer to EFA endpoint to close.
 */
static void LibFabricEndpointClose(EfaEndpointState* endpoint_ptr)
{
    bool is_transmitter = (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction);

    if (!is_transmitter) {
        EfaRxPacketPoolFree(endpoint_ptr);
    }

    if (endpoint_ptr->endpoint_ptr) {
        fi_close(&endpoint_ptr->endpoint_ptr->fid);
        endpoint_ptr->endpoint_ptr = NULL;
    }

    if (endpoint_ptr->address_vector_ptr) {
        fi_close(&endpoint_ptr->address_vector_ptr->fid);
        endpoint_ptr->address_vector_ptr = NULL;
    }

    if (endpoint_ptr->completion_queue_ptr) {
        fi_close(&endpoint_ptr->completion_queue_ptr->fid);
        endpoint_ptr->completion_queue_ptr = NULL;
    }

    if (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction &&
            endpoint_ptr->tx_state.memory_region_ptr) {
        fi_close(&endpoint_ptr->tx_state.memory_region_ptr->fid);
        endpoint_ptr->tx_state.memory_region_ptr = NULL;
    }

    if (endpoint_ptr->domain_ptr) {
        fi_close(&endpoint_ptr->domain_ptr->fid);
        endpoint_ptr->domain_ptr = NULL;
    }

    if (endpoint_ptr->fabric_ptr) {
        fi_close(&endpoint_ptr->fabric_ptr->fid);
        endpoint_ptr->fabric_ptr = NULL;
    }

    if (endpoint_ptr->fabric_info_ptr) {
        fi_freeinfo(endpoint_ptr->fabric_info_ptr);
        endpoint_ptr->fabric_info_ptr = NULL;
    }
}

/**
 * Stop the specified endpoint. This only stops and frees low-level libfabric and EFA device driver resources. Other
 * resources that were created when the connection was created are not affected.
 *
 * @param endpoint_ptr Pointer to the adapter endpoint to stop.
 * @param do_reopen If true, re-opens the libfabric endpoint, otherwise does not re-open it.
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaAdapterEndpointStop(EfaEndpointState* endpoint_ptr, bool do_reopen)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (kEndpointDirectionSend == endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->direction) {
        EfaTxEndpointStop(endpoint_ptr);
    }

    // Close libfabric endpoint resources.
    LibFabricEndpointClose(endpoint_ptr);

    if (do_reopen) {
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
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaConnectionCreate(AdapterConnectionHandle handle, int port_number)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    EfaConnectionState* efa_con_ptr = CdiOsMemAllocZero(sizeof(*efa_con_ptr));
    if (efa_con_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        efa_con_ptr->adapter_con_ptr = handle;

        // A single Rx control interface exists for each connection. One Tx control interface exists for each endpoint
        // of a connection. Have the kernel choose an ephemeral port number if this is a sending connection, otherwise
        // listen on the configured port number on the receiving connection.
        port_number = (kEndpointDirectionReceive == handle->direction) ? port_number : 0;

        EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)handle->adapter_state_ptr->type_specific_ptr;
        ControlInterfaceConfigData config_data = {
            .control_interface_adapter_handle = efa_adapter_state_ptr->control_interface_adapter_handle,
            .msg_from_endpoint_func_ptr = ProbeRxControlMessageFromEndpoint,
            .msg_from_endpoint_param_ptr = handle,
            .log_handle = handle->log_handle,
            .port_number = port_number,
        };
        rs = ControlInterfaceCreate(&config_data, kEndpointDirectionReceive, &efa_con_ptr->rx_control_handle);
    }

    // The control interfaces are independent of the adapter endpoint, so we want to start them now.
    if (kCdiStatusOk == rs) {
        // Start Rx control interface.
        if (kCdiStatusOk != CdiAdapterStartEndpoint(ControlInterfaceGetEndpoint(efa_con_ptr->rx_control_handle))) {
            rs = kCdiStatusFatal;
        }
    }

    handle->type_specific_ptr = efa_con_ptr;
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
            // Must close the control interface before destroying resources they use, such as control_packet_fifo_handle.
            // Close sockets if they are open (CdiAdapterCloseEndpoint checks if handle is NULL).
            ControlInterfaceDestroy(efa_con_ptr->rx_control_handle);
            efa_con_ptr->rx_control_handle = NULL;

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
 *
 * @return kCdiStatusOk if successful, otherwise a value that indicates the nature of the failure is returned.
 */
static CdiReturnStatus EfaEndpointOpen(AdapterEndpointHandle endpoint_handle, const char* remote_address_str,
                                       int port_number)
{
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

        if (kEndpointDirectionSend == endpoint_handle->adapter_con_state_ptr->direction) {
            CdiOsStrCpy(endpoint_ptr->remote_ip_str, sizeof(endpoint_ptr->remote_ip_str),
                        remote_address_str);
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
        rs = EfaTxEndpointPoll(endpoint_ptr);
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

    EfaAdapterEndpointStop(endpoint_ptr, true); // true= re-open the libfabric endpoint

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
    // NOTE: This is only called within the SDK, so no special logging macros needed for logging.
    EfaEndpointState* endpoint_ptr = (EfaEndpointState*)endpoint_handle->type_specific_ptr;

    ProbeEndpointStart(endpoint_ptr->probe_endpoint_handle);

    return kCdiStatusOk;
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
            CdiOsMemFree(efa_adapter_state_ptr);
            adapter_handle->type_specific_ptr = NULL;
        }

        if (adapter_handle->adapter_data.ret_tx_buffer_ptr) {
            if (adapter_handle->tx_buffer_is_hugepages) {
                CdiOsMemFreeHugePage(adapter_handle->adapter_data.ret_tx_buffer_ptr,
                                     adapter_handle->adapter_data.tx_buffer_size_bytes);
                adapter_handle->tx_buffer_is_hugepages = false;
            } else {
                CdiOsMemFree(adapter_handle->adapter_data.ret_tx_buffer_ptr);
            }
            adapter_handle->adapter_data.ret_tx_buffer_ptr = NULL;
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
    assert(adapter_state_ptr != NULL);

    CdiReturnStatus rs = kCdiStatusOk;

    EfaAdapterState* efa_adapter_state_ptr = (EfaAdapterState*)CdiOsMemAllocZero(sizeof *efa_adapter_state_ptr);
    if (efa_adapter_state_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    efa_adapter_state_ptr->is_socket_based = is_socket_based;

    if (rs == kCdiStatusOk && adapter_state_ptr->adapter_data.tx_buffer_size_bytes) {
        void* mem_ptr = CdiOsMemAllocHugePage(adapter_state_ptr->adapter_data.tx_buffer_size_bytes);
        if (NULL == mem_ptr) {
            // Fallback using heap memory.
            mem_ptr = CdiOsMemAlloc(adapter_state_ptr->adapter_data.tx_buffer_size_bytes);
            if (NULL == mem_ptr) {
                rs = kCdiStatusNotEnoughMemory;
            }
        } else {
            // Tx buffer was allocated using huge pages. Set flag so we know how to later free it.
            adapter_state_ptr->tx_buffer_is_hugepages = true;
        }
        adapter_state_ptr->adapter_data.ret_tx_buffer_ptr = mem_ptr;
    }

    // Set environment variables used by libfabric.
#ifdef LIBFABRIC_LOG_LEVEL
    rs = EnvironmentVariableSet("FI_LOG_LEVEL", LIBFABRIC_LOG_LEVEL); // Set the libfabric log level.
#endif
    if (rs == kCdiStatusOk && !is_socket_based) {
        // Set values specific to EFA provider.
        //
        // Disable the shared memory provider, which we are not using. If it is enabled, it will use
        // rxr_check_cma_capability(), which does a fork() and causes a double flush of cached write data to any open
        // files that have not been flushed using fflush(). In case this feature is used, the logic below flushes all
        // open CdiLogger log files using the call to CdiLoggerFlushAllFileLogs().
        rs = EnvironmentVariableSet("FI_EFA_ENABLE_SHM_TRANSFER", false);
        if (rs == kCdiStatusOk) {
            // Set the number of read completion queue entries. Current libfabric default is 50.
            rs = EnvironmentVariableSet("FI_EFA_CQ_READ_SIZE", EFA_CQ_READ_SIZE);
            //EnvironmentVariableSet("FI_EFA_CQ_SIZE", 1024); // default is 1024 (see EFA_DEF_CQ_SIZE)
        }
    }

    if (rs == kCdiStatusOk) {
        if (is_socket_based) {
            adapter_state_ptr->maximum_payload_bytes = MAX_TCP_PACKET_SIZE;
        } else {
            struct fi_info* hints_ptr = CreateHints(is_socket_based);
            if (NULL == hints_ptr) {
                rs = kCdiStatusNotEnoughMemory;
            } else {
                uint64_t flags = 0;
                struct fi_info* fi_ptr;

                // Ensure that the all log files are flushed before using fi_get_info() below for the EFA adapter. See
                // comment above about fork().
                CdiLoggerFlushAllFileLogs();

                int ret = fi_getinfo(FT_FIVERSION, NULL, NULL, flags, hints_ptr, &fi_ptr);
                if (0 != ret) {
                    SDK_LOG_GLOBAL(kLogError, "fi_getinfo() failed for local EFA device. Ret[%d]", ret);
                    rs = kCdiStatusFatal;
                } else {
                    // Get MTU size from adapter.
                    adapter_state_ptr->maximum_payload_bytes = fi_ptr->nic->link_attr->mtu;

                    /// See logic in rxr_get_rts_data_size().
                    /// Don't want to exceed MTU user data size (MTU - 64 bytes for SRD headers).
                    adapter_state_ptr->maximum_payload_bytes -= 64;

                    // Get Tx IOV Limit (maximum number of SGL entries for a payload).
                    adapter_state_ptr->maximum_tx_sgl_entries = fi_ptr->tx_attr->iov_limit;

                    fi_freeinfo(fi_ptr);
                }
                hints_ptr->fabric_attr->prov_name = NULL;
                fi_freeinfo(hints_ptr);
            }
        }
        assert(adapter_state_ptr->maximum_payload_bytes > 0);
    }

    if (kCdiStatusOk == rs) {
        rs = ControlInterfaceInitialize(adapter_state_ptr->adapter_ip_addr_str,
                                        &efa_adapter_state_ptr->control_interface_adapter_handle);
    }

    adapter_state_ptr->type_specific_ptr = efa_adapter_state_ptr;
    if (rs == kCdiStatusOk) {
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

CdiAdapterHandle EfaAdapterGetAdapterControlInterface(EfaEndpointState* endpoint_ptr)
{
    EfaAdapterState* efa_adapter_ptr = (EfaAdapterState*)
        endpoint_ptr->adapter_endpoint_ptr->adapter_con_state_ptr->adapter_state_ptr->type_specific_ptr;

    return efa_adapter_ptr->control_interface_adapter_handle;
}
