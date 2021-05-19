// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in protcol.c.
 */

#ifndef CDI_PROTOCOL_H__
#define CDI_PROTOCOL_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "cdi_core_api.h"
#include "cdi_pool_api.h"
#include "payload.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

// --------------------------------------------------------------------
// All structures in the block below are byte packed (no byte padding).
// --------------------------------------------------------------------
#pragma pack(push, 1)

/// Forward reference of structure to create pointers later.
typedef struct CdiProtocolVersionNumber CdiProtocolVersionNumber;

/**
 * @brief CDI header used to identify protocol version number information.
 */
struct CdiProtocolVersionNumber {
    uint8_t version_num;       ///< CDI protocol version number.
    uint8_t major_version_num; ///< CDI protocol major version number.
    uint8_t probe_version_num; ///< CDI probe version number.
};

#pragma pack(pop)
// --------------------------------------------------------------------
// End of byte packed structures (no byte padding).
// --------------------------------------------------------------------

/**
 * @brief CDI decoded header for payload packets that contains a data offset value (payload type is
 * kPayloadTypeDataOffset). Decoded headers are protocol independent.
 */
typedef struct {
    int payload_data_offset; ///< Current offset of payload data.
} CdiDecodedPacketDataOffsetInfo;

/**
 * @brief CDI decoded header for payload packet #0. This packet never uses payload_data_offset, since it is always zero.
 * Decoded headers are protocol independent.
 */
typedef struct {
    int total_payload_size;         ///< Total size of payload in bytes.
    uint64_t max_latency_microsecs; ///< Maximum latency payload in microseconds.

    /// Origination RTP timestamp provided by transmitter that is related to the payload.
    CdiPtpTimestamp origination_ptp_timestamp;
    uint64_t payload_user_data; ///< User data provided by transmitter that is related to the payload.

    int extra_data_size;        ///< Size of additional header data in bytes.
    void* extra_data_ptr;       ///< Pointer to extra data.

     /// Payload Tx start time in microseconds since epoch. NOTE: Only valid for protocols 2 and later.
    uint64_t tx_start_time_microseconds;
} CdiDecodedPacketNum0Info;

/**
 * @brief Union of decoded CDI packet headers. Use to reserve memory that can be used to hold any type of decoded CDI
 * packet header. Decoded headers are protocol independent.
 */
typedef struct {
    CdiPayloadType payload_type; ///< Payload type from CdiPayloadType.
    int packet_sequence_num;     ///< Packet sequence number for the payload.
    int payload_num;             ///< Payload number this CDI packet is associated with.
    int encoded_header_size;     ///< Size of encoded header in bytes.

    /// @brief Packet ID. Increments by 1 for each packet across all payloads (wraps at 0). NOTE: Only valid for
    /// protocols 2 and later.
    uint32_t packet_id;

    union {
        CdiDecodedPacketDataOffsetInfo data_offset_info; ///< Valid when payload_type is kPayloadTypeDataOffset.
        CdiDecodedPacketNum0Info num0_info;  ///< Valid when payload_type is kPayloadTypeData and packet_sequence_num=0.
    };
} CdiDecodedPacketHeader;

/**
 * @brief Define the size of the PacketHeaderUnion structure used in protocol V1. This is done so the size of the
 * structure is known at compile time without having to expose the contents of it in a header file.
 */
#define CDI_RAW_PACKET_HEADER_SIZE_V1   (34)

/**
 * @brief Define the size of the PacketHeaderUnion structure used in protocol V2. This is done so the size of the
 * structure is known at compile time without having to expose the contents of it in a header file.
 */
#define CDI_RAW_PACKET_HEADER_SIZE_V2   (47)

/**
 * @brief Union of raw CDI packet headers. Use to reserve memory that can be used to hold any type of raw CDI packet
 * header. Each protocol version uses a specific data format and is kept internal. Use PayloadHeaderDecode() to
 * decoded the raw packet header into CdiDecodedPacketHeader, which is protocol independent. Use PayloadHeaderInit()
 * to convert a CdiDecodedPacketHeader into this format.
 */
struct CdiRawPacketHeader {
    union {
        uint8_t header_v1[CDI_RAW_PACKET_HEADER_SIZE_V1 + MAX_CDI_PACKET_EXTRA_DATA]; ///< For protocol version 1.
        uint8_t header_v2[CDI_RAW_PACKET_HEADER_SIZE_V2 + MAX_CDI_PACKET_EXTRA_DATA]; ///< For protocol version 2.
    };
};

/**
 * @brief Structure used to hold packet data used by Rx packet reordering.
 */
typedef struct {
    int payload_num;         ///< Payload number the packet is associated with.
    int packet_sequence_num; ///< Packet sequence number for the payload.
} CdiPacketRxReorderInfo;

/**
 * @brief This enumeration is used in the ProbePacketHeader structure to indicate a probe command.
 * NOTE: Any changes made here MUST also be made to "probe_command_key_array".
 */
