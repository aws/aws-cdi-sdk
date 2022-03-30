// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file implements the AVM payloads API.
*/

#include "cdi_avm_payloads_api.h"
#include "cdi_os_api.h"
#include "anc_payloads.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Copy SGL buffer contents to a linear buffer.
 *
 * @param sgl_ptr Pointer to SGL.
 *
 * @return Pointer to linear buffer. Caller needs to free buffer.
 */
static void* CopyToLinearBuffer(CdiSgList const* sgl_ptr)
{
    void* buffer_ptr = CdiOsMemAlloc(sgl_ptr->total_data_size);
    if (buffer_ptr) {
        char* dest_ptr = buffer_ptr;
        for (CdiSglEntry* entry_ptr = sgl_ptr->sgl_head_ptr; NULL != entry_ptr; entry_ptr = entry_ptr->next_ptr) {
            memcpy(dest_ptr, entry_ptr->address_ptr, entry_ptr->size_in_bytes);
            dest_ptr += entry_ptr->size_in_bytes;
        }
    }
    return buffer_ptr;
}

/**
 * Helper function for CdiAvmUnpacketizeAncillaryDataPayload. Decodes the payload header and checks that the payload may
 * be decoded without exceeding the specified payload size.
 *
 * @param buffer_ptr Pointer to the payload buffer.
 * @param payload_size_in_bytes Size of the payload.
 *
 * @return Return code indicating success or failure.
 */
static CdiReturnStatus PrecheckAncillaryDataPayload(const char* buffer_ptr, int payload_size_in_bytes)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Payload size must be multiple of word size.
    if (0 != payload_size_in_bytes % sizeof(uint32_t) || 4 > payload_size_in_bytes) {
        return kCdiStatusInvalidParameter;
    }
    int size_in_words = payload_size_in_bytes / sizeof(uint32_t);

    // Read the header to get the packet count.
    CdiFieldKind unused = kCdiFieldKindUnspecified;
    uint32_t* payload_ptr = (uint32_t*)buffer_ptr;
    uint16_t anc_packet_count = 0;
    ParseAncillaryDataPayloadHeader(payload_ptr, &anc_packet_count, &unused);

    // When this is not an empty payload, step through all ANC data packets to check for errors.
    // If in the process we would exceed the payload size, then the payload is invalid and we return kCdiInvalidPayload.
    struct AncillaryDataPayloadErrors payload_errors = { 0 };
    struct AncillaryDataPacket packet;
    int offset = 1; // one word for payload header
    while (offset < size_in_words && 0 != anc_packet_count) {
        ParseAncillaryDataPacketHeader(payload_ptr + offset, &packet, &payload_errors);
        int packet_size = GetAncillaryDataPacketSize(packet.data_count);
        offset += packet_size;
        anc_packet_count--;
    }
    if (offset != size_in_words || 0 != anc_packet_count) {
        rs = kCdiStatusInvalidPayload;
    }

    return rs;
}

/**
 * Copy internal packet structure to public-facing API. Count number of parity errors in source.
 *
 * @param dest_packet_ptr Destination packet to copy to.
 * @param source_packet_ptr Source packet to copy from.
 *
 * @return Number of user data parity errors detected in source packet.
 */
static int CopyInternalToPublicPacket(CdiAvmAncillaryDataPacket* dest_packet_ptr,
    const struct AncillaryDataPacket* source_packet_ptr)
{
    int parity_errors = 0;
    dest_packet_ptr->is_color_difference_channel = source_packet_ptr->is_color_difference_channel;
    dest_packet_ptr->line_number = source_packet_ptr->line_number;
    dest_packet_ptr->horizontal_offset = source_packet_ptr->horizontal_offset;
    dest_packet_ptr->is_valid_source_stream_number = source_packet_ptr->is_valid_source_stream_number;
    dest_packet_ptr->source_stream_number = source_packet_ptr->source_stream_number;
    dest_packet_ptr->did = source_packet_ptr->did;
    dest_packet_ptr->sdid = source_packet_ptr->sdid;
    dest_packet_ptr->data_count = source_packet_ptr->data_count;
    for (uint16_t i=0; i<source_packet_ptr->data_count; i++) {
        dest_packet_ptr->user_data[i] = CheckParityBits(source_packet_ptr->user_data[i], &parity_errors);
    }
    return parity_errors;
}

