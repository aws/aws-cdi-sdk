// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in payload.c.
 */

#ifndef CDI_PAYLOAD_H__
#define CDI_PAYLOAD_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "private_avm.h"
#include "cdi_core_api.h"
#include "cdi_pool_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Enumeration used to identify packet type.
 */
typedef enum {
    kPayloadTypeData = 0,   ///< Payload contains application payload data.
    kPayloadTypeDataOffset, ///< Payload contains application payload data with data offset field in each packet.
    kPayloadTypeProbe,      ///< Payload contains probe data.
    kPayloadTypeKeepAlive,  ///< Payload is being used for keeping the connection alive (don't use app payload
                            ///  callbacks).
} CdiPayloadType;

/**
 * @brief Structure used to hold state data for a single payload.
 */
typedef struct {
    CdiPayloadType payload_type;         ///< Payload type (application or keep alive).
    uint16_t maximum_packet_byte_size;   ///< Maximum size of packets in bytes.
    uint8_t maximum_tx_sgl_entries;      ///< Maximum number of SGL entries for a packet.

    uint16_t payload_num;                ///< Payload number. Value is unique for each Tx connection and increments by 1
                                         ///  for each payload transmitted.

    uint16_t packet_sequence_num;        ///< Current CDI packet sequence number.
    uint32_t packet_id;                  ///< Current CDI packet ID.
    uint16_t packet_payload_data_size;   ///< Size in bytes of payload data in current CDI packet.

    const CdiSglEntry* source_entry_ptr; ///< Current source payload SGL entry being used.
    int source_entry_address_offset;     ///< Current source entry address offset of entry being used. Only used if the
                                         ///  data size of the source SGL entry is larger than the CDI packet data
                                         ///  size (the SGL entry spans more than 1 CDI packet).
    uint32_t payload_data_offset;        ///< Current offset of payload data.
} CdiPayloadPacketState;

/// An opaque type for the packetizer to keep track of its progress in case it must be suspended for lack of resources.
typedef struct CdiPacketizerState* CdiPacketizerStateHandle;

/// Forward reference of structure to create pointers later.
typedef struct CdiConnectionState CdiConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct TxPayloadState TxPayloadState;
/// Forward reference of structure to create pointers later.
typedef struct CdiRawPacketHeader CdiRawPacketHeader;
/// Forward reference of structure to create pointers later.
typedef struct CdiProtocol CdiProtocol;
/// Forward reference of structure to create pointers later.
typedef struct CdiProtocol* CdiProtocolHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize an CdiPayloadPacketState structure before using CdiPayloadPacketizerPacketGet() to split the payload
 * into packets. NOTE: If an error occurs, caller is responsible for freeing the pool buffers that it allocates.
 *
 * @param con_state_ptr Pointer to connection state data.
 * @param source_sgl_ptr Pointer to Tx Payload source SGL list.
 * @param payload_state_ptr Pointer to payload state data.
 *
 * @return true if successful, otherwise an error occurred.
 */
bool PayloadInit(CdiConnectionState* con_state_ptr, const CdiSgList* source_sgl_ptr,
                 TxPayloadState* payload_state_ptr);

/**
 * Creates a packetizer state object. This must be destroyed with CdiPayloadPacketizerDestroy() when the connection
 * is closed.
 *
 * @return Handle for the created packetizer state or NULL if the creation failed.
 */
CdiPacketizerStateHandle PayloadPacketizerCreate(void);

/**
 * Frees the memory previously allocated for a packetizer state object through CdiPayloadPacketizerCreate().
 *
 * @param packetizer_state_handle The handle of the packetizer state to be destroyed.
 */
void PayloadPacketizerDestroy(CdiPacketizerStateHandle packetizer_state_handle);

/**
 * Initializes a packetizer state object. This function should be called before calling CdiPayloadPacketizerPacketGet()
 * the first time for a given payload.
 *
 * @param packetizer_state_handle Handle of packetizer object.
 */
void PayloadPacketizerStateInit(CdiPacketizerStateHandle packetizer_state_handle);

/**
 * Get the next packet for a payload. Must use CdiPayloadPacketizerStateInit() for a new payload before using this
 * function. If false is returned, one of the pools from which required resources are taken is dry so this function
 * should be called again until it returns true.
 *
 * NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing them
 * at a time.
 *
 * @param protocol_handle Handle of protocol to use.
 * @param packetizer_state_handle Handle of the packetizer state for this connection.
 * @param header_ptr Pointer to the header data structure to be filled in for the new packet.
 * @param packet_sgl_entry_pool_handle CDI packet SGL list entry pool.
 * @param payload_state_ptr Pointer to payload state data.
 * @param packet_sgl_ptr Pointer to returned packet SGL list
 * @param ret_is_last_packet_ptr Pointer to returned last packet state. True if last packet, otherwise false.
 *
 * @return true if packet returned, otherwise a pool was empty so false is returned.
 */
bool PayloadPacketizerPacketGet(CdiProtocolHandle protocol_handle, CdiPacketizerStateHandle packetizer_state_handle,
                                char* header_ptr, CdiPoolHandle packet_sgl_entry_pool_handle,
                                TxPayloadState* payload_state_ptr, CdiSgList* packet_sgl_ptr,
                                bool* ret_is_last_packet_ptr);

#endif  // CDI_PAYLOAD_H__