typedef enum {
    kProbeCommandReset = 1, ///< Request to reset the connection. Start with 1 so no commands have the value 0.
    kProbeCommandPing,      ///< Request to ping the connection.
    kProbeCommandConnected, ///< Notification that connection has been established (probe has completed).
    kProbeCommandAck,       ///< Packet is an ACK response to a previously sent command.
    kProbeCommandProtocolVersion, ///< Packet contains protocol version of sender.
} ProbeCommand;

/**
 * @brief Probe command packet that is being transmitted.
 */
typedef struct {
    bool requires_ack; ///< A control flag that, when true, indicates the specified command requires ack.
} CdiDecodedProbeCommand;

/**
 * @brief Control ACK packet that is a response for a transmitted command.
 */
typedef struct {
    ProbeCommand ack_command;           ///< Command that ACK corresponds to.
    uint16_t ack_control_packet_num;    ///< Command's control packet number that ACK corresponds to.
} CdiDecodedProbeAck;

/**
 * @brief Union of decoded probe headers. Use to reserve memory that can be used to hold any type of decoded CDI probe
 * header. Decoded headers are protocol independent.
 */
typedef struct {
    CdiProtocolVersionNumber senders_version; ///< Sender's CDI protocol version number.

    ProbeCommand command; ///< Sender's command
    union {
        ///< Valid if command is not kProbeCommandAck. Command packet transmitted over the control interface.
        CdiDecodedProbeCommand command_packet;
        ///< Valid if command is kProbeCommandAck. ACK packet transmitted over the control interface.
        CdiDecodedProbeAck ack_packet;
    };

    const char* senders_ip_str;          ///< Pointer to sender's IP address.
    const uint8_t* senders_gid_array;    ///< Pointer to sender's device GID. contains GID + QPN (see efa_ep_addr).
    const char* senders_stream_name_str; ///< Pointer to sender's stream name string.

    /// @brief Sender's control interface destination port. Sent from Tx (client) to Rx (server) so the Rx can establish
    /// a transmit connection back to the Tx.
    uint16_t senders_control_dest_port;

    /// @brief Probe packet number that is incremented for each command sent. Value begins at zero when a new connection
    /// is established and is only unique to the connection.
    uint16_t control_packet_num;
} CdiDecodedProbeHeader;

/**
 * @brief Define the size of the ProbeHeaderUnion structure used in protocol V1. This is done so the size of the
 * structure is known at compile time without having to expose the contents of it in a header file.
 */
#define CDI_RAW_PROBE_HEADER_SIZE_V1   (257)

/**
 * @brief Define the size of the ProbeHeaderUnion structure used in protocol V2. This is done so the size of the
 * structure is known at compile time without having to expose the contents of it in a header file.
 */
#define CDI_RAW_PROBE_HEADER_SIZE_V2   (253)

/**
 * @brief Packet format used by probe when sending probe packets over the EFA interface.
 */
typedef struct {
    uint16_t packet_sequence_num; ///< Probe packet sequence number.
    uint8_t efa_data[EFA_PROBE_PACKET_DATA_SIZE]; ///< Probe packet data.
} EfaProbePacket;

/**
 * @brief Union of raw probe headers. Use to reserve memory that can be used to hold any type of raw probe header. Each
 * protocol version uses a specific data format and is kept internal. Use ProtocolPayloadHeaderDecode() to decoded the
 * raw packet header into CdiDecodedProbeHeader, which is protocol independent. Use ProtocolProbeHeaderEncode() to convert
 * a CdiDecodedProbeHeader into this format.
 */
struct CdiRawProbeHeader {
    union {
        uint8_t header_v1[CDI_RAW_PROBE_HEADER_SIZE_V1]; ///< For protocol version 1.
        uint8_t header_v2[CDI_RAW_PROBE_HEADER_SIZE_V2]; ///< For protocol version 2.
        EfaProbePacket efa_packet; ///< Packet used for EFA probe transmitted over the EFA interface.
    };
};
/// Forward reference of structure to create pointers later.
typedef struct CdiRawProbeHeader CdiRawProbeHeader;

/// Forward reference of structure to create pointers later.
typedef struct CdiConnectionState CdiConnectionState;
/// Forward reference of structure to create pointers later.
typedef struct TxPayloadState TxPayloadState;

/**
 * @brief Structure used to hold negotiated protocol version information.
 */
struct CdiProtocol {
    CdiProtocolVersionNumber negotiated_version; ///< Negotiated protocol version number.
    int payload_num_max; ///< Maximum value for payload number. See CdiDecodedPacketHeader.payload_num.
};
/// Forward reference of structure to create pointers later.
typedef struct CdiProtocol* CdiProtocolHandle;

/// Prototype of function used for protocol version VTable API.
typedef void (*VtblPayloadHeaderDecode)(const void* encoded_data_ptr, int encoded_data_size,
                                        CdiDecodedPacketHeader* dest_header_ptr);
