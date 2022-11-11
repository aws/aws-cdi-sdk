// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for the Adapter Control Interface.
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa_probe.h"
#include "adapter_control_interface.h"

#include "adapter_api.h"
#include "endpoint_manager.h"
#include "internal_tx.h"
#include "private.h"
#include "cdi_os_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used to hold control interface state data.
 */
typedef struct {
    AdapterConnectionHandle adapter_connection_handle; ///< Handle of adapter connection.
    AdapterEndpointHandle adapter_endpoint_handle;     ///< Handle of adapter endpoint.
    CdiPoolHandle control_work_request_pool_handle;    ///< Handle of control work request pool.
} ControlInterfaceState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef DEBUG_ENABLE_POOL_DEBUGGING_EFA_PROBE
/**
 * Example pool debug callback function.
 *
 * @param cb_ptr: Pointer to pool data.
 */
static void PoolDebugCallback(const CdiPoolCbData* cb_ptr)
{
    if (cb_ptr->is_put) {
        CDI_LOG_THREAD(kLogDebug, "PUT[%d]", cb_ptr->num_entries);
    } else {
        CDI_LOG_THREAD(kLogDebug, "GET[%d]", cb_ptr->num_entries);
    }
}
#endif

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

CdiReturnStatus ControlInterfaceInitialize(const char* adapter_ip_addr_str, CdiAdapterHandle* ret_handle_ptr)
{
    // NOTE: Since the caller is the application's thread, use SDK_LOG_GLOBAL() for any logging in this function.
    CdiReturnStatus rs = kCdiStatusOk;

    // Create socket type adapter for control interface.
    CdiAdapterData adapter_data = {
        .adapter_ip_addr_str = adapter_ip_addr_str,
        .tx_buffer_size_bytes = CONTROL_INTERFACE_TX_BUFFER_SIZE_BYTES,
        .adapter_type = kCdiAdapterTypeSocket
    };

    if (kCdiStatusOk != CdiCoreNetworkAdapterInitialize(&adapter_data, ret_handle_ptr)) {
        rs = kCdiStatusFatal;
    }

    return rs;
}

AdapterConnectionHandle ControlInterfaceGetConnection(ControlInterfaceHandle handle)
{
    ControlInterfaceState* state_ptr = (ControlInterfaceState*)handle;
    return state_ptr->adapter_connection_handle;
}

AdapterEndpointHandle ControlInterfaceGetEndpoint(ControlInterfaceHandle handle)
{
    ControlInterfaceState* state_ptr = (ControlInterfaceState*)handle;
    return state_ptr->adapter_endpoint_handle;
}

CdiPoolHandle ControlInterfaceGetWorkRequestPoolHandle(ControlInterfaceHandle handle)
{
    ControlInterfaceState* state_ptr = (ControlInterfaceState*)handle;
    return state_ptr->control_work_request_pool_handle;
}

