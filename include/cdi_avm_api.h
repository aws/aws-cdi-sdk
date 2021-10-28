// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_AVM_API_H__
#define CDI_AVM_API_H__

/**
 * @file
 * @brief
 * This file declares the public API data types, structures and functions that comprise the CDI audio, video and
 * metadata SDK API.
 */

// Page for CDI-AVM
/*!
 * @page AVM_home_page CDI Audio, Video and Metadata (CDI-AVM) API Home Page
 * @tableofcontents
 *
 * @section avm_intro_sec Introduction
 *
 * The Cloud Digital Interface Audio, Video and Metadata (CDI_AVM) is the library which implements the low-latency
 * reliable transport of audio, video and metadata between EC2 instances within the Amazon network.
 *
 * The AVM interface of the CDI SDK is intended mainly for interoperabilty among vendors while remaining extensible. To
 * better encourage interoperability, a constrained set of audio, video, and ancillary data formats is supported. Audio,
 * for example, is always 24 bit linear PCM in big endian format. Ancillary data follows IETF RFC 8331. Video similarly
 * has a narrow set of supported parameters. Together, these comprise the CDI baseline profile.
 *
 * Extensibility is addressed through a generic configuration mechanism which is used even for the CDI baseline profile.
 * It is based on a structure containing a URI and optional parameter data. The URI is defined such that it ensures
 * uniqueness and optionally points to documentation on how to interpret the parameter data. The format of the payload
 * data is also dependent on the URI. Helper functions ease the process of creating and parsing the generic
 * configuration structure for the CDI baseline profile.
 *
 * The URIs used for the CDI baseline profile are:
 * @code
   https://cdi.elemental.com/specs/baseline-video
   https://cdi.elemental.com/specs/baseline-audio
   https://cdi.elemental.com/specs/baseline-ancillary-data
   @endcode
 *
 * The documents at those URIs fully specify the various aspects of each media type including the parameter data and the
 * in memory representation of payload data. These files also reside in CDI_SDK/doc/specs.
 *
 * Since the media format details are specificated outside of the SDK, new formats (beyond the CDI baseline profile) can
 * be added without changing the SDK. They can be publicly documented or they can remain private for situations where
 * interoperability is not required.
 *
 * @section avm_high_level_arch CDI-AVM Architecture Overview
 *
 * The diagram shown below provides an overview of the CDI-AVM Transmit/Receive architecture.
 *
 * @image html "high_level_architecture.jpg"
 *
 * @section ec2_usage_avm CDI-AVM EC2 Instance Workflow Example (Connections with single endpoints)
 *
 * Connections that contain a single endpoint can be used to transmit video, audio and ancillary data streams that are
 * identified by a "stream_identifier" as defined in the API. This allows applications to transmit and receive multiple
 * streams using single endpoints. See the CdiAvmTxCreate(), CdiAvmTxPayload(), and CdiCoreConnectionDestroy() API
 * functions.
 *
 * The diagram shown below provides an example of using the CDI-AVM API on multiple EC2 instances and multiple TX/RX
 * connections.
 *
 * @image html "avm_ec2_usage_example.jpg"
 *
 * @section ec2_mux_demux_avm CDI-AVM EC2 Instance Workflow Example (Connections with multiple endpoints)
 *
 * Connections that contain multiple endpoints can be used to demux and mux video, audio and ancillary data streams that
 * are identified by a "stream_identifier" as defined in the API. This allows an application to receive multiple streams
 * on a single connection and transmit them to different endpoints. It also allows an application to receive multiple
 * streams from different endpoints on a single connection. Demuxing and muxing of the streams is handled entirely by
 * the CDI-AVM SDK. See the CdiAvmTxStreamConnectionCreate(), CdiAvmTxStreamEndpointCreate(), CdiAvmEndpointTxPayload(),
 * and CdiAvmStreamEndpointDestroy() API functions.
 *
 * The diagram shown below provides an example of using the CDI-AVM API on multiple EC2 instances using single
 * connections that contain multiple endpoints to demux and mux video, audio and ancillary data streams.
 *
 * @image html "multi_endpoint_flow.jpg"
 *
 * @section avm_main_api CDI-AVM Application Programming Interface (API)
 *
 * The API is declared in: @ref cdi_avm_api.h
 *
 * @subsection avm_api Connections with Single Endpoints
 *
 * The diagram shown below provides an example of the typical CDI-AVM TX/RX workflow using the CDI-AVM API for a
 * connection that contains a single endpoint.
 * @image html "avm_api_workflow.jpg"
 *
 * @subsection multi_avm_api Connections with Multiple Endpoints
 *
 * The diagram shown below provides an example of the typical CDI-AVM TX/RX workflow using the CDI-AVM API for a
 * connection that contains multiple endpoints.
 * @image html "multi_endpoint_avm_api_workflow.jpg"
 */

