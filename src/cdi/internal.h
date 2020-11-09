// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in internal.c.
 */

#ifndef CDI_INTERNAL_H__
#define CDI_INTERNAL_H__

#include <assert.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "adapter_api.h"
#include "cdi_core_api.h"
#include "cdi_log_api.h"
#include "cdi_raw_api.h"
#include "internal_log.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Function to check if a Tx is valid.
 *
 * @param handle Pointer to Tx being checked.
 */
static inline bool IsValidTxHandle(const CdiConnectionHandle handle)
{
    return handle != NULL && handle->handle_type == kHandleTypeTx && handle->magic == kMagicCon &&
           handle->adapter_state_ptr != NULL;
}

/**
 * Function to check if an Rx is valid.
 *
 * @param handle Pointer to Rx being checked.
 */
static inline bool IsValidRxHandle(const CdiConnectionHandle handle)
{
    return handle != NULL && handle->handle_type == kHandleTypeRx && handle->magic == kMagicCon
           && handle->adapter_state_ptr != NULL;
}

/**
 * Function to check if a connection is valid.
 *
 * @param handle Pointer to connection being checked.
 */
static inline bool IsValidConnectionHandle(const CdiConnectionHandle handle)
{
    // with -O2 or higher, GCC optimizes to only a single check of magic
    return (IsValidTxHandle(handle) || IsValidRxHandle(handle));
}

/**
 * Function to check if a memory handle is valid.
 *
 * @param handle Pointer to memory handle being checked.
 */
static inline bool IsValidMemoryHandle(const CdiMemoryHandle handle)
{
    return handle != NULL && handle->magic == kMagicMem;
}

/**
 * Performs initialization of the SDK that only needs to be done once and applies to programs that transmit, receive,
 * or both transmit and receive.
 *
 * @param core_config_ptr Pointer to core configuration data that is used to initalize the SDK. Value is required and
 * cannot be NULL.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CdiGlobalInitialization(const CdiCoreConfigData* core_config_ptr);

/**
 * This is the implementation behind the API function CdiGather(). It is located in a private implementation file
 * since it references names we wish to hide from cdiAPI.c.
 *
 * @param sgl_ptr The scatter-gather list containing the data to be gathered.
 * @param offset Number of bytes to skip in SGL before starting the copy.
 * @param dest_data_ptr Where to write the gathered data in linear format.
 * @param byte_count The number of bytes to copy.
 *
 * @return The number of bytes copied. This will be less than byte_count if fewer than that number of bytes are present
 *         in the source SGL starting from the specified offset. A value of -1 indicates that a fatal error was
 *         encountered.
 */
int CdiGatherInternal(const CdiSgList* sgl_ptr, int offset, void* dest_data_ptr, int byte_count);

/**
 * Initialize an adapter.
 *
 * @param adapter_data_ptr Address of a structure containing all of the pertinent information required to perform the
 *                         initialization.
 * @param ret_handle_ptr Pointer to a location in memory to place the handle to the initialized adapter.
 *
 * @return CdiReturnStatus kCdiStatusOk if all went well, otherwise a value indicating why it failed.
 *
 * @see CdiCoreNetworkAdapterInitialize
 */
CdiReturnStatus AdapterInitializeInternal(CdiAdapterData* adapter_data_ptr, CdiAdapterHandle* ret_handle_ptr);

/**
 * Shuts down a connection and frees all of the resources associated with it.
 *
 * @param handle The handle of the connection to shut down.
 */
void ConnectionDestroyInternal(CdiConnectionHandle handle);

/**
 * Shuts down a endpoint and frees all of the resources associated with it.
 *
 * @param handle The handle of the endpoint to shut down.
 */
void EndpointDestroyInternal(CdiEndpointHandle handle);

/**
 * Shut down the SDK, freeing all of the resources that were allocated by it.
 *
 * @return CdiReturnStatus kCdiStatusOk if SDK was shut down cleanly, otherwise the value indicates the nature of the
 *         failure.
 */
CdiReturnStatus SdkShutdownInternal(void);

/**
 * Create connection resources that are common to both Tx and Rx connection types. These are OS level resources that
 * must be created prior to the creation of any child threads related to this connection.
 *
 * @param handle The handle of the connection being created.
 * @param stats_cb_ptr Address of stats callback function.
 * @param stats_user_cb_param User-defined parameter in structure passed to stats_cb_ptr.
 * @param stats_config_ptr Pointer to statistics configuration data.
 *
 * @return CdiReturnStatus kCdiStatusOk if resources were successfully created, otherwise the value indicates the
 *         nature of the failure.
 */
CdiReturnStatus ConnectionCommonResourcesCreate(CdiConnectionHandle handle, CdiCoreStatsCallback stats_cb_ptr,
                                                CdiUserCbParameter stats_user_cb_param,
                                                const CdiStatsConfigData* stats_config_ptr);

