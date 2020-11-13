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
} ControlInterfaceState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

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

CdiReturnStatus ControlInterfaceCreate(const ControlInterfaceConfigData* config_data_ptr, EndpointDirection direction,
                                       ControlInterfaceHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    ControlInterfaceState* control_ptr = (ControlInterfaceState*)CdiOsMemAllocZero(sizeof *control_ptr);
    if (control_ptr == NULL) {
        rs = kCdiStatusNotEnoughMemory;
    }

    if (kCdiStatusOk == rs) {
        // Open an endpoint to send packets to a remote host using the control interface.
        CdiAdapterConnectionConfigData config_data = {
            .cdi_adapter_handle = config_data_ptr->control_interface_adapter_handle,
            .cdi_connection_handle = NULL,         // Not used by control interface.
            .connection_cb_ptr = NULL,             // Not used by control interface.
            .connection_user_cb_param = NULL,      // Not used by control interface.
            .log_handle = config_data_ptr->log_handle,
            .thread_core_num = -1,
            .direction = direction,

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
            .remote_address_str = (kEndpointDirectionReceive == direction) ? NULL :
                                  config_data_ptr->tx_dest_ip_addr_str,
            .port_number = config_data_ptr->port_number,
            .endpoint_stats_ptr = NULL,       // Not used by control interface.
        };
        rs = CdiAdapterOpenEndpoint(&config_data, &control_ptr->adapter_endpoint_handle);

        // Save a copy of the handle so the polling thread can use it. See ControlInterfacePoll().
        control_ptr->adapter_connection_handle->control_state.control_endpoint_handle =
            control_ptr->adapter_endpoint_handle;
    }

    if (kCdiStatusOk != rs) {
        ControlInterfaceDestroy((ControlInterfaceHandle)control_ptr);
        control_ptr = NULL;
    }
    *ret_handle_ptr = (ControlInterfaceHandle)control_ptr;

    return rs;
}

void ControlInterfaceDestroy(ControlInterfaceHandle handle)
{
    ControlInterfaceState* control_ptr = (ControlInterfaceState*)handle;
    if (control_ptr) {
        if (control_ptr->adapter_connection_handle) {
            // Stop PollThread(). This needs to shutdown before closing the endpoint, otherwise destroying resources
            // the thread is using such as tx_packet_queue_handle will case a hang.
            CdiAdapterStopConnection(control_ptr->adapter_connection_handle);
        }

        if (control_ptr->adapter_endpoint_handle) {
            // Close endpoint.
            CdiAdapterCloseEndpoint(control_ptr->adapter_endpoint_handle);
            control_ptr->adapter_endpoint_handle = NULL;
        }

        if (control_ptr->adapter_connection_handle) {
            // Close connection.
            CdiAdapterDestroyConnection(control_ptr->adapter_connection_handle);
            control_ptr->adapter_connection_handle = NULL;
        }

        CdiOsMemFree(control_ptr);
    }
}
