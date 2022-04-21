// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_AVM_PAYLOADS_API_H__
#define CDI_AVM_PAYLOADS_API_H__

/**
 * @file
 * @brief
 * This file declares the public API data types, structures and functions that facilitate parsing and synthesizing
 * payloads that conform to CDI's baseline configuration.
 */

#include "cdi_core_api.h"

#include <stdbool.h>
#include <stdint.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Maximum number of user data words per ancillary data packet (SMPTE ST 291-1 Section 6.6).
#define CDI_MAX_ANC_USER_DATA_WORDS (255)

/**
 * Specifies what video field the timestamp in the ancillary data payload refers to.
 * See also header field "F" in https://datatracker.ietf.org/doc/html/rfc8331#section-2.1.
 */
typedef enum
{
    /// Indicates that no associated video field has been specified. Use with progressive scan mode.
    kCdiFieldKindUnspecified      = 0,
    /// Not a valid choice, receivers should ignore the payload.
    kCdiFieldKindInvalid          = 1,
    /// Indicates that the payload timestamp refers to the first field of an interlaced video signal.
    kCdiFieldKindInterlacedFirst  = 2,
    /// Indicates that the payload timestamp refers to the second field of an interlaced video signal.
    kCdiFieldKindInterlacedSecond = 3
} CdiFieldKind;

/// @brief An ancillary data packet.
/// See https://datatracker.ietf.org/doc/html/rfc8331#section-2.1 for details.
typedef struct
{
    /// Offset (in bytes) into payload buffer to the start of this ANC data packet.
    int packet_offset;
    /// Size (in bytes) of payload chunk that encodes this ANC data packet.
    int packet_size;
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
    /// Bits b7-b0 of the user data words (up to 255). Parity bits b8 and b9 are added by the SDK.
    uint8_t user_data[CDI_MAX_ANC_USER_DATA_WORDS];

}  CdiAvmAncillaryDataPacket;

/**
 * @brief Prototype of callback function used by CdiAvmPacketizeAncillaryData.
 *
 * This callback function is invoked one or multiple times by CdiAvmPacketizeAncillaryData. For each invocation the
 * callback returns either a pointer to the next ancillary data packet structure to be encoded according to RFC 8331,
 * or NULL when the ancillary data payload is complete.
 *
 * The memory referenced by the returned pointer is owned by the application.
 *
 * @param context_ptr Pointer to user-defined data to keep track of state.
 *
 * @return Pointer to an CdiAvmAncillaryDataPacket or NULL.
 */
typedef const CdiAvmAncillaryDataPacket* (CdiAvmPacketizeAncCallback)(void* context_ptr);

/**
 * @brief Prototype of callback function used by CdiAvmUnpacketizeAncillaryData.
 *
 * This callback function is invoked one or multiple times by CdiAvmUnpacketizeAncillaryData. For each invocation the
 * callback receives either a pointer to the next decoded ancillary data packet, or NULL when the ancillary data
 * payload is complete.
 *
 * The field_kind value is read from the ancillary data payload header and is the same for every callback invocation of
 * the payload. Parameters parity_error and checksum_error indicate whether a parity error or checksum error was
 * detected during decoding of the ancillary data packet.
 *
 * The memory referenced by packet_ptr is owned by the SDK.
 *
 * @param context_ptr Pointer to user-defined data to keep track of state.
 * @param field_kind Field kind value from the ancillary data payload header.
 * @param packet_ptr Pointer to the decoded ancillary data packet.
 * @param has_parity_error True when a header parity bit was wrong (did, sdid, and data_count have parity bits).
 * @param has_checksum_error True when the packet's checksum was wrong.
 */
typedef void (CdiAvmUnpacketizeAncCallback)(void* context_ptr, CdiFieldKind field_kind,
    const CdiAvmAncillaryDataPacket* packet_ptr, bool has_parity_error, bool has_checksum_error);

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Calculate the buffer size needed to write an ancillary data payload.
 *
 * @param num_anc_packets The number of ancillary data packets.
 * @param data_counts Number of user data words for each prospective ancillary data packet (see CdiAvmAncillaryDataPacket).
 *
 * @return Required buffer size in bytes.
 */
CDI_INTERFACE int CdiAvmGetAncillaryDataPayloadSize(uint16_t num_anc_packets, uint8_t data_counts[]);

/**
 * Generate an ancillary data payload in the provided buffer. For each ancillary data packet the application's
 * callback returns a pointer to an CdiAvmAncillaryDataPacket object; it returns NULL when the payload is complete. The
 * packets produced by the callback are encoded in order. The whole payload is prefixed with a two-word header.
 * See https://cdi.elemental.com/specs/baseline-ancillary-data for the payload format.
 *
 * The provided buffer must be large enough to hold the encoded data. When the number of user data words of all
 * ancillary data packets in the payload is known in advance, CdiAvmGetAncillaryDataPayloadSize may be used to compute
 * the required buffer size.
 *
 * @param produce_next_packet_ptr Callback provided by the application, producing next ancillary data packet to encode.
 * @param field_kind Field kind of this payload. See CdiFieldKind.
 * @param context_ptr Pointer to user-defined data to keep track of state.
 * @param buffer_ptr Pointer to payload buffer.
 * @param size_in_bytes_ptr Points to size in bytes of payload buffer. Points to size of payload on successful return.
 *
 * @return Status code indicating success or failure.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmPacketizeAncillaryData(CdiAvmPacketizeAncCallback* produce_next_packet_ptr,
    CdiFieldKind field_kind, void* context_ptr, char* buffer_ptr, int* size_in_bytes_ptr);

/**
 * Decode an ancillary data payload from the provided buffer and pass decoded ancillary data packets to the provided
 * callback function for processing. If the payload cannot be decoded then kCdiInvalidPayload is returned. A return code
 * of kCdiStatusRxPayloadError indicates that the payload was decoded with parity or checksum errors detected.
 *
 * @param sgl_ptr Pointer to payload buffer.
 * @param consume_next_packet_ptr Callback provided by the application, consuming next decoded ancillary data packet.
 * @param context_ptr Pointer to user-defined data to keep track of state.
 *
 * @return Status code indicating success or failure.
 */
CDI_INTERFACE CdiReturnStatus CdiAvmUnpacketizeAncillaryData(const CdiSgList* sgl_ptr,
    CdiAvmUnpacketizeAncCallback* consume_next_packet_ptr, void* context_ptr);

#endif // CDI_AVM_PAYLOADS_API_H__
