// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in endpoint_manager.c.
 */

#ifndef ENDPOINT_MANAGER_H__
#define ENDPOINT_MANAGER_H__

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_core_api.h"
#include "cdi_os_api.h"
#include "private.h"
#include "protocol.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Type used as the handle (pointer to an opaque structure) for Endpoint Manager global data.
 */
typedef struct EndpointManagerGlobalState* EndpointManagerGlobalHandle;

/**
 * @brief Type used as the handle (pointer to an opaque structure) for a Endpoint Manager. Each handle represents
 * a instance of a Endpoint Manager associated with a connection.
 */
typedef struct EndpointManagerState* EndpointManagerHandle;

/**
 * @brief This enumeration is used in the EndpointManagerState structure to indicate what type of endpoint command to
 * process.
 */
typedef enum {
    kEndpointStateIdle,       ///< Endpoint Manager is idle. Nothing special to do.
    kEndpointStateReset,      ///< Reset the endpoint.
    kEndpointStateStart,      ///< Start the endpoint.
    kEndpointStateShutdown,   ///< Shutdown the endpoint.
} EndpointManagerCommand;

/// Forward reference of structure to create pointers later.
typedef struct CdiConnectionState CdiConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct AdapterEndpointState AdapterEndpointState;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Create an instance of the Endpoint Manager for the specified connection.
 *
 * @param handle Handle of connection.
 * @param stats_cb_ptr Address of stats callback function.
 * @param stats_user_cb_param User-defined parameter in structure passed to stats_cb_ptr.
 * @param stats_config_ptr Pointer to statistics configuration data.
 * @param ret_handle_ptr Address where to write returned Endpoint Manager handle.
 *
 * @return kCdiStatusOk if the operation was successful or a value that indicates the nature of the failure.
 */
CdiReturnStatus EndpointManagerCreate(CdiConnectionHandle handle, CdiCoreStatsCallback stats_cb_ptr,
                                      CdiUserCbParameter stats_user_cb_param,
                                      const CdiStatsConfigData* stats_config_ptr,
                                      EndpointManagerHandle* ret_handle_ptr);

/**
 * Destroys the resources used by the instance of the specified Endpoint Manager.
 *
 * @param handle Handle of Endpoint Manager.
 */
void EndpointManagerDestroy(EndpointManagerHandle handle);

/**
 * Return the Endpoint Manager associated with the specified connection.
 *
 * @param handle Handle of the connection.
 *
 * @return Endpoint Manager handle.
 */
EndpointManagerHandle EndpointManagerConnectionToEndpointManager(CdiConnectionHandle handle);

/**
 * Copy the specified information about the remote endpoint to the internal state data of the provided endpoint.
 *
 * @param handle Handle of endpoint.
 * @param remote_address_ptr Pointer to remote address (sockaddr_in)
 * @param stream_name_str Pointer to endpoint stream name. If the stream name is NULL, then a '\0' will be stored.
 */
void EndpointManagerRemoteEndpointInfoSet(CdiEndpointHandle handle, const struct sockaddr_in* remote_address_ptr,
                                          const char* stream_name_str);

/**
 * Get the stream name related to the provided endpoint. If the stream name empty, then NULL is returned.
 *
 * @param handle Handle of endpoint.
 *
 * @return Pointer to stream name or NULL if empty.
 */
const char* EndpointManagerEndpointStreamNameGet(CdiEndpointHandle handle);

/**
 * Get the remote IP address related to the provided endpoint.
 *
 * @param handle Handle of endpoint.
 *
 * @return Pointer to IP address string.
 */
const char* EndpointManagerEndpointRemoteIpGet(CdiEndpointHandle handle);

/**
 * Get the remote port related to the provided endpoint.
 *
 * @param handle Handle of endpoint.
 *
 * @return Remote port as integer value.
 */
int EndpointManagerEndpointRemotePortGet(CdiEndpointHandle handle);

/**
 * Get the remote address structure (sockaddr_in) related to the provided endpoint.
 *
 * @param handle Handle of endpoint.
 *
 * @return Pointer to remote address structure.
 */
const struct sockaddr_in* EndpointManagerEndpointRemoteAddressGet(CdiEndpointHandle handle);

/**
 * Create resources used for a new Tx endpoint and add it to the list of endpoints managed by the specified Endpoint
 * Manager.
 *
 * @param handle Handle of Endpoint Manager.
 * @param is_multi_stream True if the Tx endpoint is going to be used by multiple Tx streams.
 * @param dest_ip_addr_str Pointer to destination IP address string.
 * @param dest_port Destination port.
 * @param stream_name_str Pointer to stream name string.
 * @param ret_endpoint_handle_ptr Address where to write the handle of the new endpoint.
 *
 * @return kCdiStatusOk if the operation was successful or a value that indicates the nature of the failure.
 */
CdiReturnStatus EndpointManagerTxCreateEndpoint(EndpointManagerHandle handle, bool is_multi_stream,
                                                const char* dest_ip_addr_str, int dest_port,
                                                const char* stream_name_str,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr);

/**
 * Create resources used for a new Rx endpoint and add it to the list of endpoints managed by the specified Endpoint
 * Manager.
 *
 * @param handle Handle of Endpoint Manager.
 * @param dest_port Destination port.
 * @param ret_endpoint_handle_ptr Address where to write the handle of the new endpoint.
 *
 * @return kCdiStatusOk if the operation was successful or a value that indicates the nature of the failure.
 */
CdiReturnStatus EndpointManagerRxCreateEndpoint(EndpointManagerHandle handle, int dest_port,
                                                CdiEndpointHandle* ret_endpoint_handle_ptr);