/// Prototype of function used for protocol version VTable API.
typedef int (*VtblPayloadHeaderInit)(CdiRawPacketHeader* header_ptr, const TxPayloadState* payload_state_ptr);
/// Prototype of function used for protocol version VTable API.
typedef void (*VtblPayloadPacketRxReorderInfo)(const CdiRawPacketHeader* header_ptr,
                                               CdiPacketRxReorderInfo* ret_info_ptr);
/// Prototype of function used for protocol version VTable API.
typedef CdiReturnStatus (*VtblProbeHeaderDecode)(const void* encoded_data_ptr, int encoded_data_size,
                                                 CdiDecodedProbeHeader* dest_header_ptr);
/// Prototype of function used for protocol version VTable API.
typedef int (*VtblProbeHeaderEncode)(const CdiDecodedProbeHeader* state_ptr, CdiRawProbeHeader* dest_header_ptr);

/**
 * @brief Type used to hold V-table of APIs that must be implemented by payload protocol versions.
 */
typedef struct {
    VtblPayloadHeaderDecode header_decode; ///< Function pointer used to decode a raw packet header.
    VtblPayloadHeaderInit header_init; ///< Function pointer used to initialize a raw packet header.
    VtblPayloadPacketRxReorderInfo rx_reorder_info; ///< Function pointer used to get packet Rx reorder information.
    VtblProbeHeaderDecode probe_decode; ///< Function pointer used to decode a raw probe header.
    VtblProbeHeaderEncode probe_encode; ///< Function pointer used to encode a raw probe header.
} CdiProtocolVTableApi;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// Forward reference of structure to allow pointer creation.
typedef struct TxPayloadState TxPayloadState;

/**
 * @brief Create a protocol version using the specified remote protocol version. The version is compared against the
 * versions in the current SDK and the most recent compatible version is returned.
 *
 * @param remote_version_ptr Pointer to remote's protocol version.
 * @param ret_handle_ptr Address where to write returned protocol handle.
 */
void ProtocolVersionSet(const CdiProtocolVersionNumber* remote_version_ptr,
                        CdiProtocolHandle* ret_handle_ptr);

/**
 * @brief Freeup resources used by a protocol.
 *
 * @param protocol_handle Handle of protocol version.
 */
void ProtocolVersionDestroy(CdiProtocolHandle protocol_handle);

/**
 * @brief Decode an encoded raw header into a header structure that is protocol version independent.
 *
 * @param protocol_handle Handle of protocol version.
 * @param encoded_data_ptr Pointer to encoder header data.
 * @param encoded_data_size Size of encoded header data in bytes.
 * @param dest_header_ptr Address where to write decoded header data.
 */
void ProtocolPayloadHeaderDecode(CdiProtocolHandle protocol_handle, void* encoded_data_ptr, int encoded_data_size,
                                 CdiDecodedPacketHeader* dest_header_ptr);
/**
 * @brief Initialize raw packet encoded header data using the specified protocol and packet state data.
 *
 * @param protocol_handle Handle of protocol version.
 * @param header_ptr Address where to write raw packet header.
 * @param payload_state_ptr Pointer to TX payload state data.
 *
 * @return Size of payload header in bytes.
 */
int ProtocolPayloadHeaderInit(CdiProtocolHandle protocol_handle, CdiRawPacketHeader* header_ptr,
                              const TxPayloadState* payload_state_ptr);

/**
 * @brief Get Rx reorder information for the specified packet.
 *
 * @param protocol_handle Handle of protocol version.
 * @param header_ptr Pointer to raw encoded packet header to get Rx reorder information from.
 * @param ret_info_ptr Address where to write returned Rx reorder info.
 */
void ProtocolPayloadPacketRxReorderInfo(CdiProtocolHandle protocol_handle, const CdiRawPacketHeader* header_ptr,
                                        CdiPacketRxReorderInfo* ret_info_ptr);

/**
 * @brief Decode an encoded raw probe header into a header structure that is protocol version independent.
 *
 * @param encoded_data_ptr Pointer to encoder header data. NOTE: The caller must not free this data until done with the
 * decoded version, since some of the decoded values contain pointers to it.
 * @param encoded_data_size Size of encoded header data in bytes.
 * @param dest_header_ptr Address where to write decoded header data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus ProtocolProbeHeaderDecode(const void* encoded_data_ptr, int encoded_data_size,
                                          CdiDecodedProbeHeader* dest_header_ptr);

/**
 * @brief Encode raw payload header data using the specified protocol and probe state data.
 *
 * @param protocol_handle Handle of protocol version.
 * @param src_header_ptr Pointer to source header data.
 * @param dest_header_ptr Address where to write raw probe header.
 *
 * @return Size of protocol header in bytes.
 */
int ProtocolProbeHeaderEncode(CdiProtocolHandle protocol_handle, CdiDecodedProbeHeader* src_header_ptr,
                              CdiRawProbeHeader* dest_header_ptr);

#endif  // CDI_PROTOCOL_H__
