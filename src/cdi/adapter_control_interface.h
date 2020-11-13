// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in adapter.c.
 */

#ifndef ADAPTER_CONTROL_INTERFACE_H__
#define ADAPTER_CONTROL_INTERFACE_H__

#include "adapter_api.h"
#include "internal.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a control interface. Each handle represents an
 * instance of an interface.
 */
typedef struct ControlInterfaceState* ControlInterfaceHandle;

/**
 * @brief Type used to hold control interface configuration data.
 */
typedef struct {
    CdiAdapterHandle control_interface_adapter_handle; ///< Handle of adapter used for control interface.

    /// @brief Address of function used to queue packet messages from the endpoint.
    MessageFromEndpoint msg_from_endpoint_func_ptr;
    void* msg_from_endpoint_param_ptr;    ///< Parameter passed to queue message function.

    CdiLogHandle log_handle; ///< Handle of logger associated with this control interface.

    const char* tx_dest_ip_addr_str; ///< Tx destination IP. Only used by Tx control interface.
    int port_number;        ///< Port number related to this control interface.
} ControlInterfaceConfigData;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize the control interface using the specific adapter IP and return a handle to the adapter.
 *
 * @param adapter_ip_addr_str Pointer to adapter's IP address string.
 * @param ret_handle_ptr Address where to write returned handle of the adapter.
 *
 * @return A value from the CdiReturnStatus enumeration.
*/
CdiReturnStatus ControlInterfaceInitialize(const char* adapter_ip_addr_str, CdiAdapterHandle* ret_handle_ptr);

/**
 * Create a Tx control interface. NOTE: For a receiver (which is a server), we have to get the destination IP and port
 * information from the transmitter (the client) before this function can be used.
 *
 * @param config_data_ptr Pointer to control interface configuration data.
 * @param direction Interface direction (receive or send).
 * @param ret_handle_ptr Address where to write returned handle of the control interface.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus ControlInterfaceCreate(const ControlInterfaceConfigData* config_data_ptr, EndpointDirection direction,
                                       ControlInterfaceHandle* ret_handle_ptr);

/**
 * Destroy a specific TX or RX control interface and free resources that were created for it.
 *
 * @param handle Handle of control interface to destroy.
 */
void ControlInterfaceDestroy(ControlInterfaceHandle handle);

/**
 * Return the adapter connection associated with the specified control interface.
 *
 * @param handle Handle of control interface.
 *
 * @return Handle of adapter connection.
 */
AdapterConnectionHandle ControlInterfaceGetConnection(ControlInterfaceHandle handle);

/**
 * Return the adapter endpoint associated with the specified control interface.
 *
 * @param handle Handle of control interface.
 *
 * @return Handle of adapter endpoint.
 */
AdapterEndpointHandle ControlInterfaceGetEndpoint(ControlInterfaceHandle handle);

#endif // ADAPTER_CONTROL_INTERFACE_H__
