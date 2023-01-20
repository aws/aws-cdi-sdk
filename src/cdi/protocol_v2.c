// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used for payloads and probe packets using protocol version
 * 2.
 *
 * NOTE: The differences between version 1 and this version are:
 *  PacketCommonHeader.payload_num : Changed from 8-bits to 16-bits.
 *  PacketCommonHeader.packet_id   : New value.
 *  PacketNum0Header.tx_start_time_microseconds : New value.
 *  ControlPacketCommonHeader.senders_stream_identifier : Obsolete, removed.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "protocol.h"

#include <stdbool.h>
#include <string.h>

#include "adapter_api.h"
#include "adapter_efa_probe.h"
#include "internal.h"
#include "private.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Maximum IP string length for protocol version 1.
#define MAX_IP_STRING_LENGTH_V2                 (64)

/// @brief Maximum EFA device GID length for protocol version 1. Contains GID + QPN (see efa_ep_addr).
#define MAX_IPV6_GID_LENGTH_V2                  (32)


/// @brief Maximum stream name string length for protocol version 1.
#define MAX_STREAM_NAME_STRING_LENGTH_V2        (128+10)

// --------------------------------------------------------------------
// All structures in the block below are byte packed (no byte padding).
// --------------------------------------------------------------------
#pragma pack(push, 1)

/**
 * @brief CDI header for payload packets that don't use data offset values (payload type is not kPayloadTypeDataOffset).
 */
typedef struct {
    uint8_t payload_type;         ///< Payload type from CdiPayloadType.
    uint16_t packet_sequence_num; ///< Packet sequence number for the payload.
    uint16_t payload_num;         ///< Payload number this CDI packet is associated with.
    uint32_t packet_id;           ///< Packet ID. Increments by one for each packet across all payloads (wraps at 0).
} PacketCommonHeader;

/**
 * @brief CDI header for payload packets that contains a data offset value (payload type is kPayloadTypeDataOffset).
 */
typedef struct {
    PacketCommonHeader hdr; ///< Header that is common to all packets that contain a CDI header.
    uint32_t payload_data_offset;   ///< Current offset of payload data.
} PacketDataOffsetHeader;

/**
 * @brief CDI header for payload packet #0. This packet never uses payload_data_offset, since it is always zero.
 */
typedef struct {
    PacketCommonHeader hdr; ///< Header that is common to all packets that contain a CDI header.
    uint32_t total_payload_size;     ///< Total size of payload in bytes.
    uint64_t max_latency_microsecs;  ///< Maximum latency payload in microseconds.

    /// Origination RTP timestamp provided by transmitter that is related to the payload.
    CdiPtpTimestamp origination_ptp_timestamp;
    uint64_t payload_user_data;      ///< User data provided by transmitter that is related to the payload.

    uint16_t  extra_data_size;       ///< Size of additional header data in bytes. The data bytes immediately follow
                                     ///< this structure.

    uint64_t tx_start_time_microseconds; ///< Payload Tx start time in microseconds since epoch.
} PacketNum0Header;

/**
 * @brief Union of payload cdi headers. Use to reserve memory that can be used to hold any type of CDI packet header.
 */
typedef struct {
    union {
        PacketNum0Header num0_hdr;     ///< Header of CDI packet number 0 header.
        PacketCommonHeader common_hdr; ///< Header of non data offset cdi packets.
        PacketDataOffsetHeader offset_hdr; ///< Header of data offset cdi packets.
    };
} PacketHeaderUnion;

/// @brief Ensure size of the external define matches the size of our internal structure.
CDI_STATIC_ASSERT(CDI_RAW_PACKET_HEADER_SIZE_V2 == sizeof(PacketHeaderUnion), "The define does not match the structure size!");
// Enable the line below to force a compile error to see the size of the internal structure.
//char __foo[sizeof(PacketHeaderUnion) + 1] = {[sizeof(PacketHeaderUnion)] = ""};

