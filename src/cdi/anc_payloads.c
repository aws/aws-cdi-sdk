// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#include "anc_payloads.h"
#include "cdi_os_api.h"

#include <assert.h>
#include <arpa/inet.h>
#include <math.h>
#ifdef _LINUX
#  include <sys/param.h>
#endif
#ifdef _WIN32
#  define __LITTLE_ENDIAN 1234
#  define __BYTE_ORDER __LITTLE_ENDIAN
#endif

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Maximum number of user data words per ancillary data packet.
#define MAX_DATA_COUNT UINT8_MAX

#ifndef DOXYGEN_IGNORE
#if (__BYTE_ORDER == __LITTLE_ENDIAN)

//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           ANC_Count           | F |         reserved          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryDataPayloadRawHeader
{
    unsigned reserved:14;
    unsigned f_bits:2;
    unsigned anc_count:16;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |C|   Line_Number       |   Horizontal_Offset   |S|  StreamNum  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         DID       |        SDID       |   Data_Count      |UDW0
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryDataPacketRawHeader
{
    unsigned stream_number:7;
    unsigned s_bit:1;
    unsigned horizontal_offset:12;
    unsigned line_number:11;
    unsigned c_bit:1;
    unsigned udw0:2;
    unsigned data_count:10;
    unsigned sdid:10;
    unsigned did:10;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      UDW0       |        UDW1       |        UDW2       |  UDW3
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryUdws0
{
    unsigned udw3:4;
    unsigned udw2:10;
    unsigned udw1:10;
    unsigned udw0:8;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      UDW3   |        UDW4       |        UDW5       |    UDW6
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryUdws1
{
    unsigned udw6:6;
    unsigned udw5:10;
    unsigned udw4:10;
    unsigned udw3:6;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   UDW6  |        UDW7       |        UDW8       |      UDW9
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryUdws2
{
    unsigned udw9:8;
    unsigned udw8:10;
    unsigned udw7:10;
    unsigned udw6:4;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// UDW9|        UDW10      |        UDW11      |      UDW12        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryUdws3
{
    unsigned udw12:10;
    unsigned udw11:10;
    unsigned udw10:10;
    unsigned udw9:2;
};

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |        UDW13      |        UDW14      |      UDW15        |UDW0
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
struct AncillaryUdws4
{
    unsigned udw0:2;
    unsigned udw15:10;
    unsigned udw14:10;
    unsigned udw13:10;
};

#else

#  error Big endian platforms are not supported.

#endif  // IS_LITTLE_ENDIAN

CDI_STATIC_ASSERT(sizeof(struct AncillaryDataPayloadRawHeader) == sizeof(uint32_t), "Raw payload header is 32 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryDataPacketRawHeader) == 2*sizeof(uint32_t), "Raw packet header is 64 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryUdws0) == sizeof(uint32_t), "Raw UDW packet word is 32 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryUdws1) == sizeof(uint32_t), "Raw UDW packet word is 32 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryUdws2) == sizeof(uint32_t), "Raw UDW packet word is 32 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryUdws3) == sizeof(uint32_t), "Raw UDW packet word is 32 bit");
CDI_STATIC_ASSERT(sizeof(struct AncillaryUdws4) == sizeof(uint32_t), "Raw UDW packet word is 32 bit");

#endif // DOXYGEN_IGNORE

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Tell whether value is a 10-bit value.
 *
 * @param value The value to check.
 *
 * @return True if and only if value may be represented as a 10-bit unsigned number.
 */
static inline bool Is10BitValue(uint16_t value)
{
    return 0 == (~0x03ff & value);
}

/**
 * Return the nine least significant bits used in the checksum.
 *
 * @param value the 10-bit value to process.
 *
 * @return Nine LSBs of value.
 */
static uint16_t GetChecksumBits(uint32_t value)
{
    return value & 0x1ff;
}

/**
 * Add parity bits to 9-bit checksum.
 *
 * @param checksum Raw 9-bit checksum.
 *
 * @return Checksum with parity bit.
 */
static uint16_t FinishChecksum(uint32_t checksum) {
    // From SMPTE ST 291-1:
    // The checksum (CS) word shall be used to determine the validity of the ancillary data packet from the data
    // identification (DID) word through the user data words (UDW). It shall consist of 10 bits, wherein:
    // bits b8 (MSB) through b0 (LSB) shall define the checksum value, bit b9 = NOT b8.
    uint16_t not_b8 = (~checksum & 0x100);
    return GetChecksumBits(checksum) + (not_b8 << 1);
}

/// Helper for ParseNextUdws. See for ParseNextUdws for parameters.
static void ParseAndChecksumUdw(int next_udw, uint16_t value, struct AncillaryDataPacket* packet_ptr,
    uint32_t* checksum_ptr)
{
    if (next_udw <= packet_ptr->data_count) {
        packet_ptr->user_data[next_udw] += value;
    }
    if (next_udw < packet_ptr->data_count) {
        *checksum_ptr += packet_ptr->user_data[next_udw];
    }
}

/**
 * Helper for ParseAncillaryDataPacket: Parse the next few 10-bit user data words and update checksum.
 *
 * @param net_word A single word from the network byte order packet buffer.
 * @param next_udw UDW index. Logical index into the list of 10-bit user data words, indicating what words to parse next.
 * @param packet_ptr The ANC packet data structure to be filled.
 * @param checksum_ptr Running checksum to be updated.
 *
 * @return Updated UDW index.
 */
static int ParseNextUdws(const uint32_t net_word, int next_udw, struct AncillaryDataPacket* packet_ptr, uint32_t* checksum_ptr)
{
    assert(next_udw <= MAX_DATA_COUNT); // '<=' because this routine also parses the checksum

    switch (next_udw & 0xf) {
        case 0:
        {
            union {
                struct AncillaryUdws0 udws;
                uint32_t bytes;
            } raw;
            raw.bytes = ntohl(net_word);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw0, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw1, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw2, packet_ptr, checksum_ptr);
            packet_ptr->user_data[next_udw] = raw.udws.udw3 << 6;
            break;
        }
        case 1:
        case 2:
        assert(0); // never happens

        case 3:
        {
            union {
                struct AncillaryUdws1 udws;
                uint32_t bytes;
            } raw;
            raw.bytes = ntohl(net_word);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw3, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw4, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw5, packet_ptr, checksum_ptr);
            packet_ptr->user_data[next_udw] = raw.udws.udw6 << 4;
            break;
        }
        case 4:
        case 5:
        assert(0); // never happens

        case 6:
        {
            union {
                struct AncillaryUdws2 udws;
                uint32_t bytes;
            } raw;
            raw.bytes = ntohl(net_word);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw6, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw7, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw8, packet_ptr, checksum_ptr);
            packet_ptr->user_data[next_udw] = raw.udws.udw9 << 2;
            break;
        }
        case 7:
        case 8:
        assert(0); // never happens

        case 9:
        {
            union {
                struct AncillaryUdws3 udws;
                uint32_t bytes;
            } raw;
            raw.bytes = ntohl(net_word);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw9, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw10, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw11, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw12, packet_ptr, checksum_ptr);
            break;
        }
        case 10:
        case 11:
        case 12:
        assert(0); // never happens

        case 13:
        {
            union {
                struct AncillaryUdws4 udws;
                uint32_t bytes;
            } raw;
            raw.bytes = ntohl(net_word);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw13, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw14, packet_ptr, checksum_ptr);
            ParseAndChecksumUdw(next_udw++, raw.udws.udw15, packet_ptr, checksum_ptr);
            packet_ptr->user_data[next_udw] = raw.udws.udw0 << 8;
            break;
        }
        case 14:
        case 15:
        default:
        assert(0); // never happens
    }
    return next_udw;
}