#include <stdint.h>

#include "cdi_core_api.h"
#include "cdi_raw_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This is the generic AVM configuration structure which describes the format of each stream.
 */
typedef struct {
    /// The URI unambiguously specifies the type (audio, video, ancillary data, or other) of data comprising an AVM
    /// stream within an AVM connection. Typically, it will be a URL to a document that describes how to interpret the
    /// bytes of the enclosing structure's data member as well as how payload data is to be formatted. As such, the
    /// scheme portion of the URI will normally be "http" or "https". In order to assure uniqueness of the URI, the host
    /// portion must be specified with its fully qualified domain name which must be registered with ICANN. The path
    /// component, and optional query and fragment parts, of the URI further define precisely the specification of the
    /// stream's data.
    ///
    /// This is a C NUL terminated string whose length is limited to 256 characters. Reserved characters in the host,
    /// path, query, and fragment must be "percent-encoded". See RFC 3986 for details on percent encoding and which
    /// characters are reserved.
    char uri[257];

    /// This data further describes the specifics of the stream where the specification pointed at by the URI has
    /// variables such as resolution, sampling rate, bit depth, etc. How this data is interpreted is determined by the
    /// uri member. Its length, which is specified by the data_size member, is limited to 1024 bytes.
    uint8_t data[1024];

    /// The length of the data in bytes. Valid values are 0 through 1024, inclusive.
    int data_size;
} CdiAvmConfig;

/// @brief Extra data that is sent along with the AVM payload to the receiver. It will be provided to the receiver
/// through a registered user receive callback function.
typedef struct {
    /// @brief Used to identify the data stream. Each stream within a connection must have a unique value.
    uint16_t stream_identifier;
} CdiAvmExtraData;

/// @brief A structure used to configure a AVM transmit payload.
typedef struct {
    /// @brief Core extra data. Part of the data is sent along with the payload and part is passed to the TX registered
    /// user callback function.
    CdiCoreTxPayloadConfig core_config_data;

    /// @brief AVM extra data that is sent along with the payload.
    CdiAvmExtraData avm_extra_data;
} CdiAvmTxPayloadConfig;

/**
 * @brief A structure of this type is passed as the parameter to CdiAvmRxCallback(). It contains a single payload sent
 * from a transmitter.
 */
typedef struct {
    /// @brief Core common data shared between registered user TX/RX callback functions.
    CdiCoreCbData core_cb_data;

    /// @brief Extra data sent along with the AVM payload.
    CdiAvmExtraData avm_extra_data;

    /// @brief An optionally provided pointer to an AVM configuration structure. This is NULL unless a configuration
    /// structure was provided with the payload when it was transmitted. The parameters specified in the structure apply
    /// to the payload data in the scatter-gather list and to all subsequent payloads with this stream identifier until
    /// another configuration structure is supplied to the callback function.
    CdiAvmConfig* config_ptr;

    /// @brief If no error occurred, the payload's data is a scatter-gather list. If the payload is in linear format,
    /// there will only be one element in this list. If an error occurred, this list will have zero entries.
    CdiSgList sgl;
} CdiAvmRxCbData;

/**
 * @brief Prototype of receive data callback function. The user code must implement a function with this prototype and
 * provide it to CdiAvmRxCreate() as a parameter.
 *
 * This callback function is invoked when a complete payload has been received. The application must use the
 * CdiCoreRxFreeBuffer() API function to free the buffer. This can either be done within the user callback function or
 * at a later time whenever the application is done with the buffer.
 *
 * @param data_ptr A pointer to an CdiAvmRxData structure.
 */
typedef void (*CdiAvmRxCallback)(const CdiAvmRxCbData* data_ptr);

/**
 * @brief A structure of this type is passed as the parameter to CdiAvmTxCallback(). It contains data related to the
 * transmission of a single payload to a receiver.
 */
typedef struct {
    /// @brief Core common data shared between registered user TX/RX callback functions.
    CdiCoreCbData core_cb_data;

    /// @brief Extra data that was sent along with the payload.
    CdiAvmExtraData avm_extra_data;
} CdiAvmTxCbData;

/**
 * @brief Prototype of transmit data callback function. The user code must implement a function with this prototype and
 * provide it to CdiAvmTxCreate() as a parameter.
 *
 * This callback function is invoked when a complete payload has been transmitted.
 *
 * @param data_ptr A pointer to an CdiAvmTxCbData structure.
 */