/**
 * Destroy connection resources that are common to both Tx and Rx connection types. These are OS level resources that
 * should be destroyed after all child threads related to this connection have finished using resources.
 *
 * @param handle The handle of the connection being destroyed.
 */
void ConnectionCommonResourcesDestroy(CdiConnectionHandle handle);

/**
 * Create connection packet message thread that is common to both Tx and Rx connection types.
 *
 * @param handle The handle of the connection being created.
 *
 * @return CdiReturnStatus kCdiStatusOk if the thread was successfully created, otherwise the value indicates the
 *         nature of the failure.
 */
CdiReturnStatus ConnectionCommonPacketMessageThreadCreate(CdiConnectionHandle handle);

/**
 * Configure transfer statistics.
 *
 * @param handle The handle of the connection to set statistics configuration.
 * @param new_config_ptr Pointer to new statistics configuration data.
 * @param force_changes If true, settings are applied (used to set initial values), otherwise settings are only applied
 *                      if any of their values change.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus CoreStatsConfigureInternal(CdiConnectionHandle handle, const CdiStatsConfigData* new_config_ptr,
                                           bool force_changes);

/**
 * Macro used to set error message string in the specified payload callback. The error message is made available to the
 * application through the user-registered Tx/Rx payload callback functions.
 *
 * @param con_state_ptr Pointer to connection state data (CdiConnectionState).
 * @param app_cb_data_ptr Pointer to payload callback data (AppPayloadCallbackData).
 * @param status_code Status code to set (CdiReturnStatus).<br>
 * The remaining parameters contain a variable length list of arguments to generate the error message.
 */
#define PAYLOAD_ERROR(con_state_ptr, app_cb_data_ptr, status_code, ...) { \
    PayloadErrorSet(con_state_ptr, app_cb_data_ptr, status_code, __VA_ARGS__); \
    CdiLogger(CdiLoggerThreadLogGet(), kLogComponentGeneric, kLogError, __FUNCTION__, __LINE__ , __VA_ARGS__); }

/**
 * Set error message string in the specified payload callback. The error message is made available to the application
 * through the user-registered Tx/Rx payload callback functions.
 *
 * NOTE: Should not be used directly. Use the PAYLOAD_ERROR() macro instead.
 *
 * @param con_state_ptr Pointer to connection state data (CdiConnectionState).
 * @param app_cb_data_ptr Pointer to payload callback data (AppPayloadCallbackData).
 * @param status_code Status code to set (CdiReturnStatus).
 * @param format_str Pointer to string used for formatting the message.<br> The remaining parameters contain a variable
 * length list of arguments used by the format string to generate the error message.
 */
void PayloadErrorSet(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr,
                     CdiReturnStatus status_code, const char* format_str, ...);

/**
 * Free error message buffer, if it was used.
 *
 * @param con_state_ptr Pointer to connection state data (CdiConnectionState).
 * @param app_cb_data_ptr Pointer to payload callback data (AppPayloadCallbackData).
 */
void PayloadErrorFreeBuffer(CdiConnectionState* con_state_ptr, AppPayloadCallbackData* app_cb_data_ptr);

/**
 * Append a scatter-gather list entry to the tail of an SGL entry. N.B.: sgl_add_ptr should be a *single* entry
 * (that is, its next_ptr should be NULL) otherwise any chained entries may be lost. The SGL list's total_data_size is
 * updated accordingly.
 *
 * @param sgl_list_ptr Pointer to SGL list. If the head of the list is NULL, then the value is set to the new entry.
 *                     Otherwise the new entry is added to the current tail's next pointer and the tail is updated
 *                     to point to the new entry.
 * @param sgl_add_ptr  Pointer to new entry to add.
 */
static inline void SglAppend(CdiSgList* sgl_list_ptr, CdiSglEntry* sgl_add_ptr)
{
    if (NULL == sgl_list_ptr->sgl_head_ptr) {
        sgl_list_ptr->sgl_head_ptr = sgl_add_ptr;
        sgl_list_ptr->sgl_tail_ptr = sgl_add_ptr;
    } else {
        if (NULL == sgl_list_ptr->sgl_tail_ptr) {
            sgl_list_ptr->sgl_tail_ptr = sgl_list_ptr->sgl_head_ptr;
        }
        sgl_list_ptr->sgl_tail_ptr->next_ptr = sgl_add_ptr;
        sgl_list_ptr->sgl_tail_ptr = sgl_add_ptr;
    }
    sgl_list_ptr->total_data_size += sgl_add_ptr->size_in_bytes; // Update size of the SGL list.
}