/**
 * @brief Common header for all probe control packets. NOTE: Last digit of Protocol Version is the probe version. This
 * file supports probe version 4.
 *
 * SDK     Protocol Command    Raw Packet
 * Version Version  Header     Header     Comments
 * ------- -------- ---------  ---------- ----------------------------
 * 2.2.0    2.1.4   252 bytes  47 bytes
 * 2.3.0    2.1.4   252 bytes  47 bytes
 * 2.3.1    2.1.4   252 bytes  47 bytes   Not supported (must upgrade)
 * 2.3.2    2.1.4   252 bytes  47 bytes
 */
typedef struct {
    CdiProtocolVersionNumber senders_version; ///< Sender's CDI protocol version number.

    ProbeCommand command; ///< Sender's command
    char senders_ip_str[MAX_IP_STRING_LENGTH_V2];   ///< Sender's IP address.
    uint8_t senders_gid_array[MAX_IPV6_GID_LENGTH_V2]; ///< Sender's device GID. Contains GID + QPN (see efa_ep_addr).
    char senders_stream_name_str[MAX_STREAM_NAME_STRING_LENGTH_V2]; ///< Sender's stream name string.

    /// @brief Sender's control interface destination port. Sent from Tx (client) to Rx (server) so the Rx can establish
    /// a transmit connection back to the Tx.
    uint16_t senders_control_dest_port;

    /// @brief Probe packet number that is incremented for each command sent. Value begins at zero when a new connection
    /// is established and is only unique to the connection.
    uint16_t control_packet_num;
    uint16_t checksum; ///< The checksum for this control packet.
} ControlPacketCommonHeader;

/**
 * @brief Probe command packet that is being transmitted.
 */
typedef struct {
    bool requires_ack; ///< A control flag that, when true, indicates the specified command requires ack.
} ControlPacketCommand;

/**
 * @brief Control ACK packet that is a response for a transmitted command.
 */
typedef struct {
    ProbeCommand ack_command;           ///< Command that ACK corresponds to.
    uint16_t ack_control_packet_num;    ///< Command's control packet number that ACK corresponds to.
} ControlPacketAck;

/**
 * @brief Structure used to hold a union of packets that are transmitted over the control or EFA interface.
 */
typedef struct {
    ControlPacketCommonHeader common_hdr; ///< Common header of all probe packets.
    union {
        ControlPacketCommand command_packet; ///< Command packet transmitted over the control interface.
        ControlPacketAck ack_packet;         ///< ACK packet transmitted over the control interface.
    };
} ProbePacketUnion;

/// @brief Ensure size of the external define matches the size of our internal structure.
CDI_STATIC_ASSERT(CDI_RAW_PROBE_HEADER_SIZE_V2 == sizeof(ProbePacketUnion), "The define does not match the structure size!");
// Enable the line below to force a compile error to see the size of the internal structure.
//char __foo[sizeof(ProbePacketUnion) + 1] = {[sizeof(ProbePacketUnion)] = ""};

#pragma pack(pop)
// --------------------------------------------------------------------
// End of byte packed structures (no byte padding).
// --------------------------------------------------------------------

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Forward declaration of function.
static void HeaderDecode(const void* encoded_data_ptr, int encoded_data_size, CdiDecodedPacketHeader* dest_header_ptr);
/// Forward declaration of function.
static int HeaderInit(void* header_ptr, int header_buffer_size, const TxPayloadState* payload_state_ptr);
/// Forward declaration of function.
static void PacketRxReorderInfo(const CdiRawPacketHeader* header_ptr, CdiPacketRxReorderInfo* ret_info_ptr);
/// Forward declaration of function.
static CdiReturnStatus ProbeHeaderDecode(const void* encoded_data_ptr, int encoded_data_size,
                                         CdiDecodedProbeHeader* dest_header_ptr);