/**
 * Copy public packet data to internal packet structure and add parity bits to 8-bit user data.
 *
 * @param dest_packet_ptr Destination packet to copy to.
 * @param source_packet_ptr Source packet to copy from.
 */
static void CopyPublicToInternalPacket(struct AncillaryDataPacket* dest_packet_ptr,
    const CdiAvmAncillaryDataPacket* source_packet_ptr)
{
    dest_packet_ptr->is_color_difference_channel = source_packet_ptr->is_color_difference_channel;
    dest_packet_ptr->line_number = source_packet_ptr->line_number;
    dest_packet_ptr->horizontal_offset = source_packet_ptr->horizontal_offset;
    dest_packet_ptr->is_valid_source_stream_number = source_packet_ptr->is_valid_source_stream_number;
    dest_packet_ptr->source_stream_number = source_packet_ptr->source_stream_number;
    dest_packet_ptr->did = source_packet_ptr->did;
    dest_packet_ptr->sdid = source_packet_ptr->sdid;
    dest_packet_ptr->data_count = source_packet_ptr->data_count;
    for (uint16_t i=0; i<source_packet_ptr->data_count; i++) {
        dest_packet_ptr->user_data[i] = WithParityBits(source_packet_ptr->user_data[i]);
    }
}

/**
 * Copy function. Only exists for testing.
 *
 * @param dest_packet_ptr Destination packet to copy to.
 * @param source_packet_ptr Source packet to copy from.
 *
 * @return Number of parity errors (must be zero).
 */
