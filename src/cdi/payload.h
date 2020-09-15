// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in payload.c.
 */

#ifndef CDI_PAYLOAD_H__
#define CDI_PAYLOAD_H__

#include <stdbool.h>
#include <stdint.h>

#include "private_avm.h"
#include "cdi_core_api.h"
#include "cdi_pool_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Enumeration used to maintain payload state.
 */
typedef enum {
    kPayloadIdle = 0,          ///< Payload state is not in use.
    kPayloadPacketZeroPending, ///< Payload is waiting for packet 0.
    kPayloadInProgress,        ///< Payload is in progress.
    kPayloadError,             ///< Payload received an error and has not yet been sent; transition to Ignore when sent.
    kPayloadIgnore,            ///< Error payload has been sent and we now ignore packets for it.
    kPayloadComplete,          ///< Payload has completed but has not been sent; transition to Idle when sent.
} CdiPayloadState;

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

// --------------------------------------------------------------------
// All structures in the block below are byte packed (no byte padding).
// --------------------------------------------------------------------
#pragma pack(push, 1)

/**
 * @brief CDI header for payload packets that don't use data offset values (payload type is not
 *        kPayloadTypeDataOffset).
 */
typedef struct {
    uint8_t payload_type;         ///< Payload type from CdiPayloadType.
    uint16_t packet_sequence_num; ///< Packet sequence number for the payload.
    uint8_t payload_num;          ///< Payload number this CDI packet is associated with.
} CdiCDIPacketCommonHeader;

/**
 * @brief CDI header for payload packets that contains a data offset value (payload type is kPayloadTypeDataOffset).
 */
typedef struct {
    CdiCDIPacketCommonHeader hdr; ///< Header that is common to all packets that contain a CDI header.
    uint32_t payload_data_offset;   ///< Current offset of payload data.
} CdiCDIPacketDataOffsetHeader;

/**
 * @brief CDI header for payload packet #0. This packet never uses payload_data_offset, since it is always zero.
 */
typedef struct {
    CdiCDIPacketCommonHeader hdr; ///< Header that is common to all packets that contain a CDI header.
    uint32_t total_payload_size;     ///< Total size of payload in bytes.
    uint64_t max_latency_microsecs;  ///< Maximum latency payload in microseconds.

    /// Origination RTP timestamp provided by transmitter that is related to the payload.
    CdiPtpTimestamp origination_ptp_timestamp;
    uint64_t payload_user_data;      ///< User data provided by transmitter that is related to the payload.

    uint16_t  extra_data_size;       ///< Size of additional header data in bytes. The data bytes immediately follow
                                     ///< this structure.
} CdiCDIPacketNum0Header;

/**
 * @brief Union of payload cdi headers. Use to reserve memory that can be used to hold any type of CDI packet
 *        header.
 */
typedef struct {
    union {
        CdiCDIPacketNum0Header num0_hdr;     ///< Header of CDI packet number 0 header.
        CdiCDIPacketCommonHeader common_hdr; ///< Header of non data offset cdi packets.
        CdiCDIPacketDataOffsetHeader offset_hdr; ///< Header of data offset cdi packets.
    };
    uint8_t extra_data[MAX_CDI_PACKET_EXTRA_DATA]; ///< Optional extra data.
} CdiCDIPacketHeaderUnion;

#pragma pack(pop)
// --------------------------------------------------------------------
// End of byte packed structures (no byte padding).
// --------------------------------------------------------------------

/**
 * @brief Structure used to hold state data for a single payload.
 */
typedef struct {
    CdiPayloadType payload_type;         ///< Payload type (application or keep alive).
    uint16_t maximum_packet_byte_size;   ///< Maximum size of packets in bytes.
    uint8_t maximum_tx_sgl_entries;      ///< Maximum number of SGL entries for a packet.

    uint8_t payload_num;                 ///< Payload number. Value is unique for each Tx connection and increments by 1
                                         ///  for each payload transmitted.

    uint16_t packet_sequence_num;        ///< Current CDI packet sequence number.
    uint16_t packet_payload_data_size;   ///< Size in bytes of payload data in current CDI packet.

    const CdiSglEntry* source_entry_ptr; ///< Current source payload SGL entry being used.
    int source_entry_address_offset;     ///< Current source entry address offset of entry being used. Only used if the
                                         ///  data size of the source SGL entry is larger than the CDI packet data
                                         ///  size (the SGL entry spans more than 1 CDI packet).
    uint32_t payload_data_offset;        ///< Current offset of payload data.
} CdiPayloadCDIPacketState;