/**
 * Append scatter-gather list's entries to the end of another list. This handles either or both lists containing zero or
 * more entries. The destination list's total_data_size is updated accordingly. src_sgl_ptr is empty on return.
 *
 * @param destination_sgl_ptr Pointer to an SGL which will assume the entries from src_sgl_ptr.
 * @param source_sgl_ptr Pointer to an SGL whose entries will be appended to the list at destination_sgl_ptr.
 */
static inline void SglMoveEntries(CdiSgList* destination_sgl_ptr, CdiSgList* source_sgl_ptr)
{
    // If the SGL has a head, it should also have a tail. Validate both SGL lists.
    if (destination_sgl_ptr->sgl_head_ptr) {
        assert(destination_sgl_ptr->sgl_tail_ptr);
    }
    if (source_sgl_ptr->sgl_head_ptr) {
        assert(source_sgl_ptr->sgl_tail_ptr);
    }

    if (destination_sgl_ptr->sgl_tail_ptr != NULL) {
        destination_sgl_ptr->sgl_tail_ptr->next_ptr = source_sgl_ptr->sgl_head_ptr;
    } else {
        if (destination_sgl_ptr->sgl_head_ptr == NULL) {
            destination_sgl_ptr->sgl_head_ptr =  source_sgl_ptr->sgl_head_ptr;
        } else {
            destination_sgl_ptr->sgl_head_ptr->next_ptr = source_sgl_ptr->sgl_head_ptr;
        }
    }
    destination_sgl_ptr->sgl_tail_ptr = source_sgl_ptr->sgl_tail_ptr;
    destination_sgl_ptr->total_data_size += source_sgl_ptr->total_data_size; // Update size of the destination list.
    memset(source_sgl_ptr, 0, sizeof(*source_sgl_ptr));
}

/**
 * Set a scatter-gather list as empty using a predefined static SGL entry. Used to clear an SGL that is passed to the
 * user application and later used internally. In this case, use of an empty SGL will not generate an error.
 *
 * @param sgl_ptr Pointer to an SGL to set as ampty.
 */
static inline void SglSetEmptyForExternalUse(CdiSgList* sgl_ptr)
{
    sgl_ptr->total_data_size = 0;
    sgl_ptr->sgl_head_ptr = &cdi_global_context.empty_sgl_entry;
    sgl_ptr->sgl_tail_ptr = sgl_ptr->sgl_head_ptr;
    sgl_ptr->internal_data_ptr = NULL;
}

/**
 * Free a list of scatter-gather list entries from a memory pool.
 *
 * @param pool_handle Handle of pool which contains the SGL entries to free.
 * @param sgl_entry_head_ptr Pointer to the head entry of the SGL entry list to free.
 *
 * @return bool True if entry was successfully freed, otherwise an error occurred.
 */
bool FreeSglEntries(CdiPoolHandle pool_handle, CdiSglEntry* sgl_entry_head_ptr);

/**
 * Dump to log payload configuration data. NOTE: kLogPAYLOAD_CONFIG must be enabled in the logger using
 * CdiFileLogEnable(), otherwise no messages will be generated.
 *
 * @param core_extra_data_ptr Pointer to core extra data.
 * @param extra_data_size Number of extra data bytes.
 * @param extra_data_array Pointer to extra data bytes.
 * @param protocol_type The type of connection protocol, kProtocolTypeRaw or kProtocolTypeAvm
 */
void DumpPayloadConfiguration(const CdiCoreExtraData* core_extra_data_ptr, int extra_data_size,
                              const uint8_t* extra_data_array, ConnectionProtocolType protocol_type);

/**
 * Convert an array of bytes to a hexadecimal string.
 *
 * @param data_ptr Pointer to array of bytes.
 * @param data_byte_count Number of data bytes.
 * @param dest_buffer_str Address where to write the hex string to.
 * @param dest_buffer_size Size of destination string buffer.
 */
void BytesToHexString(const void* data_ptr, int data_byte_count, char* dest_buffer_str, int dest_buffer_size);

/**
 * Convert a binary device GID to a string.
 *
 * @param device_gid_ptr Pointer to binary device GID.
 * @param gid_length Number of bytes in GID.
 * @param dest_buffer_str Address where to write the string to.
 * @param dest_buffer_size Size of destination string buffer.
 */
void DeviceGidToString(const char* device_gid_ptr, int gid_length, char* dest_buffer_str, int dest_buffer_size);

/**
 * Set shutdown signal and wait for thread to exit.
 *
 * @param thread_id Identifier of thread to wait for it to exit.
 * @param shutdown_signal The shutdown signal used to shutdown the thread.
 */
void SdkThreadJoin(CdiThreadID thread_id, CdiSignalType shutdown_signal);

#endif  // CDI_INTERNAL_H__