int CdiAvmCopyAncillaryDataPacket(CdiAvmAncillaryDataPacket* dest_packet_ptr,
    const CdiAvmAncillaryDataPacket* source_packet_ptr)
{
    struct AncillaryDataPacket internal_packet = { 0 };
    CopyPublicToInternalPacket(&internal_packet, source_packet_ptr);
    return CopyInternalToPublicPacket(dest_packet_ptr, &internal_packet);
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

int CdiAvmGetAncillaryDataPayloadSize(uint16_t num_anc_packets, uint8_t data_counts[])
{
    int num_words = 1; // One word for payload header.
    for (uint32_t i = 0; i < num_anc_packets; ++i) {
        num_words += GetAncillaryDataPacketSize(data_counts[i]); // returns size in words
    }

    return num_words * sizeof(uint32_t);
}

CdiReturnStatus CdiAvmPacketizeAncillaryData(CdiAvmPacketizeAncCallback* produce_next_packet_ptr,
    CdiFieldKind field_kind, void* context_ptr, char* buffer_ptr, int* size_in_bytes_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;
    const CdiAvmAncillaryDataPacket* packet_ptr = NULL;

    // Remember the buffer size.
    int buffer_size = *size_in_bytes_ptr;
    if (0 > buffer_size) {
        rs = kCdiStatusInvalidParameter;
    }

    // Keep track of payload size and abort when it would grow larger than the buffer size.
    uint32_t* payload_ptr = (uint32_t*)buffer_ptr;
    int offset = 1; // Reserve 1 word for the payload header. We will write it at the end.
    int total_size = offset * sizeof(uint32_t);
    int anc_packet_count = 0;
    while ((kCdiStatusOk == rs) && (NULL != (packet_ptr = produce_next_packet_ptr(context_ptr)))) {
        total_size += GetAncillaryDataPacketSize(packet_ptr->data_count) * sizeof(uint32_t);
        if (total_size > buffer_size) {
            rs = kCdiStatusBufferOverflow;
        } else {
            struct AncillaryDataPacket internal_packet;
            CopyPublicToInternalPacket(&internal_packet, packet_ptr);
            offset += WriteAncillaryDataPacket(payload_ptr + offset, &internal_packet);
            anc_packet_count++;
        }
    }
    if (kCdiStatusOk == rs) {
        WriteAncillaryDataPayloadHeader(payload_ptr, anc_packet_count, field_kind);
        *size_in_bytes_ptr = total_size;
    } else {
        *size_in_bytes_ptr = 0;
    }
    return rs;
}


CdiReturnStatus CdiAvmUnpacketizeAncillaryData(const CdiSgList* sgl_ptr,
    CdiAvmUnpacketizeAncCallback* consume_next_packet_ptr, void* context_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Ensure data is in contiguous memory.
    bool need_to_free_buffer = false;
    char* buffer_ptr = NULL;
    const int payload_size_in_bytes = sgl_ptr->total_data_size;
    if (sgl_ptr->sgl_head_ptr == sgl_ptr->sgl_tail_ptr) {
        buffer_ptr = sgl_ptr->sgl_head_ptr->address_ptr;
        assert(payload_size_in_bytes == sgl_ptr->sgl_head_ptr->size_in_bytes);
    } else if (NULL != (buffer_ptr = CopyToLinearBuffer(sgl_ptr))) {
        need_to_free_buffer = true;
    } else {
        rs = kCdiStatusAllocationFailed;
    }

    // Do a quick sanity check before processing the payload.
    if (kCdiStatusOk == rs) {
        rs = PrecheckAncillaryDataPayload(buffer_ptr, payload_size_in_bytes);
    }

    if (kCdiStatusOk == rs) {
        // Payload size must be multiple of word size. Precheck is supposed to check this.
        assert(0 == (payload_size_in_bytes % sizeof(uint32_t)) && 4 <= payload_size_in_bytes);
        int size_in_words = payload_size_in_bytes / sizeof(uint32_t);

        // Read the payload header.
        CdiFieldKind field_kind = kCdiFieldKindUnspecified;
        uint32_t* payload_ptr = (uint32_t*)buffer_ptr;
        uint16_t anc_packet_count = 0;
        ParseAncillaryDataPayloadHeader(payload_ptr, &anc_packet_count, &field_kind);

        // Walk through the payload and call the application callback for each decoded ancillary data packet.
        struct AncillaryDataPayloadErrors payload_errors = { 0 };
        int offset = 1; // one word for payload header
        while (offset < size_in_words && 0 != anc_packet_count) {
            struct AncillaryDataPacket internal_packet;
            struct AncillaryDataPayloadErrors packet_errors = { 0 };
            int size = ParseAncillaryDataPacket(payload_ptr + offset, &internal_packet, &packet_errors);
            offset += size;
            if (offset <= size_in_words) {
                CdiAvmAncillaryDataPacket packet;
                int parity_errors = CopyInternalToPublicPacket(&packet, &internal_packet);
                packet.packet_offset = 4 * (offset - size);
                packet.packet_size = 4 * size;
                consume_next_packet_ptr(context_ptr, field_kind, &packet,
                    0 != packet_errors.parity_errors, 0 != packet_errors.checksum_errors);
                payload_errors.parity_errors += packet_errors.parity_errors + parity_errors;
                payload_errors.checksum_errors += packet_errors.checksum_errors;
            }
            anc_packet_count--;
        }
        if (offset != size_in_words || 0 != anc_packet_count) {
            rs = kCdiStatusInvalidPayload;
        } else {
            if (0 != payload_errors.parity_errors || 0 != payload_errors.checksum_errors) {
                rs = kCdiStatusRxPayloadError;
            }
            // Signal payload complete.
            consume_next_packet_ptr(context_ptr, field_kind, NULL, 0 != payload_errors.parity_errors,
                0 != payload_errors.checksum_errors);
        }
    }

    if (need_to_free_buffer) {
        CdiOsMemFree(buffer_ptr);
    }

    return rs;
}