/// Forward reference of structure to create pointers later.
typedef struct CdiConnectionState CdiConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct TxPayloadState TxPayloadState;

/// An opaque type for the packetizer to keep track of its progress in case it must be suspended for lack of resources.
typedef struct CdiPacketizerState* CdiPacketizerStateHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Initialize an CdiPayloadCDIPacketState structure before using CdiPayloadGetPacket() to split the payload into
 * packets. NOTE: If an error occurs, caller is responsible for freeing the pool buffers that it allocates.
 *
 * @param con_state_ptr            ///< Pointer to connection state data.
 * @param source_sgl_ptr           ///< Pointer to Tx Payload source SGL list.
 * @param payload_state_ptr        ///< Pointer to payload state data.
 *
 * @return true if successful, otherwise an error occurred.
 */
bool CdiPayloadInit(CdiConnectionState* con_state_ptr, const CdiSgList* source_sgl_ptr,
                    TxPayloadState* payload_state_ptr);

/**
 * Creates a packetizer state object. This must be destroyed with CdiPacketizerStateDestroy() when the connection is
 * closed.
 *
 * @return Handle for the created packetizer state or NULL if the creation failed.
 */
CdiPacketizerStateHandle CdiPacketizerStateCreate();

/**
 * Frees the memory previously allocated for a packetizer state object through CdiPacketizerStateCreate().
 *
 * @param packetizer_state_handle The handle of the packetizer state to be destroyed.
 */
void CdiPacketizerStateDestroy(CdiPacketizerStateHandle packetizer_state_handle);

/**
 * Initializes a packetizer state object. This function should be called before calling CdiPayloadGetPacket() the first
 * time for a given payload.
 *
 * @param packetizer_state_handle Handle of packetizer object.
 */
void CdiPacketizerStateInit(CdiPacketizerStateHandle packetizer_state_handle);

/// Forward reference of structure to allow pointer creation.
typedef struct TxPayloadState TxPayloadState;

/**
 * Get the next packet for a payload. Must use CdiPacketizerStateInit() for a new payload before using this function. If
 * false is returned, one of the pools from which required resources are taken is dry so this function should be called
 * again until it returns true.
 *
 * NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is accessing them
 * at a time.
 *
 * @param packetizer_state_handle      ///< Handle of the packetizer state for this connection.
 * @param hdr_union_ptr                ///< Pointer to the header data structure to be filled in for the new packet.
 * @param packet_sgl_entry_pool_handle ///< CDI packet SGL list entry pool.
 * @param payload_state_ptr            ///< Pointer to payload state data.
 * @param packet_sgl_ptr               ///< Pointer to returned packet SGL list
 * @param ret_is_last_packet_ptr       ///< Pointer to returned last packet state. True if last packet, otherwise false.
 *
 * @return true if packet returned, otherwise a pool was empty so false is returned.
 */
bool CdiPayloadGetPacket(CdiPacketizerStateHandle packetizer_state_handle, CdiCDIPacketHeaderUnion *hdr_union_ptr,
                         CdiPoolHandle packet_sgl_entry_pool_handle, TxPayloadState* payload_state_ptr,
                         CdiSgList* packet_sgl_ptr, bool* ret_is_last_packet_ptr);

/**
 * Return the CDI common header of a CDI packet #0.
 *
 * @param packet_sgl_ptr Pointer to CDI packet SGL list.
 *
 * @return CdiCDIPacketCommonHeader* Pointer to CDI packet common header. If NULL, then an error occurred.
 */
CdiCDIPacketCommonHeader* CdiPayloadParseCDIPacket(const CdiSgList* packet_sgl_ptr);

#endif  // CDI_PAYLOAD_H__