typedef void (*CdiAvmTxCallback)(const CdiAvmTxCbData* data_ptr);

/**
 * @brief Stream configuration data used by the CdiAvmTxStreamEndpointCreate() API function.
 */
typedef struct {
    /// @brief The IP address of the host which is to receive the flow from this transmit stream. NOTE: This must be the
    /// dotted form of an IPv4 address. DNS may be supported in the future.
    const char* dest_ip_addr_str;

    /// @brief The port number to use at the receiving host. The range of valid values is 1 to 65535, inclusive and must
    /// match the value configured for the receiving connection.
    int dest_port;

    /// @brief Pointer to name of the stream. It is used as an identifier when generating log messages that are specific
    /// to this stream. If NULL, a name is internally generated. Length of name must not exceed
    /// CDI_MAX_STREAM_NAME_STRING_LENGTH.
    const char* stream_name_str;
} CdiTxConfigDataStream;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of an AVM transmitter. When the instance is no longer needed, use the CdiCoreConnectionDestroy()
 * API function to free-up resources that are being used by it.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param config_data_ptr Pointer to transmitter configuration data. Copies of the data in the configuration data
 *                        structure are made as needed. A local variable can be used for composing the structure since
 *                        its contents are not needed after this function returns.
 * @param tx_cb_ptr Address of the user function to call whenever a payload has been transmitted or a transmit timeout
 *                  error has occurred.
 * @param ret_handle_ptr Pointer to returned connection handle. The handle is used as a parameter to other API functions
 *                       to identify this specific transmitter.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmTxCreate(CdiTxConfigData* config_data_ptr, CdiAvmTxCallback tx_cb_ptr,
                                             CdiConnectionHandle* ret_handle_ptr);

/**
 * Create an instance of an AVM transmitter that uses multiple stream endpoints. A stream identifier value is used to
 * uniquely identify each stream. Payloads are transmitted using the CdiAvmEndpointTxPayload() API function, which
 * contains the stream identifier. The value determines which matching endpoint to use to transmit the payload. This API
 * function only creates instance data for the connection. Use the CdiAvmTxStreamEndpointCreate() and
 * CdiAvmStreamEndpointDestroy() API functions to dynamically create and destroy stream endpoints associated with this
 * connection. When the instance is no longer needed, use the CdiCoreConnectionDestroy() API function to free-up
 * resources that are being used by it.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param config_data_ptr Pointer to transmitter configuration data. Copies of the data in the configuration data
 *                        structure are made as needed. A local variable can be used for composing the structure since
 *                        its contents are not needed after this function returns. NOTE: Within the structure,
 *                        dest_ip_addr_str and dest_port are only used for generating the name of the connection if one
 *                        was not provided. The IP and port are defined as part of the configuration data passed to
 *                        CdiAvmTxStreamConnectionCreate(), when creating streams.
 * @param tx_cb_ptr Address of the user function to call whenever a payload has been transmitted or a transmit timeout
 *                  error has occurred.
 * @param ret_handle_ptr Pointer to returned connection handle. The handle is used as a parameter to other API functions
 *                       to identify this specific transmitter.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmTxStreamConnectionCreate(CdiTxConfigData* config_data_ptr,
                                                             CdiAvmTxCallback tx_cb_ptr,
                                                             CdiConnectionHandle* ret_handle_ptr);

/**
 * Create an instance of an AVM stream endpoint that is associated with the specified stream connection.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param handle Stream connection handle returned by a previous call to CdiAvmTxStreamConnectionCreate().
 * @param stream_config_ptr Pointer to stream configuration data. Copies of the data in this structure are made as
 *                          needed.
 * @param ret_handle_ptr Pointer to returned endpoint handle. The handle is used as a parameter to other API functions
 *                       to identify this specific stream endpoint.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmTxStreamEndpointCreate(CdiConnectionHandle handle,
                                                           CdiTxConfigDataStream* stream_config_ptr,
                                                           CdiEndpointHandle* ret_handle_ptr);

/**
 * Destroy a specific AVM stream endpoint and free resources that were created for it.
 *
 * @param handle Connection handle returned by the CdiAvmTxStreamEndpointCreate() API function.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmStreamEndpointDestroy(CdiEndpointHandle handle);

/**
 * Create an instance of an AVM receiver. When done, must call CdiCoreConnectionDestroy().
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
 */
CDI_INTERFACE CdiReturnStatus CdiAvmRxCreate(CdiRxConfigData* config_data_ptr, CdiAvmRxCallback rx_cb_ptr,
                                             CdiConnectionHandle* ret_handle_ptr);