/// Helper for WriteNextUdws. Return checksum when ready.
static uint16_t ChecksumUdw(int next_udw, bool add_cs, const struct AncillaryDataPacket* packet_ptr,
    uint32_t *checksum_ptr)
{
    uint16_t value = 0;
    if (next_udw < packet_ptr->data_count) {
        value = packet_ptr->user_data[next_udw];
        if (add_cs) {
            assert(Is10BitValue(value));
            *checksum_ptr += value;
        }
    }
    // The checksum immediately follows the last UDW. We treat it like another UDW.
    if (next_udw == packet_ptr->data_count) {
        value = FinishChecksum(*checksum_ptr);
        *checksum_ptr = value;
    }
    return value;
}

/**
 * Helper for WriteAncillaryDataPacket: Write the next few 10-bit user data words and update checksum.
 *
 * @param net_word Pointer into network byte order packet buffer, locating where to write.
 * @param next_udw UDW index. Index into the packet's list of user data words, indicating what words to write next.
 * @param packet_ptr The ANC packet data structure to be serialized and added to an ANC payload.
 * @param checksum_ptr Running checksum to be updated.
 *
 * @return Updated UDW index.
 */
static int WriteNextUdws(uint32_t* net_word, int next_udw, const struct AncillaryDataPacket* packet_ptr, uint32_t* checksum_ptr)
{
    assert(next_udw <= MAX_DATA_COUNT);

    switch (next_udw & 0xf) {
        case 0:
        {
            union {
                struct AncillaryUdws0 udws;
                uint32_t bytes;
            } raw;
            raw.udws.udw0 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw1 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw2 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw3 = ChecksumUdw(next_udw, false, packet_ptr, checksum_ptr) >> 6;
            *net_word = htonl(raw.bytes);
            break;
        }
        case 1:
        case 2:
        assert(0); // never happens

        case 3:
        {
            union {
                struct AncillaryUdws1 udws;
                uint32_t bytes;
            } raw;
            raw.udws.udw3 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw4 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw5 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw6 = ChecksumUdw(next_udw, false, packet_ptr, checksum_ptr) >> 4;
            *net_word = htonl(raw.bytes);
            break;
        }
        case 4:
        case 5:
        assert(0); // never happens

        case 6:
        {
            union {
                struct AncillaryUdws2 udws;
                uint32_t bytes;
            } raw;
            raw.udws.udw6 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw7 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw8 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw9 = ChecksumUdw(next_udw, false, packet_ptr, checksum_ptr) >> 2;
            *net_word = htonl(raw.bytes);
            break;
        }
        case 7:
        case 8:
        assert(0); // never happens

        case 9:
        {
            union {
                struct AncillaryUdws3 udws;
                uint32_t bytes;
            } raw;
            raw.udws.udw9 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw10 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw11 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw12 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            *net_word = htonl(raw.bytes);
            break;
        }
        case 10:
        case 11:
        case 12:
        assert(0); // never happens

        case 13:
        {
            union {
                struct AncillaryUdws4 udws;
                uint32_t bytes;
            } raw;
            raw.udws.udw13 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw14 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw15 = ChecksumUdw(next_udw++, true, packet_ptr, checksum_ptr);
            raw.udws.udw0 = ChecksumUdw(next_udw, false, packet_ptr, checksum_ptr) >> 8;
            *net_word = htonl(raw.bytes);
            break;
        }
        case 14:
        case 15:
        default:
        assert(0); // never happens
    }
    return next_udw;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void ParseAncillaryDataPayloadHeader(const uint32_t* payload_net_data_ptr, uint16_t* packet_count_ptr,
    CdiFieldKind* field_kind_ptr)
{
    union {
        struct AncillaryDataPayloadRawHeader header;
        uint32_t bytes;
    } raw;
    raw.bytes = ntohl(payload_net_data_ptr[0]);

    *packet_count_ptr = raw.header.anc_count;
    *field_kind_ptr = (CdiFieldKind)raw.header.f_bits;
}

void ParseAncillaryDataPacketHeader(const uint32_t* packet_net_data_ptr, struct AncillaryDataPacket* packet_ptr,
    struct AncillaryDataPayloadErrors* payload_errors_ptr)
{
    union {
        struct AncillaryDataPacketRawHeader header;
        uint32_t bytes[2];
    } raw;
    raw.bytes[0] = ntohl(packet_net_data_ptr[0]);
    raw.bytes[1] = ntohl(packet_net_data_ptr[1]);

    packet_ptr->is_color_difference_channel = (bool)raw.header.c_bit;
    packet_ptr->line_number = raw.header.line_number;
    packet_ptr->horizontal_offset = raw.header.horizontal_offset;
    packet_ptr->is_valid_source_stream_number = (bool)raw.header.s_bit;
    packet_ptr->source_stream_number = raw.header.stream_number;
    // DID, SDID, and DATA_COUNT are 8 bit values with two parity bits.
    uint16_t raw_did = raw.header.did;
    uint16_t raw_sdid = raw.header.sdid;
    uint16_t raw_data_count = raw.header.data_count;
    packet_ptr->did = raw_did & 0xff;
    packet_ptr->sdid = raw_sdid & 0xff;
    packet_ptr->data_count = raw_data_count & 0xff;
    packet_ptr->user_data[0] = raw.header.udw0 << 8;

    // Start a new checksum.
    payload_errors_ptr->checksum = 0;
    payload_errors_ptr->checksum += raw_did;
    payload_errors_ptr->checksum += raw_sdid;
    payload_errors_ptr->checksum += raw_data_count;

    // Check parity bits.
    CheckParityBits(raw_did, &payload_errors_ptr->parity_errors);
    CheckParityBits(raw_sdid, &payload_errors_ptr->parity_errors);
    CheckParityBits(raw_data_count, &payload_errors_ptr->parity_errors);
}


int ParseAncillaryDataPacket(const uint32_t* packet_net_data_ptr, struct AncillaryDataPacket* packet_ptr,
    struct AncillaryDataPayloadErrors* payload_errors_ptr)
{
    memset(packet_ptr->user_data, 0, sizeof(packet_ptr->user_data));
    ParseAncillaryDataPacketHeader(packet_net_data_ptr, packet_ptr, payload_errors_ptr);
    int next_udw = 0;
    int offset = 2; // The first two words are header.
    // We use '<=' here because ParseNextUdws also parses the checksum.
    while (next_udw <= packet_ptr->data_count) {
        next_udw = ParseNextUdws(packet_net_data_ptr[offset++], next_udw, packet_ptr, &payload_errors_ptr->checksum);
    }

    // Check that check sums match.
    uint16_t checksum = FinishChecksum(payload_errors_ptr->checksum);
    uint16_t packet_checksum = packet_ptr->user_data[packet_ptr->data_count];
    if (checksum != packet_checksum) {
        payload_errors_ptr->checksum_errors++;
    }

    // Clean up: erase the check sum.
    packet_ptr->user_data[packet_ptr->data_count] = 0;

    return offset;
}

int GetAncillaryDataPacketSize(int data_count)
{
    assert(data_count >= 0);
    // header + 10 bits per UDW + 10 bits for checksum
    double num_bits = 62 + 10 * data_count + 10;
    return ceil(num_bits / 32);
}


void WriteAncillaryDataPayloadHeader(uint32_t* payload_net_data_ptr, uint16_t packet_count, CdiFieldKind field_kind)
{
    union {
        struct AncillaryDataPayloadRawHeader header;
        uint32_t bytes;
    } raw = { 0 };
    raw.header.anc_count = packet_count;
    raw.header.f_bits = field_kind;
    payload_net_data_ptr[0] = htonl(raw.bytes);
}


void WriteAncillaryDataPacketHeader(uint32_t *packet_net_data_ptr, const struct AncillaryDataPacket* packet_ptr,
    uint32_t* checksum_ptr)
{
    union {
        struct AncillaryDataPacketRawHeader header;
        uint32_t bytes[2];
    } raw = { 0 };

    raw.header.c_bit = packet_ptr->is_color_difference_channel;
    raw.header.line_number = packet_ptr->line_number;
    raw.header.horizontal_offset = packet_ptr->horizontal_offset;
    raw.header.s_bit = packet_ptr->is_valid_source_stream_number;
    raw.header.stream_number = packet_ptr->source_stream_number;

    uint16_t did_with_parity = WithParityBits(packet_ptr->did);
    uint16_t sdid_with_parity = WithParityBits(packet_ptr->sdid);
    uint16_t data_count_with_parity = WithParityBits(packet_ptr->data_count);
    raw.header.did = did_with_parity;
    raw.header.sdid = sdid_with_parity;
    raw.header.data_count = data_count_with_parity;

    assert(Is10BitValue(packet_ptr->user_data[0]));
    raw.header.udw0 = packet_ptr->user_data[0] >> 8;

    // Start a new checksum.
    *checksum_ptr = 0;
    *checksum_ptr += did_with_parity;
    *checksum_ptr += sdid_with_parity;
    *checksum_ptr += data_count_with_parity;

    // Special case empty packet: need to write header with checksum for udw0.
    if (0 == packet_ptr->data_count) {
        raw.header.udw0 = FinishChecksum(*checksum_ptr) >> 8;
    }

    packet_net_data_ptr[0] = htonl(raw.bytes[0]);
    packet_net_data_ptr[1] = htonl(raw.bytes[1]);
}

int WriteAncillaryDataPacket(uint32_t* packet_net_data_ptr, const struct AncillaryDataPacket* packet_ptr)
{
    uint8_t data_count = packet_ptr->data_count;
    uint32_t checksum;
    WriteAncillaryDataPacketHeader(packet_net_data_ptr, packet_ptr, &checksum);

    int next_udw = 0;
    int offset = 2; // The first two words are used up by the packet header.
    // We use '<=' here because WriteNextUdws also writes the checksum.
    while (next_udw <= data_count) {
        next_udw = WriteNextUdws(&packet_net_data_ptr[offset++], next_udw, packet_ptr, &checksum);
    }

    return offset;
}
