// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in internal_tx.c.
 */

#ifndef CDI_INTERNAL_TX_H__
#define CDI_INTERNAL_TX_H__

#include "adapter_api.h"
#include "private.h"
#include "cdi_core_api.h"
#include "cdi_raw_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Structure used to hold a transmit packet work request. The lifespan of a workrequest starts when a packet is
 * queued to be sent and ends when a message is received that it has either been successfully sent or a failure has
 * occurred.
 */
typedef struct {
    TxPayloadState* payload_state_ptr;  ///< Pointer to Tx payload state structure.
    uint8_t payload_num;                ///< Packet payload number.
    uint16_t packet_sequence_num;       ///< Packet sequence number.
    uint16_t packet_payload_size;       ///< Size of payload, not including the packet header.
    Packet packet;                      ///< The top level packet structure for the data in this work request.
    CdiCDIPacketHeaderUnion header;   ///< The data for the packet header, entry zero in packet_sgl.
} TxPacketWorkRequest;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// @see CdiRawTxCreate
CdiReturnStatus TxCreateInternal(ConnectionProtocolType protocol_type, CdiTxConfigData* config_data_ptr,
                                 CdiCallback tx_cb_ptr, CdiConnectionHandle* ret_handle_ptr);

/// @see CdiAvmTxCreateStreamConnection
CdiReturnStatus TxCreateStreamConnectionInternal(CdiTxConfigData* config_data_ptr, CdiCallback tx_cb_ptr,
                                                 CdiConnectionHandle* ret_handle_ptr);

/// @see CdiAvmTxCreateStream
CdiReturnStatus TxCreateStreamEndpointInternal(CdiConnectionHandle handle, CdiTxConfigDataStream* stream_config_ptr,
                                               CdiEndpointHandle* ret_handle_ptr);

/// @see CdiRawTxPayload
CdiReturnStatus TxPayloadInternal(CdiConnectionHandle con_handle,
                                  const CdiCoreTxPayloadConfig* core_payload_config_ptr, const CdiSgList* sgl_ptr,
                                  int max_latency_microsecs, int extra_data_size, uint8_t* extra_data_ptr);

/**
 * Join Tx connection threads as part of shutting down a connection. This function waits for them to stop.
 *
 * @param con_handle Connection handle.
 */
CdiReturnStatus TxConnectionThreadJoin(CdiConnectionHandle con_handle);

/// @see CdiCoreConnectionDestroy
void TxConnectionDestroyInternal(CdiConnectionHandle con_handle);

/**
 * Destroy resources associated with the specified endpoint.
 *
 * @param handle Handle of endpoint to destroy.
 */
void TxEndpointDestroy(CdiEndpointHandle handle);

/**
 * A packet has been acknowledged as being received by the receiver. The SGL needs to be freed and we need to determine
 * when the entire payload has been freed and then tell the application.
 *
 * @param param_ptr Pointer to connection that the packet was transmitted on as a void*.
 * @param packet_ptr Pointer to packet state data.
 */
void TxPacketWorkRequestComplete(void* param_ptr, Packet* packet_ptr);

/**
 * Invoke the user registered Tx callback function for a payload.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param app_cb_data_ptr Pointer to application callback data.
 */
void TxInvokeAppPayloadCallback(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr);

/**
 * Flush resources associate with TxPayloadThread(). NOTE: This function should only be called after the thread has been
 * paused using EndpointManagerThreadWait().
 *
 * @param endpoint_ptr Pointer to endpoint to flush resources.
 */
void TxPayloadThreadFlushResources(CdiEndpointState* endpoint_ptr);

#endif  // CDI_INTERNAL_TX_H__