/**
 * Transmit a payload of data to the receiver. The connection must have been created with CdiAvmTxCreate(). Connections
 * that were created by calling CdiAvmTxStreamConnectionCreate() must use CdiAvmEndpointTxPayload() instead. This
 * function is asynchronous and will immediately return. The user callback function CdiAvmTxCallback() registered
 * through CdiAvmTxCreate() will be invoked when the payload has been acknowledged by the remote receiver or a
 * transmission timeout occurred.
 *
 * MEMORY NOTE: The payload_config_ptr, avm_config_ptr, CdiSgList and SGL entries memory can be modified or released
 * immediately after the function returns. However, the buffers pointed to in the SGL must not be modified or released
 * until after the CdiAvmTxCallback() has occurred.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param con_handle Connection handle returned by a previous call to CdiAvmTxCreate().
 * @param payload_config_ptr Pointer to payload configuration data. Part of the data is sent along with the payload and
 *                           part is provided to the registered user Tx callback function.
 * @param avm_config_ptr Pointer to configuration data that describes the contents of this payload and subsequent
 *                       payloads. The first time this function is called for a given stream identifier (in
 *                       payload_config_ptr->avm_extra_data) after the connection's status has changed to
 *                       kCdiConnectionStatusConnected (reported to the transmit callback function), a value must be
 *                       specified so the receiver can identify the format of the payload data. Afterwards, NULL shall
 *                       be specified unless some aspect of the configuration for this stream has changed since the
 *                       previous payload was transmitted.
 * @param sgl_ptr Scatter-gather list containing the data to be transmitted. The addresses in the SGL must point to
 *                locations that reside within the memory region specified in CdiAdapterData at ret_tx_buffer_ptr.
 * @param max_latency_microsecs Maximum latency in microseconds. If this value is specified as 0, there will be no
 *                              warning messages generated for late payloads. If a value is specified, and the
 *                              transmission time of a payload exceeds this value, the CdiAvmTxCallback() API function
 *                              will be invoked with an error.
 *
 * @return A value from the CdiReturnStatus enumeration. kCdiStatusInvalidHandle will be returned if the connection
 *         handle was returned by CdiAvmTxStreamEndpointCreate() instead of CdiAvmTxCreate().
 */
CDI_INTERFACE CdiReturnStatus CdiAvmTxPayload(CdiConnectionHandle con_handle,
                                              const CdiAvmTxPayloadConfig* payload_config_ptr,
                                              const CdiAvmConfig* avm_config_ptr, const CdiSgList* sgl_ptr,
                                              int max_latency_microsecs);

/**
 * Transmit a payload of data to a remote endpoint. Endpoint handles are obtained through
 * CdiAvmTxStreamConnectionCreate(). This function is asynchronous and will immediately return. The user callback
 * function CdiAvmTxCallback() registered through CdiAvmTxStreamConnectionCreate() will be invoked when the payload has
 * been acknowledged by the remote receiver or a transmission timeout occurred.
 *
 * MEMORY NOTE: The payload_config_ptr, avm_config_ptr, CdiSgList and SGL entries memory can be modified or released
 * immediately after the function returns. However, the buffers pointed to in the SGL must not be modified or released
 * until after the CdiAvmTxCallback() has occurred.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param endpoint_handle Connection handle returned by a previous call to CdiAvmTxStreamEndpointCreate().
 * @param payload_config_ptr Pointer to payload configuration data. Part of the data is sent along with the payload and
 *                           part is provided to the registered user TX callback function.
 * @param avm_config_ptr Pointer to configuration data that describes the contents of this payload and subsequent
 *                       payloads. The first time this function is called for a given stream_id (in
 *                       payload_config_ptr->avm_extra_data) in the connection, a value must be specified so the
 *                       receiver can identify the format of the payload data. Afterwards, NULL shall be specified
 *                       unless some aspect of the configuration for this stream has changed since the previous payload
 *                       was transmitted.
 * @param sgl_ptr Scatter-gather list containing the data to be transmitted. The addresses in the SGL must point to
 *                locations that reside within the memory region specified in CdiAdapterData at ret_tx_buffer_ptr.
 * @param max_latency_microsecs Maximum latency in microseconds. If the transmission time of a payload exceeds this
 *                              value, the CdiAvmTxCallback() API function will be invoked with an error.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmEndpointTxPayload(CdiEndpointHandle endpoint_handle,
                                                      const CdiAvmTxPayloadConfig* payload_config_ptr,
                                                      const CdiAvmConfig* avm_config_ptr, const CdiSgList* sgl_ptr,
                                                      int max_latency_microsecs);

#endif // CDI_AVM_API_H__
