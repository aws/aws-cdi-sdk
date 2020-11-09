// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

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
 * The Cloud Digital Interface Audio, Video and Metadata (CDI_AVM) is the library which implements the
   low-latency reliable transport of audio, video and metadata between EC2 instances within the Amazon network.
 *
 * @section avm_high_level_arch CDI-AVM Architecture Overview
 *
 * The diagram shown below provides an overview of the CDI-AVM Transmit/Receive architecture.
 *
 * @image html "high_level_architecture.jpg"
 *
 * @section ec2_usage_avm CDI-AVM EC2 Instance Workflow Example
 *
 * The diagram shown below provides an example of using the CDI-AVM API on multiple EC2 instances and multiple TX/RX
 * connections. Each connection supports multiple video, audio and ancillary data streams that are identified by a
 * "stream_identifier" as defined in the API.
 *
 * @image html "avm_ec2_usage_example.jpg"
 *
 * @section avm_main_api CDI-AVM Application Programming Interface (API)
 *
 * The API is declared in: @ref cdi_avm_api.h
 *
 * The diagram shown below provides an example of the typical CDI-AVM TX/RX workflow using the CDI-AVM API.
 *
 * @image html "avm_api_workflow.jpg"
 *
 * The AVM interface of the CDI SDK is intended mainly for interoperabilty among vendors while remaining extensible.
 * To better encourage interoperability, a constrained set of audio, video, and ancillary data formats is supported.
 * Audio, for example, is always 24 bit linear PCM in big endian format. Ancillary data follows IETF RFC 8331. Video
 * similarly has a narrow set of supported parameters. Together, these comprise the CDI baseline profile.
 *
 * Extensibility is addressed through a generic configuration mechanism which is used even for the CDI baseline
 * profile. It is based on a structure containing a URI and optional parameter data. The URI is defined such that it
 * ensures uniqueness and optionally points to documentation on how to interpret the parameter data. The format of the
 * payload data is also dependent on the URI. Helper functions ease the process of creating and parsing the generic
 * configuration structure for the CDI baseline profile.
 *
 * The URIs used for the CDI baseline profile are:
 * https://cdi.elemental.com/specs/baseline-audio
 * https://cdi.elemental.com/specs/baseline-video
 * https://cdi.elemental.com/specs/baseline-ancillary-data
 *
 * The documents at those URIs fully specify the various aspects of each media type including the parameter data and the
 * in memory representation of payload data. These files also reside in CDI_SDK/doc/specs.
 *
 * Since the media format details are specificated outside of the SDK, new formats (beyond the CDI baseline profile)
 * can be added without changing the SDK. They can be publicly documented or they can remain private for situations
 * where interoperability is not required.
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
 * @brief Stream configuration data used by the CdiAvmTxCreateStreams() API.
 */
typedef struct {
    /// @brief Used to identify the data stream number associated with this Tx stream. This allows multiple streams to
    /// be carried on a single connection and be uniquely transmitted to a unique IP and port.
    uint16_t stream_identifier;

    /// @brief The IP address of the host which is to receive the flow from this transmit stream. NOTE: This must be the
    /// dotted form of an IPv4 address. DNS may be supported in the future.
    const char* dest_ip_addr_str;

    /// @brief The port number to use at the receiving host. The range of valid values is 1 to 65535, inclusive and must
    /// match the value configured for the receiving connection.
    int dest_port;

    /// @brief Pointer to name of the stream. It is used as an identifier when generating log messages that are specific
    /// to this stream. If NULL, a name is internally generated. Length of name must not exceed
    /// MAX_STREAM_NAME_STRING_LENGTH.
    const char* stream_name_str;
} CdiTxConfigDataStream;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

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
 * Destroy a specific AVM stream endpoint and free resources that were created for it.
 *
 * @param handle Connection handle returned by the CdiAvmTxCreateStream() API.
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
 * Transmit a payload of data to the receiver. This function is asynchronous and will immediately return. The user
 * callback function CdiAvmTxCallback() registered through CdiAvmTxCreate() will be invoked when the payload has been
 * acknowledged by the remote receiver or a transmission timeout occurred.
 *
 * MEMORY NOTE: The payload_config_ptr, avm_config_ptr, CdiSgList and SGL entries memory can be modified or released
 * immediately after the function returns. However, the the buffers pointed to in the SGL must not be modified or
 * released until after the CdiAvmTxCallback() has occurred.
 *
 * NOTE: Newly created data structures that are passed in to this function should be properly initialized before being
 * programmed with user values. Use memset or a zero structure initializer (= {0}) to set the whole structure to zero
 * before setting the desired members to the actual values required.
 *
 * @param con_handle Connection handle returned by a previous call to CdiAvmTxCreate().
 * @param payload_config_ptr Pointer to payload configuration data. Part of the data is sent along with the payload and
 *                           part is provided to the registered user TX callback function.
 * @param avm_config_ptr Pointer to configuration data that describes the contents of this payload and subsequent
 *                       payloads. The first time this function is called for a given stream_id (in
 *                       payload_config_ptr->avm_extra_data) after the connection's status has changed to
 *                       kCdiConnectionStatusConnected (reported to the transmit callback function), a value must be
 *                       specified so the receiver can identify the format of the payload data. Afterwards, NULL shall
 *                       be specified unless some aspect of the configuration for this stream has changed since the
 *                       previous payload was transmitted.
 * @param sgl_ptr Scatter-gather list containing the data to be transmitted. The addresses in the SGL must point to
 *                locations that reside within the memory region specified in CdiAdapterData at ret_tx_buffer_ptr.
 * @param max_latency_microsecs Maximum latency in microseconds. If the transmission time of a payload exceeds this
 *                              value, the CdiAvmTxCallback() API function will be invoked with an error.
 *
 * @return A value from the CdiReturnStatus enumeration.
 *
 * @see CdiRawTxPayload() for additional detail about memory management.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmTxPayload(CdiConnectionHandle con_handle,
                                              const CdiAvmTxPayloadConfig* payload_config_ptr,
                                              const CdiAvmConfig* avm_config_ptr, const CdiSgList* sgl_ptr,
                                              int max_latency_microsecs);

#ifdef __cplusplus
}
#endif

#endif // CDI_AVM_API_H__