/**
 * @brief Set the protocol version for the specified endpoint. The protocol version actually used is negotiated using
 * the specified remote version and the current version of the CDI-SDK.
 *
 * @param handle Handle of CDI endpoint.
 * @param remote_version_ptr Pointer to remote protocol version data.
 *
 * @return kCdiStatusOk if the operation was successful or a value that indicates the nature of the failure.
 */
CdiReturnStatus EndpointManagerProtocolVersionSet(CdiEndpointHandle handle,
                                                  const CdiProtocolVersionNumber* remote_version_ptr);
/**
 * Returns the first endpoint in the list of endpoints associated with the specified Endpoint Manager.
 *
 * @param handle Handle of CDI endpoint.
 *
 * @return Handle of first endpoint in the list. Returns NULL if the list is empty.
 */
CdiEndpointHandle EndpointManagerGetFirstEndpoint(EndpointManagerHandle handle);

/**
 * Returns the next endpoint in the list of endpoints associated with the specified CDI endpoint.
 *
 * @param handle Handle of CDI endpoint.
 *
 * @return Handle of next endpoint in the list. Returns NULL if no more endpoints.
 */
CdiEndpointHandle EndpointManagerGetNextEndpoint(CdiEndpointHandle handle);

/**
 * Return the number of endpoints associated with the specified Endpoint Manager.
 *
 * @param handle Handle of Endpoint Manager.
 *
 * @return Number of endpoints.
 */
int EndpointManagerEndpointGetCount(EndpointManagerHandle handle);

/**
 * Return the adapter endpoint related to the specified CDI endpoint.
 *
 * @param handle Handle of CDI endpoint.
 *
 * @return Handle of adapter endpoint.
 */
AdapterEndpointHandle EndpointManagerEndpointToAdapterEndpoint(CdiEndpointHandle handle);

/**
 * Register a thread with the specified Endpoint Manager. This should be called once, at the start of each thread that
 * uses resources associated with the connection. The Endpoint Manager keeps track of how many times this function
 * is used (how many threads). When the Endpoint Manager receives a request via one of the Queue API functions, it will
 * wait until the number of threads have called EndpointManagerThreadWait() and are blocked. Then, it can carry out the
 * request and unblock the waiting threads after the request has completed.
 *
 * @param handle Handle of Endpoint Manager.
 * @param thread_name_str Name of thread.
 *
 * @return Returns the notification signal. Same value returned from EndpointManagerGetNotificationSignal().
 */
CdiSignalType EndpointManagerThreadRegister(EndpointManagerHandle handle, const char* thread_name_str);

/**
 * For all threads that have used EndpointManagerThreadRegister(), each must call this function whenever the signal
 * obtained using EndpointManagerGetNotificationSignal() is set (it is set whenever one of the Queue API functions are
 * used). This will block the thread until all registered threads have called this function. The threads are blocked
 * until the pending state change request has completed.
 *
 * @param handle Handle of Endpoint Manager.
 */
void EndpointManagerThreadWait(EndpointManagerHandle handle);

/**
 * Perform Endpoint Manager polling and determine if adapter level poll APIs should be used or not.
 *
 * @param handle_ptr On entry, Address to handle of endpoint. On exit, handle of next endpoint is written to to address.
 *
 * @return true if poll thread should invoke adapter poll APIs, false if it should not.
 */
bool EndpointManagerPoll(CdiEndpointHandle* handle_ptr);

/**
 * Called by Poll thread when it is about to exit.
 *
 * @param handle Handle of Endpoint Manager.
 */
void EndpointManagerPollThreadExit(EndpointManagerHandle handle);

/**
 * Return the signal that is used to notify registered threads that they must call EndpointManagerThreadWait() so a
 * queued state change can be processed.
 *
 * @param handle Handle of endpoint.
 *
 * @return Returns the notification signal.
 */
CdiSignalType EndpointManagerGetNotificationSignal(EndpointManagerHandle handle);

/**
 * Queue a request to reset the endpoint associated with the specified Endpoint Manager.
 *
 * @param handle Handle of endpoint.
 */
void EndpointManagerQueueEndpointReset(CdiEndpointHandle handle);

/**
 * Queue a request to start the endpoint associated with the specified Endpoint Manager.
 *
 * @param handle Handle of endpoint.
 */
void EndpointManagerQueueEndpointStart(CdiEndpointHandle handle);

/**
 * Destroy the specified endpoint. Thread will block until the endpoint is destroyed by EndpointManagerPoll().
 *
 * @param handle Handle of CDI endpoint.
 */
void EndpointManagerEndpointDestroy(CdiEndpointHandle handle);

/**
 * Shutdown the connection associated with the specified Endpoint Manager. This will block until threads registered
 * using EndpointManagerThreadRegister() have called EndpointManagerThreadWait() and the connection shutdown has
 * completed.
 *
 * @param handle Handle of Endpoint Manager.
 */
void EndpointManagerShutdownConnection(EndpointManagerHandle handle);

/**
 * Return true if the connection is in the process of being shutdown.
 *
 * @param handle Handle of Endpoint Manager.
 *
 * @return Returns true if the connection is in the process of being shutdown, otherwise false is returned.
 */
bool EndpointManagerIsConnectionShuttingDown(EndpointManagerHandle handle);

/**
 * Notify the application of a connection state change using the user registered connection callback function, if the
 * state has actually changed.
 *
 * NOTE: This function is called from ProbeControlThread().
 *
 * @param handle Handle of endpoint.
 * @param status_code Connection status code (connected or disconnected).
 * @param error_msg_str Pointer to optional error message string to provide to the user registered callback function.
 */
void EndpointManagerConnectionStateChange(CdiEndpointHandle handle, CdiConnectionStatus status_code,
                                          const char* error_msg_str);

#endif // ENDPOINT_MANAGER_H__