/// Forward declaration of function.
static int ProbeHeaderEncode(const CdiDecodedProbeHeader* src_header_ptr, CdiRawProbeHeader* dest_header_ptr);

/**
 * @brief VTable of APIs used to access payload header and internal data.
 */
static CdiProtocolVTableApi vtable_api = {
    HeaderDecode,
    HeaderInit,
    PacketRxReorderInfo,
    ProbeHeaderDecode,
    ProbeHeaderEncode,
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

// See documentation for CdiPayloadHeaderDecode().
static void HeaderDecode(const void* encoded_data_ptr, int encoded_data_size, CdiDecodedPacketHeader* dest_ptr)
{
    const PacketCommonHeader* hdr_ptr = (PacketCommonHeader*)encoded_data_ptr;

    dest_ptr->payload_type = hdr_ptr->payload_type;
    dest_ptr->packet_sequence_num = hdr_ptr->packet_sequence_num;
    dest_ptr->payload_num = hdr_ptr->payload_num;
    dest_ptr->packet_id = hdr_ptr->packet_id;

    if (0 == hdr_ptr->packet_sequence_num) {
        PacketNum0Header* hdr0_ptr = (PacketNum0Header*)hdr_ptr;
        dest_ptr->encoded_header_size = sizeof(PacketNum0Header) + hdr0_ptr->extra_data_size;
        if (dest_ptr->encoded_header_size > encoded_data_size) {
            assert(false);
        }

        dest_ptr->num0_info.total_payload_size = hdr0_ptr->total_payload_size;
        dest_ptr->num0_info.max_latency_microsecs = hdr0_ptr->max_latency_microsecs;
        dest_ptr->num0_info.origination_ptp_timestamp = hdr0_ptr->origination_ptp_timestamp;
        dest_ptr->num0_info.payload_user_data = hdr0_ptr->payload_user_data;
        dest_ptr->num0_info.extra_data_size = hdr0_ptr->extra_data_size;
        if (hdr0_ptr->extra_data_size) {
            dest_ptr->num0_info.extra_data_ptr = (uint8_t*)hdr0_ptr + sizeof(*hdr0_ptr);
        } else {
            dest_ptr->num0_info.extra_data_ptr = NULL;
        }
        dest_ptr->num0_info.tx_start_time_microseconds = hdr0_ptr->tx_start_time_microseconds;

    } else if (kPayloadTypeDataOffset == hdr_ptr->payload_type) {
        PacketDataOffsetHeader* hdrdo_ptr = (PacketDataOffsetHeader*)hdr_ptr;
        dest_ptr->encoded_header_size = sizeof(PacketDataOffsetHeader);
        assert(dest_ptr->encoded_header_size <= encoded_data_size);

        dest_ptr->data_offset_info.payload_data_offset = hdrdo_ptr->payload_data_offset;
    } else {
        dest_ptr->encoded_header_size = sizeof(PacketCommonHeader);
        assert(dest_ptr->encoded_header_size <= encoded_data_size);
    }
}

// See documentation for CdiPayloadHeaderInit().
static int HeaderInit(void* header_ptr, int header_buffer_size, const TxPayloadState* payload_state_ptr)
{
    (void)header_buffer_size; // Not used in release build, so suppress warning.
    int header_size = 0;
    const CdiPayloadPacketState* packet_state_ptr = &payload_state_ptr->payload_packet_state;

    // All packets contain a common CDI header, so initialize it here.
    PacketCommonHeader* hdr_ptr = (PacketCommonHeader*)header_ptr;
    assert(header_buffer_size >= (int)sizeof(*hdr_ptr));
    hdr_ptr->payload_type = packet_state_ptr->payload_type;
    hdr_ptr->packet_sequence_num = packet_state_ptr->packet_sequence_num;
    hdr_ptr->payload_num = packet_state_ptr->payload_num;
    hdr_ptr->packet_id = packet_state_ptr->packet_id;

    if (0 == packet_state_ptr->packet_sequence_num) {
        // Process first packet of the payload (packet #0).
        PacketNum0Header* hdr0_ptr = (PacketNum0Header*)header_ptr;
        header_size = sizeof(PacketNum0Header);
        assert(header_buffer_size >= header_size);

        // Initialize additional CDI packet #0 header data.
        hdr0_ptr->total_payload_size = payload_state_ptr->source_sgl.total_data_size;
        hdr0_ptr->max_latency_microsecs = payload_state_ptr->max_latency_microsecs;
        hdr0_ptr->origination_ptp_timestamp =
                payload_state_ptr->app_payload_cb_data.core_extra_data.origination_ptp_timestamp;
        hdr0_ptr->payload_user_data = payload_state_ptr->app_payload_cb_data.core_extra_data.payload_user_data;

        hdr0_ptr->extra_data_size = payload_state_ptr->app_payload_cb_data.extra_data_size;
        if (hdr0_ptr->extra_data_size) {
            assert(header_buffer_size >= (header_size + hdr0_ptr->extra_data_size));
            memcpy((uint8_t*)hdr0_ptr + header_size,
                    payload_state_ptr->app_payload_cb_data.extra_data_array,
                    payload_state_ptr->app_payload_cb_data.extra_data_size);
            header_size += payload_state_ptr->app_payload_cb_data.extra_data_size;
        }
        hdr0_ptr->tx_start_time_microseconds = CdiCoreGetUtcTimeMicroseconds();
    } else {
        // Process additional packets of the payload (other than packet #0).
        if (kPayloadTypeDataOffset == packet_state_ptr->payload_type) {
            header_size = sizeof(PacketDataOffsetHeader);
            assert(header_buffer_size >= header_size);
            // Initialize additional CDI data offset header.
            PacketDataOffsetHeader* ptr = (PacketDataOffsetHeader*)header_ptr;
            ptr->payload_data_offset = packet_state_ptr->payload_data_offset;
        } else {
            // Packet is just using the common header, so no additional initialization is required.
            header_size = sizeof(PacketCommonHeader);
        }
    }

    return header_size;
}

// See documentation for PayloadPacketRxReorderInfo().
static void PacketRxReorderInfo(const CdiRawPacketHeader* header_ptr, CdiPacketRxReorderInfo* ret_info_ptr)
{
    const PacketCommonHeader* hdr_ptr = (PacketCommonHeader*)header_ptr;
    ret_info_ptr->payload_num = hdr_ptr->payload_num;
    ret_info_ptr->packet_sequence_num = hdr_ptr->packet_sequence_num;
}

/**
 * @brief Calculate a checksum and return it.
 *
 * @param buffer_ptr Pointer to data to calculate checksum.
 * @param size Size of buffer in bytes.
 *
 * @return Calculated checksum value.
 */
static uint16_t CalculateChecksum(const uint16_t* buffer_ptr, int size)
{
    uint32_t cksum = 0;

    // Sum entire packet.
    while (size > 1) {
        cksum += *buffer_ptr++;
        size -= 2;
    }

    // Pad to 16-bit boundary if necessary.
    if (size == 1) {
        cksum += *(uint8_t*)buffer_ptr;
    }

    // Add carries and do one's complement.
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return (uint16_t)(~cksum);
}

// See documentation for ProtocolProbeHeaderDecode().
static CdiReturnStatus ProbeHeaderDecode(const void* encoded_data_ptr, int encoded_data_size,
                                         CdiDecodedProbeHeader* dest_header_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    ProbePacketUnion* union_ptr = (ProbePacketUnion*)encoded_data_ptr;
    ControlPacketCommonHeader* common_hdr_ptr = &union_ptr->common_hdr;

    if ((int)sizeof(ControlPacketCommonHeader) > encoded_data_size) {
        CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet that is too small[%d]. Expecting[%d] bytes.",
                       encoded_data_size, (int)sizeof(ControlPacketCommonHeader));
        rs = kCdiStatusProbePacketInvalidSize;
    }

    int header_size = 0;
    if (kCdiStatusOk == rs) {
        dest_header_ptr->senders_version = common_hdr_ptr->senders_version;
        dest_header_ptr->command = common_hdr_ptr->command;

        header_size = (int)sizeof(ControlPacketCommonHeader);
        if (common_hdr_ptr->command != kProbeCommandAck) {
            // Encode command data.
            const ControlPacketCommand* cmd_ptr = &union_ptr->command_packet;
            dest_header_ptr->command_packet.requires_ack = cmd_ptr->requires_ack;
            header_size += (int)sizeof(ControlPacketCommand);
        } else {
            // Encode ACK data.
            const ControlPacketAck* ack_ptr = &union_ptr->ack_packet;
            dest_header_ptr->ack_packet.ack_command = ack_ptr->ack_command;
            dest_header_ptr->ack_packet.ack_control_packet_num = ack_ptr->ack_control_packet_num;
            header_size += (int)sizeof(ControlPacketAck);
        }

        // Save away the checksum and then zero it, since the value is used as part of the calculation.
        uint16_t expected_checksum = common_hdr_ptr->checksum;
        common_hdr_ptr->checksum = 0;
        uint16_t checksum = CalculateChecksum(encoded_data_ptr, header_size);
        common_hdr_ptr->checksum =  expected_checksum; // Restore the value.

        if (checksum != common_hdr_ptr->checksum) {
            CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with bad checksum[0x%04x]. Expecting[0x%04x]",
                           common_hdr_ptr->checksum, checksum);
            rs = kCdiStatusProbePacketCrcError;
        }
    }

    if (kCdiStatusOk == rs) {
        bool valid = false; // Reset to false so we can detect a bad command type below without using a default case.
        switch (common_hdr_ptr->command) {
            case kProbeCommandReset:
            case kProbeCommandPing:
            case kProbeCommandConnected:
            case kProbeCommandAck:
            case kProbeCommandProtocolVersion:
                valid = true;
                break;
        }
        if (!valid) {
            // We got here because none of the cases matched, so the command is invalid.
            CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with invalid command type value[%d].",
                           (int)common_hdr_ptr->command);
        } else if (header_size != encoded_data_size) {
            // Make sure the command packet is the expected length.
            CDI_LOG_THREAD(kLogInfo, "Ignoring probe control packet with wrong size[%d]. Expecting[%d]",
                           encoded_data_size, header_size);
            rs = kCdiStatusProbePacketInvalidSize;
        }
    }

    if (kCdiStatusOk == rs) {
        // Copy pointers to these strings and arrays. The caller must not free the memory at encoded_data_ptr until done
        // with the pointers.
        dest_header_ptr->senders_ip_str = common_hdr_ptr->senders_ip_str;
        dest_header_ptr->senders_gid_array = common_hdr_ptr->senders_gid_array;
        dest_header_ptr->senders_stream_name_str = common_hdr_ptr->senders_stream_name_str;

        // Copy additional data.
        dest_header_ptr->senders_control_dest_port = common_hdr_ptr->senders_control_dest_port;
        dest_header_ptr->control_packet_num = common_hdr_ptr->control_packet_num;
    }

    return rs;
}
// See documentation for ProtocolProbeHeaderEncode().
static int ProbeHeaderEncode(const CdiDecodedProbeHeader* src_header_ptr, CdiRawProbeHeader* dest_header_ptr)
{
    ProbePacketUnion* union_ptr = (ProbePacketUnion*)dest_header_ptr;
    ControlPacketCommonHeader* common_hdr_ptr = &union_ptr->common_hdr;

    // Encode common header data.
    common_hdr_ptr->senders_version = src_header_ptr->senders_version;
    common_hdr_ptr->command = src_header_ptr->command;

    // Since the encoded variant is sent to a remote endpoint, it must not contain pointers to data, so we copy the
    // memory.
    if (src_header_ptr->senders_ip_str) {
        memcpy(common_hdr_ptr->senders_ip_str, src_header_ptr->senders_ip_str, sizeof(common_hdr_ptr->senders_ip_str));
    }
    if (src_header_ptr->senders_gid_array) {
        memcpy(common_hdr_ptr->senders_gid_array, src_header_ptr->senders_gid_array,
           sizeof(common_hdr_ptr->senders_gid_array));
    }
    if (src_header_ptr->senders_stream_name_str) {
        memcpy(common_hdr_ptr->senders_stream_name_str, src_header_ptr->senders_stream_name_str,
               sizeof(common_hdr_ptr->senders_stream_name_str));
    }
    common_hdr_ptr->senders_control_dest_port = src_header_ptr->senders_control_dest_port;
    common_hdr_ptr->control_packet_num = src_header_ptr->control_packet_num;

    int header_size = (int)sizeof(ControlPacketCommonHeader);
    if (src_header_ptr->command != kProbeCommandAck) {
        // Encode command data.
        ControlPacketCommand* cmd_ptr = &union_ptr->command_packet;
        cmd_ptr->requires_ack = src_header_ptr->command_packet.requires_ack;
        header_size += (int)sizeof(ControlPacketCommand);
    } else {
        // Encode ACK data.
        ControlPacketAck* ack_ptr = &union_ptr->ack_packet;
        ack_ptr->ack_command = src_header_ptr->ack_packet.ack_command;
        ack_ptr->ack_control_packet_num = src_header_ptr->ack_packet.ack_control_packet_num;
        header_size += (int)sizeof(ControlPacketAck);
    }

    // Calculate the packet checksum.
    common_hdr_ptr->checksum = 0; // Ensure checksum is zeroed before the calculation (since it is part of it).
    common_hdr_ptr->checksum = CalculateChecksum((uint16_t*)dest_header_ptr, header_size);

    return header_size;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool ProtocolVersionSet2(const CdiProtocolVersionNumber* remote_version_ptr, CdiProtocolHandle protocol_handle,
                         CdiProtocolVTableApi** ret_api_ptr)
{
    if (remote_version_ptr->version_num <= 1) {
        *ret_api_ptr = NULL;
        return false;
    }

    // Set default protocol version numbers to the current one.
    protocol_handle->negotiated_version.version_num = CDI_PROTOCOL_VERSION;
    protocol_handle->negotiated_version.major_version_num = CDI_PROTOCOL_MAJOR_VERSION;
    protocol_handle->negotiated_version.probe_version_num = CDI_PROBE_VERSION;

    // Override the default protocol version if the remote's is older.
    if (remote_version_ptr->version_num < CDI_PROTOCOL_VERSION) {
        protocol_handle->negotiated_version = *remote_version_ptr;
    } else if (remote_version_ptr->version_num == CDI_PROTOCOL_VERSION) {
        if (!(remote_version_ptr->major_version_num > CDI_PROTOCOL_MAJOR_VERSION)) {
            // Remote's major version # is less than ours, so use it.
            protocol_handle->negotiated_version.major_version_num = remote_version_ptr->major_version_num;
            protocol_handle->negotiated_version.probe_version_num = remote_version_ptr->probe_version_num;
        } else if ((remote_version_ptr->major_version_num == CDI_PROTOCOL_MAJOR_VERSION) &&
                    (remote_version_ptr->probe_version_num < CDI_PROBE_VERSION)) {
            protocol_handle->negotiated_version.probe_version_num = remote_version_ptr->probe_version_num;
        }
    }

    // Set additional returned protocol data.
    protocol_handle->payload_num_max = UINT16_MAX; // payload_num is 16-bits, so max value is 65535.

    // Set returned pointer to VTable API.
    *ret_api_ptr = &vtable_api;
    return true;
}
