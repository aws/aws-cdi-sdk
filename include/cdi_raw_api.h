// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_RAW_API_H__
#define CDI_RAW_API_H__

/**
 * @file
 * @brief
 * This file declares the public API data types, structures and functions that comprise the CDI Raw payload transport
 * SDK API.
 */

// Page for CDI-RAW
/*!
 * @page raw_home_page CDI Raw Payload (CDI-RAW) API Home Page
 * @tableofcontents
 *
 * @section Raw_arch CDI-RAW Architecture
 *
 * The diagram shown below provides a overview of the CDI Raw Payload transmit/receive architecture.
 *
 * @image html "high_level_architecture.jpg"
 *
 * @section ec2_usage_raw CDI-RAW EC2 Instance Workflow Example
 *
 * The diagram shown below provides an example of using the CDI-RAW API on multiple EC2 instances and multiple TX/RX
 * connections.
 *
 * @image html "raw_ec2_usage_example.jpg"
 *
 * @section Raw_main_api CDI-RAW Application Programming Interface (API)
 *
 * The API is declared in: @ref cdi_raw_api.h
 *
 * The diagram shown below provides an example of the typical TX/RX workflow using the CDI-RAW API.
 *
 * @image html "raw_api_workflow.jpg"
 *
 * @section Raw_payloads CDI-RAW Payload Formats
 *
 * The diagram shown below provides some examples of raw payloads using the CDI-RAW API.
 *
 * @image html "raw_payload.jpg"
 */

#include "cdi_core_api.h"
#include <stdint.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure of this type is passed as the parameter to CdiRawRxCallback(). It contains a single payload sent
 * from a transmitter.
 */
typedef struct {
    /// @brief Core common data shared between registered user TX/RX callback functions.
    CdiCoreCbData core_cb_data;

    /// @brief If no error occurred, the payload's data as a scatter-gather list. If the payload is in linear format,
    /// there will only be one element in this list. If an error occurred, this list will have zero entries.
    CdiSgList sgl;
} CdiRawRxCbData;

/**
 * @brief Prototype of receive data callback function. The user code must implement a function with this prototype,
 * provide it to CdiRawRxCreate() as a parameter.
 *
 * This callback function is invoked when a complete payload has been received. The application must use the
 * CdiCoreRxFreeBuffer() API function to free the buffer. This can either be done within the user callback function or
 * at a later time whenever the application is done with the buffer.
 *
 * @param data_ptr A pointer to an CdiRawRxCbData structure.
 */
typedef void (*CdiRawRxCallback)(const CdiRawRxCbData* data_ptr);

/**
 * @brief A structure of this type is passed as the parameter to CdiRawTxCallback(). It contains data related to the
 * transmission of a single payload to a receiver.
 */
typedef struct {
    /// @brief Core common data shared between registered user TX/RX callback functions.
    CdiCoreCbData core_cb_data;
} CdiRawTxCbData;

/**
 * @brief Prototype of transmit data callback function. The user code must implement a function with this prototype and
 * provide it to CdiRawtxCreate() as a parameter.
 *
 * This callback function is invoked when a complete payload has been transmitted.
 *
 * @param data_ptr A pointer to an CdiRawTxCbData structure.
 */
typedef void (*CdiRawTxCallback)(const CdiRawTxCbData* data_ptr);

#ifdef __cplusplus
extern "C" {
#endif

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of a raw transmitter. When the instance is no longer needed, use the CdiCoreConnectionDestroy()
 * API function to free-up resources that are being used by it.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param config_data_ptr Pointer to transmitter configuration data. Copies of the data in the configuration data
 *                        structure are made as needed. A local variable can be used for composing the structure since
 *                        its contents are not needed after this function returns.
 * @param tx_cb_ptr Address of the user function to call whenever a payload being transmitted has been received by the
 *                  receiver or a transmit timeout error has occurred.
 * @param ret_handle_ptr Pointer to returned connection handle. The handle is used as a parameter to other API functions
 *                       to identify this specific transmitter.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiRawTxCreate(CdiTxConfigData* config_data_ptr, CdiRawTxCallback tx_cb_ptr,
                                             CdiConnectionHandle* ret_handle_ptr);

/**
 * Create an instance of a raw receiver. When done, must call CdiCoreConnectionDestroy().
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param config_data_ptr Pointer to receiver configuration data. Copies of the data in the configuration data structure
 *                        are made as needed. A local variable can be used for composing the structure since its
 *                        contents are not needed after this function returns.
 * @param rx_cb_ptr Address of the user function to call whenever a payload has been received.
 * @param ret_handle_ptr Pointer to returned connection handle. The handle is used as a parameter to other API functions
 *                       to identify this specific receiver.
 *
 * @return A value from the CdiReturnStatus enumeration.
 *
 */
CDI_INTERFACE CdiReturnStatus CdiRawRxCreate(CdiRxConfigData* config_data_ptr, CdiRawRxCallback rx_cb_ptr,
                                             CdiConnectionHandle* ret_handle_ptr);

/**
 * Transmit a payload of data to the receiver. This function is asynchronous and will immediately return. The user
 * callback function CdiAvmTxCallback() registered using the CdiAvmTxCreate() API function will be invoked when the
 * payload has been acknowledged by the remote receiver or a transmission timeout occurred.
 *
 * MEMORY NOTE: The payload_config_ptr, video_config_ptr, CdiSgList and SGL entries memory can be modified or released
 * immediately after the function returns. However, the the buffers pointed to in the SGL must not be modified or
 * released until after the CdiAvmTxCallback() has occurred.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param con_handle Connection handle returned by a previous call to CdiTxCreate().
 * @param payload_config_ptr Pointer to payload configuration data. Part of the data is sent along with the payload and
 *                           part is provided to the registered user TX callback function.
 * @param sgl_ptr Scatter-gather list containing the payload to be transmitted. The addresses in the SGL must point to
 *                locations that reside within the memory region specified in CdiAdapterData at ret_tx_buffer_ptr.
 * @param max_latency_microsecs Maximum latency in microseconds. If the transmission time of a payload exceeds this
 *                              value, the CdiRawTxCallback() API function will be invoked with an error.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiRawTxPayload(CdiConnectionHandle con_handle,
                                              const CdiCoreTxPayloadConfig* payload_config_ptr,
                                              const CdiSgList* sgl_ptr, int max_latency_microsecs);

#ifdef __cplusplus
}
#endif

#endif // CDI_RAW_API_H__
