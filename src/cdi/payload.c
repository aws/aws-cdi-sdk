// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used for payloads.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "payload.h"

#include <stdbool.h>
#include <string.h>

#include "adapter_api.h"
#include "internal.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief Structure to store the current state of a packet being constructed. Its purpose is to allow for the suspension
 * of the creation of a packet if a pool from which items need to be allocated is empty. A state object is passed in to
 * CdiPayloadInit() prior to calls to CdiPayloadParseCDIPacket() for a given payload.
 */
typedef struct {
    /// The state of the packetizer so that CdiPayloadGetPacket()'s progress on a given packet can be suspended for lack
    /// of resources and resumed in a subsequent call.
    enum {
        kStateInactive,     ///< This is the first time CdiPayloadGetPacket() has been called for a given packet.
        kStateAddingHeader, ///< The packetizer is attempting to add the SGL entry for the CDI packet header.
        kStateAddingEntries ///< The packetizer is adding the payload SGL entries.
    } state;

    CdiSglEntry* packet_entry_hdr_ptr; ///< Pointer to the current payload SGL entry being consumed.
    int header_size;                   ///< The size of the header computed for this packet.
    int accumulated_payload_bytes;     ///< The number of payload bytes collected so far into the current packet.
    int sgl_entry_count;               ///< The number of SGL entries used so far to represent the current packet.
    uint8_t* data_addr_ptr;            ///< The current address in the payload buffer.
    int max_payload_bytes;             ///< The maximum number of payload bytes that can be put into this packet.
} CdiPacketizerState;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool CdiPayloadInit(CdiConnectionState* con_state_ptr, const CdiSgList* source_sgl_ptr,
                    TxPayloadState* payload_state_ptr)
{
    bool ret = true;

    CdiPayloadCDIPacketState* packet_state_ptr = &payload_state_ptr->payload_packet_state;

    packet_state_ptr->payload_type = kPayloadTypeData;
    packet_state_ptr->maximum_packet_byte_size = con_state_ptr->adapter_state_ptr->maximum_payload_bytes;
    packet_state_ptr->maximum_tx_sgl_entries = con_state_ptr->adapter_state_ptr->maximum_tx_sgl_entries;
    packet_state_ptr->payload_num = 0;
    packet_state_ptr->packet_sequence_num = 0;
    // NOTE: source_entry_ptr is set below to point to the head of the copy of the SGL.
    packet_state_ptr->source_entry_address_offset = 0;

    payload_state_ptr->source_sgl.internal_data_ptr = source_sgl_ptr->internal_data_ptr;
    payload_state_ptr->source_sgl.total_data_size = 0;
    payload_state_ptr->source_sgl.sgl_head_ptr = NULL;
    payload_state_ptr->source_sgl.sgl_tail_ptr = NULL;

    // Walk through source SGL and generate a copy of each SGL entry so user-application does not have to maintain the
    // memory for the entries until the payload callback has been made.
    CdiSglEntry* entry_ptr = source_sgl_ptr->sgl_head_ptr;
    int total_entry_size = 0;
    while (ret && entry_ptr) {
        total_entry_size += entry_ptr->size_in_bytes;
        CdiSglEntry* new_entry_ptr = NULL;
        ret = CdiPoolGet(con_state_ptr->tx_state.payload_sgl_entry_pool_handle, (void**)&new_entry_ptr);
        if (ret) {
            *new_entry_ptr = *entry_ptr;
            new_entry_ptr->next_ptr = NULL;
            SglAppend(&payload_state_ptr->source_sgl, new_entry_ptr);
            entry_ptr = entry_ptr->next_ptr;
        }
    }

    // Check that the sum of all entry size_in_bytes values matches the SGL's total_data_size.
    if (ret && source_sgl_ptr->total_data_size != total_entry_size ) {
        ret = false;
        CDI_LOG_THREAD(kLogError, "Mismatch between sgl total_data_size [%d] and sum of entries size_in_bytes [%d].",
                       source_sgl_ptr->total_data_size, total_entry_size);
    }
    packet_state_ptr->source_entry_ptr = payload_state_ptr->source_sgl.sgl_head_ptr;

    // NOTE: If an error occurs, caller is responsible for freeing the pool buffers.

    return ret;
}

