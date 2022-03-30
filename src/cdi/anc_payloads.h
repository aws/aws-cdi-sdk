// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * Declarations of utilities for creating and parsing payloads formatted according to the CDI Ancillary Data Format
 * Specification (https://cdi.elemental.com/specs/baseline-ancillary-data).
 *
 * Ancillary data payloads contain one or more ancillarry data packets (ANC packets). Each ANC packet compactly contains
 * user data in 10-bit words. An ANC packet may contain up to 255 user data words. The meaning of these words is
 * determined by the DID and SDID fields in an ANC packet header. See
 * https://smpte-ra.org/smpte-ancillary-data-smpte-st-291 for current assignments.
 */

#ifndef ANC_PAYLOADS_H__
#define ANC_PAYLOADS_H__

#include "cdi_avm_payloads_api.h"

#include <stdint.h>
#include <stdbool.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief The ANC data packet.
/// See https://datatracker.ietf.org/doc/html/rfc8331#section-2.1 for details.
struct AncillaryDataPacket
{
    /// True when the ANC data corresponds to the color-difference data channel.
    bool is_color_difference_channel;
    /// The digital interface line number.
    unsigned line_number:11;
    /// The horizontal offset in an SDI raster relative to the start of active video.
    unsigned horizontal_offset:12;
    /// True when the source_stream_number value is set.
    bool is_valid_source_stream_number;
    /// Source stream number.
    unsigned source_stream_number:7;
    /// Data identification number.
    uint8_t did;
    /// Secondary data identification number.
    uint8_t sdid;
    /// Number of user data words in packet.
    uint8_t data_count;
    /// The packet's user data words (up to 255). Must contain 10-bit values.
    uint16_t user_data[255 + 4];
};

/// @brief Counters for keeping track of errors observed when parsing ANC payloads.
struct AncillaryDataPayloadErrors
{
    /// Running checksum, used by ParseAncillaryDataPacket.
    uint32_t checksum;
    /// Count of the number of checkum errors observed (at most one per ANC packet).
    int checksum_errors;
    /// Count of the number of parity errors observed.
    int parity_errors;
};

/// @brief The ANC payload header.
struct AncillaryDataPayloadHeader
{
    /// The number of ANC packets in the payload.
    uint16_t ancillary_data_packet_count;
    /// Field kind of the associated video payload.
    CdiFieldKind field_kind;
};

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Return parity bit of a byte.
 *
 * @param value The value for which to calculate the parity bit.
 *
 * @return The parity bit of value.
 */
inline static bool Parity8(uint8_t value)
{
    // From https://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
    unsigned v = value;
    v ^= v >> 4;
    v &= 0xf;
    return (0x6996 >> v) & 1;
}

/**
 * Return 8-bit value with two parity bits.
 *
 * @param value The value to augment with parity bits.
 *
 * @return 10-bit word: 8-bit value and two parity bits.
 */
inline static uint16_t WithParityBits(uint8_t value)
{
    uint16_t parity = Parity8(value);
    return (!parity << 9) + (parity << 8) + value;
}

/**
 * Check the parity bits of an 8-bit value in the 10-bit input data.
 *
 * @param raw_word 10-bit raw word
 * @param parity_errors_ptr Pointer to payload error counts.
 *
 * @return 8-bit value (bits b7-b0 of raw_word).
 */
inline static uint8_t CheckParityBits(uint16_t raw_word, int* parity_errors_ptr)
{
    uint16_t parity = Parity8(raw_word);
    if ((raw_word & 0x100) >> 8 != parity || (raw_word & 0x200) >> 9 == parity) {
        (*parity_errors_ptr)++;
    }
    return 0xff & raw_word;
}

/**
 * Parse the header of a received ancillary data payload.
 *
 * @param payload_net_data_ptr Pointer to received ancillary data. In network byte order. Input.
 * @param packet_count_ptr Pointer to number of ancillary data packets in payload. Output.
 * @param field_kind_ptr Pointer to field kind associated with payload. Output.
 */
void ParseAncillaryDataPayloadHeader(const uint32_t* payload_net_data_ptr, uint16_t* packet_count_ptr,
    CdiFieldKind* field_kind_ptr);

/**
 * Parse the header of an ancillary data packet.
 *
 * @param packet_net_data_ptr Pointer to a received ANC packet contained in an ANC payload. In network byte order. Input.
 * @param packet_ptr Pointer to an ANC packet structure. Output.
 * @param payload_errors Pointer to error counters.
 *
 * @return Part of the packet's first user data word or zero when the packet is empty.
 */
void ParseAncillaryDataPacketHeader(const uint32_t* packet_net_data_ptr, struct AncillaryDataPacket* packet_ptr,
    struct AncillaryDataPayloadErrors* payload_errors);

/**
 * Parse and ancillary data packet.
 *
 * @param packet_net_data_ptr Pointer to a received ANC packet contained in an ANC payload. In network byte order. Input.
 * @param packet_ptr Pointer to an ANC packet. Output.
 * @param payload_errors_ptr Pointer to error counters.
 *
 * @return Offset from packet_net_data_ptr to the beginning of the next ANC packet.
 */
int ParseAncillaryDataPacket(const uint32_t* packet_net_data_ptr, struct AncillaryDataPacket* packet_ptr,
    struct AncillaryDataPayloadErrors* payload_errors_ptr);

/**
 * Calculate number of 32-bit words needed to write an ancillary data packet.
 *
 * @param data_count The number of user data words in the ANC packet.
 *
 * @return The number of 32-bit words required to store an ANC packet with data_count user data words.
 */
int GetAncillaryDataPacketSize(int data_count);

/**
 * Write an ancillary data payload header for transmission in network byte order.
 *
 * @param payload_net_data_ptr Pointer to transmit buffer for an ANC payload. In network byte order. Output.
 * @param packet_count Number of ancillary data packets in payload. Input.
 * @param field_kind Field kind associated with payload. Input.
 */
void WriteAncillaryDataPayloadHeader(uint32_t* payload_net_data_ptr, uint16_t packet_count, CdiFieldKind field_kind);

/**
 * Write an ancillary data packet header for transmission in network byte order.
 *
 * @param packet_net_data_ptr Pointer into transmit buffer to start of an ANC packet. In network byte order. Output.
 * @param packet_ptr Pointer to an ANC packet. Input.
 * @param checksum_ptr Pointer to running checksum. Output.
 */
void WriteAncillaryDataPacketHeader(uint32_t* packet_net_data_ptr, const struct AncillaryDataPacket* packet_ptr,
    uint32_t* checksum_ptr);

/**
 * Write an ancillary data packet for transmission in network byte order.
 *
 * @param packet_net_data_ptr Pointer into transmit buffer to start of an ANC packet. In network byte order. Output.
 * @param packet_ptr Pointer to ANC packet. Input.
 *
 * @return Offset from packet_net_data_ptr to the start of the next ancillary data packet to write.
 */
int WriteAncillaryDataPacket(uint32_t* packet_net_data_ptr, const struct AncillaryDataPacket* packet_ptr);

#endif // ANC_PAYLOADS_H__