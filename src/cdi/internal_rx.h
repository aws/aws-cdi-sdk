// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in internal_rx.c.
 */

#ifndef CDI_INTERNAL_RX_H__
#define CDI_INTERNAL_RX_H__

#include "adapter_api.h"
#include "cdi_core_api.h"
#include "cdi_raw_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a Rx work request. Each handle represents a
 * instance of a Rx work request structure.
 * FIFO.
 */
typedef struct RxPayloadWorkRequestState* RxPayloadWorkRequestHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create a raw receiver connection.
 *
 * @param protocol_type Specifies the protocol for payload (E.g. RAW or AVM)
 * @param config_data_ptr Address of a structure with all of the parameters to use for setting up the connection.
 * @param rx_cb_ptr The address of a function which will be called whenever a payload is received from the remote host.
 * @param ret_handle_ptr An address which will receive the handle of the newly created connection.
 *
 * @return kCdiStatusOk if the connection was successfully created, otherwise a value indicating why it failed.
 */
CdiReturnStatus RxCreateInternal(CdiConnectionProtocolType protocol_type, CdiRxConfigData* config_data_ptr,
                                  CdiCallback rx_cb_ptr, CdiConnectionHandle* ret_handle_ptr);

/// @see CdiCoreConnectionDestroy
void RxConnectionDestroyInternal(CdiConnectionHandle con_handle);

/**
 * @brief Create dynamically allocated memory pools for the specified endpoint.
 *
 * @param handle Handle of endpoint to create dynamic memory pools.
 *
 * @return kCdiStatusOk if the connection was successfully created, otherwise a value indicating why it failed.
 */
CdiReturnStatus RxEndpointCreateDynamicPools(CdiEndpointHandle handle);

/**
 * Destroy resources associated with the specified endpoint.
 *
 * @param handle Handle of endpoint to destroy.
 */
void RxEndpointDestroy(CdiEndpointHandle handle);

/**
 * A packet has been received by the receiver. Need to reassemble it into a payload and send the payload SGL to the
 * application.
 *
 * @param param_ptr Pointer to connection that the packet was received on as a void*.
 * @param packet_ptr Pointer to the received packet.
 * @param message_type Endpoint message type.
 */
void RxPacketReceive(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

/**
 * Send the payload on to the next stage because it is complete or determined to be in error.
 *
 * @param endpoint_ptr Pointer to endpoint state structure.
 * @param send_payload_state_ptr Pointer to the payload state for the completed payload.
 */
void RxSendPayload(CdiEndpointState* endpoint_ptr, RxPayloadState* send_payload_state_ptr);

/**
 * Free payload resources.
 *
 * @param endpoint_ptr Pointer to connection state structure.
 * @param payload_state_ptr Pointer to payload structure being updated.
 * @param free_memory_state
 */
void RxFreePayloadResources(CdiEndpointState* endpoint_ptr, RxPayloadState* payload_state_ptr, bool free_memory_state);

/**
 * Invoke the user registered Rx callback function for a payload.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param app_cb_data_ptr Pointer to application callback data.
 */
void RxInvokeAppPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr);

/**
 * Enqueue to free the receive buffer.
 *
 * @param sgl_ptr The scatter-gather list containing the memory to be freed.
 *
 * @return kCdiStatusOk if the connection was successfully created, otherwise a value indicating why it failed.
 */
CdiReturnStatus RxEnqueueFreeBuffer(const CdiSgList* sgl_ptr);

/**
 * Called from PollThread() in the adapter to poll if any Rx buffers need to be freed. If there are any, this function
 * will free payload level resources and then return a list of adapter packet buffer SGLs that need to be freed by the
 * caller.
 *
 * @param param_ptr A pointer to data used by the function.
 * @param ret_packet_buffer_sgl_ptr Pointer to address where to write the returned packet buffer SGL that needs to be
 *                                  freed by the caller.
 *
 * @return Returns true if an adapter buffer SGL is being returned.
 */
bool RxPollFreeBuffer(void* param_ptr, CdiSgList* ret_packet_buffer_sgl_ptr);

/**
 * Flush resources associated with Rx. NOTE: This function should only be called after the thread has been paused using
 * EndpointManagerThreadWait().
 *
 * @param endpoint_ptr Pointer to endpoint to free resources.
 */
void RxEndpointFlushResources(CdiEndpointState* endpoint_ptr);

#endif  // CDI_INTERNAL_RX_H__
