// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

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
 * @brief Structure used to hold Tx packet headers. Must reside in the DMA Tx memory region, since it is used directly
 * by the adapter. It includes space for message prefix.
 */
typedef struct {
    uint8_t header[MAX_MSG_PREFIX_SIZE + sizeof(CdiRawPacketHeader)]; ///< Tx packet header data.
} TxPacketHeader;

/**
 * @brief Structure used to hold Tx packet headers that contain extra data. Must reside in the DMA Tx memory region,
 * since it is used directly by the adapter. It includes space for message prefix.
 */
typedef struct {
    uint8_t header[MAX_MSG_PREFIX_SIZE + sizeof(CdiRawExtraPacketHeader)]; ///< Tx packet header with extra data.
} TxExtraPacketHeader;

/**
 * @brief Structure used to hold a transmit packet work request. The lifespan of a work request starts when a packet is
 * queued to be sent and ends when a message is received that it has either been successfully sent or a failure has
 * occurred.
 */
typedef struct {
    TxPayloadState* payload_state_ptr; ///< Pointer to Tx payload state structure.
    uint16_t payload_num;              ///< Packet payload number.
    uint16_t packet_payload_size;      ///< Size of payload, not including the packet header.
    Packet packet;                     ///< The top level packet structure for the data in this work request.

    /// @brief Handle of pool associated with header pointer in structure below. If non-null then header pointer is
    /// valid. If payload_state_ptr->app_payload_cb_data.extra_data_size is non-zero, then extra_header_ptr is used,
    /// otherwise header_ptr is used.
    CdiPoolHandle header_pool_handle;
    union {
        void* union_ptr; ///< Generic pointer used as pool item parameter when using the Pool API.
        /// @brief Pointer to data for the packet header. It uses entry zero in the packet SGL. NOTE: Must point to a
        /// buffer in the DMA TX memory region that is allocated during adapter initialization.
        TxPacketHeader* header_ptr;
        /// @brief Pointer to data for the packet header that contains extra data. It uses entry zero in the packet SGL.
        /// NOTE: Must point to a buffer in the DMA TX memory region that is allocated during adapter initialization.
        TxExtraPacketHeader* extra_header_ptr;
    };
} TxPacketWorkRequest;

//*********************************************************************************************************************
//******************************************* STARTFui OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// @see CdiRawTxCreate
CdiReturnStatus TxCreateInternal(CdiConnectionProtocolType protocol_type, CdiTxConfigData* config_data_ptr,
                                 CdiCallback tx_cb_ptr, CdiConnectionHandle* ret_handle_ptr);

/// @see CdiAvmTxStreamConnectionCreate
CdiReturnStatus TxStreamConnectionCreateInternal(CdiTxConfigData* config_data_ptr, CdiCallback tx_cb_ptr,
                                                 CdiConnectionHandle* ret_handle_ptr);

/// @see CdiAvmTxStreamEndpointCreate
CdiReturnStatus TxStreamEndpointCreateInternal(CdiConnectionHandle handle, CdiTxConfigDataStream* stream_config_ptr,
                                               CdiEndpointHandle* ret_handle_ptr);

/// @see CdiRawTxPayload
CdiReturnStatus TxPayloadInternal(CdiEndpointState* endpoint_ptr, const CdiCoreTxPayloadConfig* core_payload_config_ptr,
                                  const CdiSgList* sgl_ptr, int max_latency_microsecs, int extra_data_size,
                                  uint8_t* extra_data_ptr);

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
 * @param message_type Endpoint message type.
 */
void TxPacketWorkRequestComplete(void* param_ptr, Packet* packet_ptr, EndpointMessageType message_type);

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