CdiPacketizerStateHandle CdiPacketizerStateCreate()
{
    return (CdiPacketizerStateHandle)CdiOsMemAllocZero(sizeof(CdiPacketizerState));
}

void CdiPacketizerStateInit(CdiPacketizerStateHandle packetizer_state_handle)
{
    ((CdiPacketizerState*)packetizer_state_handle)->state = kStateInactive;
}

void CdiPacketizerStateDestroy(CdiPacketizerStateHandle packetizer_state_handle)
{
    CdiOsMemFree(packetizer_state_handle);
}

bool CdiPayloadGetPacket(CdiPacketizerStateHandle packetizer_state_handle, CdiCDIPacketHeaderUnion *hdr_union_ptr,
                         CdiPoolHandle packet_sgl_entry_pool_handle, TxPayloadState* payload_state_ptr,
                         CdiSgList* packet_sgl_ptr, bool* ret_is_last_packet_ptr)
{
    bool ret = true;

    CdiPacketizerState* packetizer_state_ptr = (CdiPacketizerState*)packetizer_state_handle;
    CdiPayloadCDIPacketState* packet_state_ptr = &payload_state_ptr->payload_packet_state;

    if (kStateInactive == packetizer_state_ptr->state) {
        // Initialize all data and pointers used in the SGL list.
        memset((void*)packet_sgl_ptr, 0, sizeof(*packet_sgl_ptr));

        // Create new SGL entry for the payload data to hold the CDI header and first part of the payload data.
        packetizer_state_ptr->packet_entry_hdr_ptr = NULL;

        packetizer_state_ptr->state = kStateAddingHeader;
    }

    if (kStateAddingHeader == packetizer_state_ptr->state) {
        // NOTE: All the pools used in this function are not thread-safe, so must ensure that only one thread is
        // accessing them at a time.
#ifdef USE_MEMORY_POOL_APPENDED_LISTS
        ret = CdiPoolGetAndAppend(packet_sgl_entry_pool_handle, packet_sgl_ptr->sgl_tail_ptr,
                                  (void**)&packetizer_state_ptr->packet_entry_hdr_ptr);
#else
        ret = CdiPoolGet(packet_sgl_entry_pool_handle, (void**)&packetizer_state_ptr->packet_entry_hdr_ptr);
#endif
        if (ret) {
            // Initialize SGL entry.
            packetizer_state_ptr->packet_entry_hdr_ptr->next_ptr = NULL;
            packetizer_state_ptr->packet_entry_hdr_ptr->internal_data_ptr = NULL;

            if (0 == packet_state_ptr->packet_sequence_num) {
                // Process first packet of the payload (packet #0).
                packetizer_state_ptr->header_size = sizeof(CdiCDIPacketNum0Header);

                // Create new packet #0 cdi header.
                CdiCDIPacketNum0Header* hdr_ptr = (CdiCDIPacketNum0Header*)hdr_union_ptr;
                hdr_ptr->hdr.payload_type = packet_state_ptr->payload_type;
                hdr_ptr->hdr.packet_sequence_num = 0;
                hdr_ptr->hdr.payload_num = packet_state_ptr->payload_num;
                hdr_ptr->total_payload_size = payload_state_ptr->source_sgl.total_data_size;
                hdr_ptr->max_latency_microsecs = payload_state_ptr->max_latency_microsecs;
                hdr_ptr->origination_ptp_timestamp =
                        payload_state_ptr->app_payload_cb_data.core_extra_data.origination_ptp_timestamp;
                hdr_ptr->payload_user_data = payload_state_ptr->app_payload_cb_data.core_extra_data.payload_user_data;

                hdr_ptr->extra_data_size = payload_state_ptr->app_payload_cb_data.extra_data_size;
                if (payload_state_ptr->app_payload_cb_data.extra_data_size) {
                    memcpy((uint8_t*)hdr_ptr + packetizer_state_ptr->header_size,
                           payload_state_ptr->app_payload_cb_data.extra_data_array,
                           payload_state_ptr->app_payload_cb_data.extra_data_size);
                    packetizer_state_ptr->header_size += payload_state_ptr->app_payload_cb_data.extra_data_size;
                }
            } else {
                // Process additional packets of the payload (other than packet #0).

                // Create new packet cdi header.
                CdiCDIPacketCommonHeader* hdr_ptr = (CdiCDIPacketCommonHeader*)hdr_union_ptr;
                hdr_ptr->payload_type = packet_state_ptr->payload_type;
                hdr_ptr->packet_sequence_num = packet_state_ptr->packet_sequence_num;
                hdr_ptr->payload_num = packet_state_ptr->payload_num;

                if (kPayloadTypeDataOffset == packet_state_ptr->payload_type) {
                    packetizer_state_ptr->header_size = sizeof(CdiCDIPacketDataOffsetHeader);
                    CdiCDIPacketDataOffsetHeader* ptr = (CdiCDIPacketDataOffsetHeader*)hdr_union_ptr;
                    ptr->payload_data_offset = packet_state_ptr->payload_data_offset;
                } else {
                    packetizer_state_ptr->header_size = sizeof(CdiCDIPacketCommonHeader);
                }
            }

            // Setup SGL entry for our header and add it to the packet SGL.
            packetizer_state_ptr->packet_entry_hdr_ptr->address_ptr = hdr_union_ptr;
            packetizer_state_ptr->packet_entry_hdr_ptr->size_in_bytes = packetizer_state_ptr->header_size;
            SglAppend(packet_sgl_ptr, packetizer_state_ptr->packet_entry_hdr_ptr); // NOTE: SGL list size is updated in
                                                                                   // this call.

            // Try to fill an entire packet, either by using part of a large SGL entry and/or multiple smaller SGL
            // entries.
            packetizer_state_ptr->max_payload_bytes =
                packet_state_ptr->maximum_packet_byte_size - packetizer_state_ptr->header_size;
            if (payload_state_ptr->pattern_size_bytes > 0) {
                // If the pattern size is larger than the max payload then do not modify the payload size.
                if (payload_state_ptr->pattern_size_bytes <= packetizer_state_ptr->max_payload_bytes) {
                    packetizer_state_ptr->max_payload_bytes = packetizer_state_ptr->max_payload_bytes /
                                                              payload_state_ptr->pattern_size_bytes *
                                                              payload_state_ptr->pattern_size_bytes;
                } else {
                    CDI_LOG_THREAD(kLogWarning,
                                   "Payload unit size [%d] bytes is larger than available packet data [%d] bytes",
                                   payload_state_ptr->pattern_size_bytes, packetizer_state_ptr->max_payload_bytes);
                }
            }

            packetizer_state_ptr->accumulated_payload_bytes = 0;
            packetizer_state_ptr->sgl_entry_count = 1; // Allow for CDI header created above.
            packetizer_state_ptr->data_addr_ptr = (uint8_t*)packet_state_ptr->source_entry_ptr->address_ptr +
                                                  packet_state_ptr->source_entry_address_offset;

            packetizer_state_ptr->state = kStateAddingEntries;
        }
    }

    if (kStateAddingEntries == packetizer_state_ptr->state) {
        // Break out of this loop if we filled the packet, or we ran out of source SGL entries, or we have reached the
        // maximum number of SGL entries supported by the underlying adapter.
        while (ret && packetizer_state_ptr->accumulated_payload_bytes < packetizer_state_ptr->max_payload_bytes &&
               packet_state_ptr->source_entry_ptr) {
            // Create new SGL entry for the payload data and add it to the packet SGL.
            CdiSglEntry *packet_entry_data_ptr = NULL;
#ifdef USE_MEMORY_POOL_APPENDED_LISTS
            ret = CdiPoolGetAndAppend(packet_sgl_entry_pool_handle, packet_sgl_ptr->sgl_tail_ptr,
                                        (void**)&packet_entry_data_ptr);
#else
            ret = CdiPoolGet(packet_sgl_entry_pool_handle, (void**)&packet_entry_data_ptr);
#endif
            if (ret) {
                const int sgl_data_size = CDI_MIN(packet_state_ptr->source_entry_ptr->size_in_bytes -
                                                  packet_state_ptr->source_entry_address_offset,
                                                  packetizer_state_ptr->max_payload_bytes -
                                                  packetizer_state_ptr->accumulated_payload_bytes);

                // Initialize SGL entry.
                packet_entry_data_ptr->next_ptr = NULL;
                packet_entry_data_ptr->internal_data_ptr = NULL;

                // Set SGL entry data and add it to the SGL list.
                packet_entry_data_ptr->address_ptr = packetizer_state_ptr->data_addr_ptr;
                packet_entry_data_ptr->size_in_bytes = sgl_data_size;
                SglAppend(packet_sgl_ptr, packet_entry_data_ptr); // NOTE: SGL list size is updated in this call.

                packetizer_state_ptr->accumulated_payload_bytes += sgl_data_size;
                packetizer_state_ptr->data_addr_ptr += sgl_data_size;
                packet_state_ptr->payload_data_offset += sgl_data_size;

                packet_state_ptr->source_entry_address_offset += sgl_data_size;
                if (packet_state_ptr->source_entry_address_offset >=
                    packet_state_ptr->source_entry_ptr->size_in_bytes) {
                    packet_state_ptr->source_entry_ptr = packet_state_ptr->source_entry_ptr->next_ptr;
                    packet_state_ptr->source_entry_address_offset = 0;
                    if (NULL != packet_state_ptr->source_entry_ptr) {
                        packetizer_state_ptr->data_addr_ptr = packet_state_ptr->source_entry_ptr->address_ptr;
                    }
                }

                packet_state_ptr->packet_payload_data_size = packetizer_state_ptr->accumulated_payload_bytes;

                // Determine if we have reached the maximum number of SGL entries the adapter supports for a single
                // packet.
                if (++packetizer_state_ptr->sgl_entry_count >= packet_state_ptr->maximum_tx_sgl_entries) {
                    // Force subsequent packets to include a data offset in their headers; this packet doesn't need the
                    // offset to be correctly placed on the receive side. The data offset is needed for the receive side
                    // to know where to place the data when its using a linear buffer since packets can arrive out of
                    // order.
                    packet_state_ptr->payload_type = kPayloadTypeDataOffset;

                    break;
                }
            }
        }

        *ret_is_last_packet_ptr = false;
        if (ret) {
            *ret_is_last_packet_ptr = NULL == packet_state_ptr->source_entry_ptr;

            packet_state_ptr->packet_sequence_num++; // Increment packet sequence number.
            packetizer_state_ptr->state = kStateInactive;
        }
    }

    return ret;
}

CdiCDIPacketCommonHeader* CdiPayloadParseCDIPacket(const CdiSgList* packet_sgl_ptr)
{
    if (packet_sgl_ptr->total_data_size < (int)sizeof(CdiCDIPacketCommonHeader)) {
        assert(false);
        return NULL;
    }

    if (NULL == packet_sgl_ptr->sgl_head_ptr ||
        packet_sgl_ptr->sgl_head_ptr->size_in_bytes < (int)sizeof(CdiCDIPacketCommonHeader)) {
        assert(false);
        return NULL;
    }

    return packet_sgl_ptr->sgl_head_ptr->address_ptr;
}