CdiReturnStatus ControlInterfaceCreate(const ControlInterfaceConfigData* config_data_ptr,
                                       ControlInterfaceHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    ControlInterfaceState* control_ptr = (ControlInterfaceState*)CdiOsMemAllocZero(sizeof *control_ptr);
    if (control_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        // ProbePacketWorkRequests are used for sending control packets over the socket interface. One additional
        // entry is required so a control packet can be sent while the probe packet queue is full.
        if (!CdiPoolCreate("Send Control ProbePacketWorkRequest Pool", MAX_PROBE_CONTROL_COMMANDS_PER_CONNECTION + 1,
                           NO_GROW_SIZE, NO_GROW_COUNT,
                           sizeof(ProbePacketWorkRequest), true, // true= Make thread-safe
                           &control_ptr->control_work_request_pool_handle)) {
            rs = kCdiStatusAllocationFailed;
        }
#ifdef DEBUG_ENABLE_POOL_DEBUGGING_EFA_PROBE
        if (kCdiStatusOk == rs) {
            CdiPoolCallbackEnable(control_ptr->control_work_request_pool_handle, PoolDebugCallback);
        }
#endif
    }

    if (kCdiStatusOk == rs) {
        // Open an endpoint to send packets to a remote host using the control interface.
        CdiAdapterConnectionConfigData config_data = {
            .cdi_adapter_handle = config_data_ptr->control_interface_adapter_handle,
            .cdi_connection_handle = NULL,         // Not used by control interface.
            .connection_cb_ptr = NULL,             // Not used by control interface.
            .connection_user_cb_param = NULL,      // Not used by control interface.
            .log_handle = config_data_ptr->log_handle,
            .shared_thread_id = 0, // 0 or -1= Use unique poll thread for this connection.
            .thread_core_num = -1, // -1= Let OS decide which CPU core to use.
            .direction = kEndpointDirectionBidirectional,

            // This endpoint is a control interface. This means that the Endpoint Manager is not used for managing
            // threads related to the connection.
            .data_type = kEndpointTypeControl,
        };
        rs = CdiAdapterCreateConnection(&config_data, &control_ptr->adapter_connection_handle);
    }

    if (kCdiStatusOk == rs) {
        // Open an endpoint to send packets to a remote host. Do this last since doing so will open the flood gates for
        // callbacks to begin.
        CdiAdapterEndpointConfigData config_data = {
            .connection_handle = control_ptr->adapter_connection_handle,
            .msg_from_endpoint_func_ptr = config_data_ptr->msg_from_endpoint_func_ptr,
            .msg_from_endpoint_param_ptr = config_data_ptr->msg_from_endpoint_param_ptr,
            .remote_address_str = config_data_ptr->tx_dest_ip_addr_str,
            .port_number = config_data_ptr->port_number,
            .bind_address_str = config_data_ptr->bind_ip_addr_str,
            .endpoint_stats_ptr = NULL,       // Not used by control interface.
        };
        // Set returned handle, as it may get used as part of enabling the endpoint via CdiAdapterOpenEndpoint().
        *ret_handle_ptr = (ControlInterfaceHandle)control_ptr;
        rs = CdiAdapterOpenEndpoint(&config_data, &control_ptr->adapter_endpoint_handle);

        // Save a copy of the handle so the poll thread can use it. See ControlInterfacePoll().
        control_ptr->adapter_connection_handle->control_state.control_endpoint_handle =
            control_ptr->adapter_endpoint_handle;
    }

    if (kCdiStatusOk != rs) {
        ControlInterfaceDestroy((ControlInterfaceHandle)control_ptr);
        control_ptr = NULL;
        *ret_handle_ptr = NULL;
    }

    return rs;
}

void ControlInterfaceDestroy(ControlInterfaceHandle handle)
{
    ControlInterfaceState* control_ptr = (ControlInterfaceState*)handle;
    if (control_ptr) {
        if (control_ptr->adapter_connection_handle) {
            // Set shutdown signal so the PollThread() will wake-up to process the stop connection.
            CdiOsSignalSet(control_ptr->adapter_connection_handle->shutdown_signal);
            // Stop PollThread(). This needs to shutdown before closing the endpoint, otherwise destroying resources
            // the thread is using such as tx_packet_queue_handle will case a hang.
            CdiAdapterStopConnection(control_ptr->adapter_connection_handle);
        }

        // Now that the poll thread has stopped, ok to free additional resources.
        if (control_ptr->adapter_endpoint_handle) {
            if (control_ptr->adapter_endpoint_handle->tx_packet_queue_handle) {
                // Flush Tx packet queue. For each list in the queue, walk through each item (a packet) and return the
                // related work request to the work request pool.
                CdiSinglyLinkedList tx_packet_list;
                while (CdiQueuePop(control_ptr->adapter_endpoint_handle->tx_packet_queue_handle, &tx_packet_list)) {
                    CdiSinglyLinkedListEntry* entry_ptr = NULL;
                    while (NULL != (entry_ptr = CdiSinglyLinkedListPopHead(&tx_packet_list))) {
                        Packet* packet_ptr = CONTAINER_OF(entry_ptr, Packet, list_entry);
                        ProbePacketWorkRequest* work_request_ptr =
                            (ProbePacketWorkRequest*)packet_ptr->sg_list.internal_data_ptr;
                        CdiPoolPut(control_ptr->control_work_request_pool_handle, work_request_ptr);
                    }
                }
            }

            // Close endpoint.
            CdiAdapterCloseEndpoint(control_ptr->adapter_endpoint_handle);
            control_ptr->adapter_endpoint_handle = NULL;
        }

        // The Control Interface uses this pool, so don't destroy it until after the Control Interface's polling
        // thread has been stopped.
        CdiPoolPutAll(control_ptr->control_work_request_pool_handle);
        CdiPoolDestroy(control_ptr->control_work_request_pool_handle);
        control_ptr->control_work_request_pool_handle = NULL;

        if (control_ptr->adapter_connection_handle) {
            // Close connection.
            CdiAdapterDestroyConnection(control_ptr->adapter_connection_handle);
            control_ptr->adapter_connection_handle = NULL;
        }

        CdiOsMemFree(control_ptr);
    }
}
