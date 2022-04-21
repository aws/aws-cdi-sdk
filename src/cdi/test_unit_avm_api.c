// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains unit tests for the AVM API.
 */

#include "anc_payloads.h"
#include "cdi_avm_api.h"
#include "cdi_avm_payloads_api.h"
#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_core_api.h"
#include "cdi_logger_api.h"
#include "utilities_api.h"

#include <assert.h>
#include <arpa/inet.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Control structure for PacketizeAncCb and UnpacketizeAncCb callbacks.
struct GenPacketControl
{
    CdiFieldKind field_kind; ///< Field kind reported to callback.
    int next_anc_packet; ///< Identifies next packet.
    int max_anc_packet; ///< Stop when reaching max_anc_packet.
    int num_mismatched_packets; ///< Counts unexpected differences; used by Unpacketize test only.
    int num_parity_errors; ///< Number of data parity errors observed.
    int num_checksum_errors; ///< Number of checksum errors observed.
    int last_packet_offset; ///< Last packet offset observed.
    int last_packet_size; ///< Last packet size observed.
};

/// Control structure for UnpacketizeAncCb2 callback.
struct GenPacketControl2
{
    char* buffer_ptr; ///< Buffer to copy to.
    const CdiSgList* sgl_ptr; ///< Buffer to copy from.
    bool error_occurred; ///< Flag indicating if there was a copying error.
};

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Packetized Anc payload with seven Anc packets generated with GenerateAncDataPacket.
static char anc_payload[] = {
    0x00, 0x07, 0x80, 0x00, 0x80, 0x20, 0x2f, 0x8b, 0x98, 0xd6, 0x28, 0x19, 0x1f, 0x48, 0x22, 0x18,
    0x89, 0x23, 0x59, 0x2d, 0x40, 0x00, 0x00, 0x00, 0x80, 0x20, 0x2f, 0x8b, 0x98, 0xd6, 0x2b, 0xfd,
    0x1f, 0x48, 0x22, 0x18, 0x89, 0x23, 0x59, 0x26, 0x54, 0x9a, 0x27, 0x8a, 0x12, 0x94, 0xaa, 0x2b,
    0x4b, 0x26, 0xa8, 0xb9, 0x2f, 0x8c, 0x13, 0x14, 0xca, 0x33, 0x4d, 0x23, 0x58, 0xd9, 0x37, 0x4e,
    0x23, 0x98, 0xe9, 0x3b, 0x8f, 0x13, 0xd4, 0xfa, 0x3f, 0x50, 0x24, 0x19, 0x09, 0x43, 0x91, 0x14,
    0x55, 0x1a, 0x47, 0x92, 0x14, 0x95, 0x2a, 0x4b, 0x53, 0x24, 0xd9, 0x39, 0x4f, 0x94, 0x15, 0x15,
    0x4a, 0x53, 0x55, 0x25, 0x59, 0x59, 0x57, 0x56, 0x25, 0x99, 0x69, 0x5b, 0x97, 0x15, 0xd5, 0x7a,
    0x5f, 0x98, 0x16, 0x15, 0x8a, 0x63, 0x59, 0x26, 0x59, 0x99, 0x67, 0x5a, 0x26, 0x99, 0xa9, 0x6b,
    0x9b, 0x16, 0xd5, 0xba, 0x6f, 0x5c, 0x27, 0x19, 0xc9, 0x73, 0x9d, 0x17, 0x55, 0xda, 0x77, 0x9e,
    0x17, 0x95, 0xea, 0x7b, 0x5f, 0x27, 0xd9, 0xf9, 0x7f, 0x60, 0x28, 0x1a, 0x09, 0x83, 0xa1, 0x18,
    0x56, 0x1a, 0x87, 0xa2, 0x18, 0x96, 0x2a, 0x8b, 0x63, 0x28, 0xda, 0x39, 0x8f, 0xa4, 0x19, 0x16,
    0x4a, 0x93, 0x65, 0x29, 0x5a, 0x59, 0x97, 0x66, 0x29, 0x9a, 0x69, 0x9b, 0xa7, 0x19, 0xd6, 0x7a,
    0x9f, 0xa8, 0x1a, 0x16, 0x8a, 0xa3, 0x69, 0x2a, 0x5a, 0x99, 0xa7, 0x6a, 0x2a, 0x9a, 0xa9, 0xab,
    0xab, 0x1a, 0xd6, 0xba, 0xaf, 0x6c, 0x2b, 0x1a, 0xc9, 0xb3, 0xad, 0x1b, 0x56, 0xda, 0xb7, 0xae,
    0x1b, 0x96, 0xea, 0xbb, 0x6f, 0x2b, 0xda, 0xf9, 0xbf, 0xb0, 0x1c, 0x17, 0x0a, 0xc3, 0x71, 0x2c,
    0x5b, 0x19, 0xc7, 0x72, 0x2c, 0x9b, 0x29, 0xcb, 0xb3, 0x1c, 0xd7, 0x3a, 0xcf, 0x74, 0x2d, 0x1b,
    0x49, 0xd3, 0xb5, 0x1d, 0x57, 0x5a, 0xd7, 0xb6, 0x1d, 0x97, 0x6a, 0xdb, 0x77, 0x2d, 0xdb, 0x79,
    0xdf, 0x78, 0x2e, 0x1b, 0x89, 0xe3, 0xb9, 0x1e, 0x57, 0x9a, 0xe7, 0xba, 0x1e, 0x97, 0xaa, 0xeb,
    0x7b, 0x2e, 0xdb, 0xb9, 0xef, 0xbc, 0x1f, 0x17, 0xca, 0xf3, 0x7d, 0x2f, 0x5b, 0xd9, 0xf7, 0x7e,
    0x2f, 0x9b, 0xe9, 0xfb, 0xbf, 0x1f, 0xd7, 0xfa, 0xff, 0x80, 0x10, 0x14, 0x0a, 0x03, 0x41, 0x20,
    0x58, 0x19, 0x07, 0x42, 0x20, 0x98, 0x29, 0x0b, 0x83, 0x10, 0xd4, 0x3a, 0x0f, 0x44, 0x21, 0x18,
    0x49, 0x13, 0x85, 0x11, 0x54, 0x5a, 0x17, 0x86, 0x11, 0x94, 0x6a, 0x1b, 0x47, 0x17, 0x08, 0xd8,
    0x80, 0x20, 0x2f, 0x8b, 0x98, 0xd6, 0x28, 0x3d, 0x1f, 0x48, 0x22, 0x18, 0x89, 0x23, 0x59, 0x26,
    0x54, 0x9a, 0x27, 0x8a, 0x12, 0x94, 0xaa, 0x2b, 0x4b, 0x26, 0x6b, 0x1c, 0x80, 0x20, 0x2f, 0x8b,
    0x98, 0xd6, 0x28, 0x01, 0xc5, 0x00, 0x00, 0x00, 0x80, 0x20, 0x2f, 0x8b, 0x98, 0xd6, 0x28, 0x19,
    0x1f, 0x48, 0x22, 0x18, 0x89, 0x23, 0x5a, 0x2d, 0x80, 0x00, 0x00, 0x00, 0x80, 0x20, 0x2f, 0x8b,
    0x98, 0xd6, 0x2b, 0xfd, 0x1f, 0x48, 0x22, 0x18, 0x89, 0x23, 0x5a, 0x26, 0x94, 0x9a, 0x27, 0x8a,
    0x12, 0x94, 0xaa, 0x2b, 0x4b, 0x26, 0x68, 0xb9, 0x2f, 0x8c, 0x13, 0x14, 0xca, 0x33, 0x4d, 0x23,
    0x58, 0xd9, 0x37, 0x4e, 0x23, 0x98, 0xe9, 0x3b, 0x8f, 0x13, 0xd4, 0xfa, 0x3f, 0x50, 0x24, 0x19,
    0x09, 0x43, 0x91, 0x14, 0x55, 0x1a, 0x47, 0x92, 0x14, 0x95, 0x2a, 0x4b, 0x53, 0x24, 0xd9, 0x39,
    0x4f, 0x94, 0x15, 0x15, 0x4a, 0x53, 0x55, 0x25, 0x59, 0x59, 0x57, 0x56, 0x25, 0x99, 0x69, 0x5b,
    0x97, 0x15, 0xd5, 0x7a, 0x5f, 0x98, 0x16, 0x15, 0x8a, 0x63, 0x59, 0x26, 0x59, 0x99, 0x67, 0x5a,
    0x26, 0x99, 0xa9, 0x6b, 0x9b, 0x16, 0xd5, 0xba, 0x6f, 0x5c, 0x27, 0x19, 0xc9, 0x73, 0x9d, 0x17,
    0x55, 0xda, 0x77, 0x9e, 0x17, 0x95, 0xea, 0x7b, 0x5f, 0x27, 0xd9, 0xf9, 0x7f, 0x60, 0x28, 0x1a,
    0x09, 0x83, 0xa1, 0x18, 0x56, 0x1a, 0x87, 0xa2, 0x18, 0x96, 0x2a, 0x8b, 0x63, 0x28, 0xda, 0x39,
    0x8f, 0xa4, 0x19, 0x16, 0x4a, 0x93, 0x65, 0x29, 0x5a, 0x59, 0x97, 0x66, 0x29, 0x9a, 0x69, 0x9b,
    0xa7, 0x19, 0xd6, 0x7a, 0x9f, 0xa8, 0x1a, 0x16, 0x8a, 0xa3, 0x69, 0x2a, 0x5a, 0x99, 0xa7, 0x6a,
    0x2a, 0x9a, 0xa9, 0xab, 0xab, 0x1a, 0xd6, 0xba, 0xaf, 0x6c, 0x2b, 0x1a, 0xc9, 0xb3, 0xad, 0x1b,
    0x56, 0xda, 0xb7, 0xae, 0x1b, 0x96, 0xea, 0xbb, 0x6f, 0x2b, 0xda, 0xf9, 0xbf, 0xb0, 0x1c, 0x17,
    0x0a, 0xc3, 0x71, 0x2c, 0x5b, 0x19, 0xc7, 0x72, 0x2c, 0x9b, 0x29, 0xcb, 0xb3, 0x1c, 0xd7, 0x3a,
    0xcf, 0x74, 0x2d, 0x1b, 0x49, 0xd3, 0xb5, 0x1d, 0x57, 0x5a, 0xd7, 0xb6, 0x1d, 0x97, 0x6a, 0xdb,
    0x77, 0x2d, 0xdb, 0x79, 0xdf, 0x78, 0x2e, 0x1b, 0x89, 0xe3, 0xb9, 0x1e, 0x57, 0x9a, 0xe7, 0xba,
    0x1e, 0x97, 0xaa, 0xeb, 0x7b, 0x2e, 0xdb, 0xb9, 0xef, 0xbc, 0x1f, 0x17, 0xca, 0xf3, 0x7d, 0x2f,
    0x5b, 0xd9, 0xf7, 0x7e, 0x2f, 0x9b, 0xe9, 0xfb, 0xbf, 0x1f, 0xd7, 0xfa, 0xff, 0x80, 0x10, 0x14,
    0x0a, 0x03, 0x41, 0x20, 0x58, 0x19, 0x07, 0x42, 0x20, 0x98, 0x29, 0x0b, 0x83, 0x10, 0xd4, 0x3a,
    0x0f, 0x44, 0x21, 0x18, 0x49, 0x13, 0x85, 0x11, 0x54, 0x5a, 0x17, 0x86, 0x11, 0x94, 0x6a, 0x1b,
    0x47, 0x17, 0x08, 0xe8, 0x80, 0x20, 0x2f, 0x8b, 0x98, 0xd6, 0x28, 0x3d, 0x1f, 0x48, 0x22, 0x18,
    0x89, 0x23, 0x5a, 0x26, 0x94, 0x9a, 0x27, 0x8a, 0x12, 0x94, 0xaa, 0x2b, 0x4b, 0x26, 0xab, 0x4c,
};

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Helper for TestUnpacketizeAncillaryData: wrap payload in an SGL.
 *
 * @param which Chooses a case.
 *
 * @return Pointer to an SGL.
 */
static const CdiSgList* MakeAncillaryDataPayload(int which)
{
    static CdiSglEntry entry3 = {
        .address_ptr = anc_payload + 400,
        .size_in_bytes = sizeof(anc_payload) - 400,
        .internal_data_ptr = NULL,
        .next_ptr = NULL
    };
    static CdiSglEntry entry2 = {
        .address_ptr = anc_payload + 200,
        .size_in_bytes = 200,
        .internal_data_ptr = NULL,
        .next_ptr = &entry3
    };
    static CdiSglEntry entry1 = {
        .address_ptr = anc_payload,
        .size_in_bytes = 200,
        .internal_data_ptr = NULL,
        .next_ptr = &entry2
    };
    static CdiSgList sgl = {
        .sgl_head_ptr = &entry1,
        .sgl_tail_ptr = &entry3,
        .total_data_size = sizeof(anc_payload),
        .internal_data_ptr = NULL
    };

    switch (which) {
        case 1:
            // Truncate.
            sgl.sgl_head_ptr = sgl.sgl_tail_ptr;
            sgl.total_data_size = 2;
            sgl.sgl_head_ptr->size_in_bytes = sgl.total_data_size;
            break;
        case 2:
            // Truncate.
            sgl.sgl_head_ptr = sgl.sgl_tail_ptr;
            sgl.total_data_size = 101;
            sgl.sgl_head_ptr->size_in_bytes = sgl.total_data_size;
            break;
        case 3:
            // Truncate.
            sgl.sgl_head_ptr = sgl.sgl_tail_ptr;
            sgl.total_data_size = 100;
            sgl.sgl_head_ptr->size_in_bytes = sgl.total_data_size;
            break;
        case 4:
            // Restore.
            sgl.sgl_head_ptr = &entry1;
            sgl.total_data_size = sizeof(anc_payload);
            sgl.sgl_tail_ptr->size_in_bytes = sizeof(anc_payload) - 400;
            break;
        default:
            assert(0);
    }
    return &sgl;
}

/**
 * Debug helper: Print a word's bit representation.
 *
 * @param word_ The 32-bit word to print.
 */
static const char* WordBits(uint32_t word_)
{
    static char buf[40];

    uint32_t word = word_;
    uint8_t* bytes = (uint8_t*)&word;
    int i = 0;
    for (int j = 0; j < 4; ++j) {
        for (int k=7; k >=0; --k, ++i) {
            uint8_t bit = (bytes[j] >> k) & 1;
            buf[i] = bit == 0 ? '0' : '1';
        }
        buf[i++] = ' ';
    }
    buf[i] = 0;
    return buf;
}

/// Helper macro.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            CDI_LOG_THREAD(kLogError, "%s at line [%d] failed", #condition, __LINE__); \
            pass = false; \
        } \
    } while (false);


/// Helper macro: check error counts.
#define CHECK_PAYLOAD_ERRORS(ERRORS, EXPECTED_CHECKSUM_ERRORS, EXPECTED_DATA_COUNT_ERRORS) \
    do { \
        if (!CheckPayloadErrors(&(ERRORS), EXPECTED_CHECKSUM_ERRORS, EXPECTED_DATA_COUNT_ERRORS, __LINE__)) { \
            pass = false; \
        } \
    } while (false)

/**
 * Check function used by macro CHECK_PAYLOAD_ERRORS.
 *
 * @param errors_ptr Pointer to error counters.
 * @param expected_checksum_errors Number of expected checksum errors.
 * @param expected_parity_errors Number of expected parity errors.
 * @param line_number Source line number to print in an error message.
 *
 * @return True if and only if error counts match expected error counts.
 */
static bool CheckPayloadErrors(const struct AncillaryDataPayloadErrors* errors_ptr, int expected_checksum_errors,
    int expected_parity_errors, int line_number)
{
    bool pass = true;
    if (errors_ptr->checksum_errors != expected_checksum_errors) {
        CDI_LOG_THREAD(kLogError, "Got [%d] checksum errors but expected [%d] at line [%d]", errors_ptr->checksum_errors,
            expected_checksum_errors, line_number);
        pass = false;
    }
    if (errors_ptr->parity_errors != expected_parity_errors) {
        CDI_LOG_THREAD(kLogError, "Got [%d] parity errors but expected [%d] at line [%d]",
            errors_ptr->parity_errors, expected_parity_errors, line_number);
        pass = false;
    }
    return pass;
}


/// Helper macro: check that error counts are zero and reset them.
#define CHECK_NO_PAYLOAD_ERRORS(errors) \
    do { \
        CHECK_PAYLOAD_ERRORS(errors, 0, 0); \
        memset(&errors, 0, sizeof(errors)); \
    } while (false)


/// Helper macro: check that two objects are equal.
#define CHECK_EQUAL_OBJECTS(OBJ1, OBJ2)  CHECK(CheckEqualObjects(&(OBJ1), &(OBJ2), sizeof(OBJ1), 10))

/**
 * Helper for CHECK_EQUAL_OBJECTS macro: Bit-compare to objects of same size and print their bits if different.
 *
 * @param obj1_ptr Pointer to first object.
 * @param obj2_ptr Pointer to second object.
 * @param n Size in bytes of object.
 * @param max_lines Maximum number of lines to print.
 */
static bool CheckEqualObjects(const void* obj1_ptr, const void* obj2_ptr, size_t n, int max_lines)
{
    bool equal = 0 == memcmp(obj1_ptr, obj2_ptr, n);
    if (!equal) {
        const uint32_t* obj1 = obj1_ptr;
        const uint32_t* obj2 = obj2_ptr;
        char buf[256] = { 0 };
        CDI_LOG_THREAD(kLogError, "Object comparison failed.");
        fprintf(stderr, "      Left-hand object                     | Right-hand object                    | XOR\n");
        int num_lines = (int)n/4 < max_lines ? (int)n/4 : max_lines;
        for (int i=0; i < num_lines; ++i) {
            int pos = snprintf(buf, sizeof(buf), "%3d : %s |", i, WordBits(obj1[i]));
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %s |", WordBits(obj2[i]));
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %s\n", WordBits(obj1[i] ^ obj2[i]));
            fprintf(stderr, "%s", buf);
        }
    }
    return equal;
}

/// Test for CdiAvmGetBaselineUnitSize.
static bool TestGetBaselineUnitSize()
{
    bool pass = true;
    int unit_size = 0;

    // Test unit size for video payload type.
    CdiAvmBaselineConfig video_config = {
        .payload_type = kCdiAvmVideo,
        .video_config = {
            .version = {2, 0},
            .sampling = kCdiAvmVidYCbCr422,
            .depth = kCdiAvmVidBitDepth12
        }
    };
    CdiAvmGetBaselineUnitSize(&video_config, &unit_size);
    pass = pass && unit_size == 48;

    // Test unit size for audio payload type.
    CdiAvmBaselineConfig audio_config = {
        .payload_type = kCdiAvmAudio,
        .audio_config = {
            .version = {2, 0},
            .grouping = kCdiAvmAudio71,
            .sample_rate_khz = kCdiAvmAudioSampleRate96kHz
        }
    };
    CdiAvmGetBaselineUnitSize(&audio_config, &unit_size);
    pass = pass && unit_size == 8 * 3 * 8; // Audio71 = 8 channels

    // Test unit size for ancillary payload type.
    CdiAvmBaselineConfig ancillary_config = {
        .payload_type = kCdiAvmAncillary,
        .ancillary_data_config = {
            .version = {2, 0}
        }
    };
    CdiAvmGetBaselineUnitSize(&ancillary_config, &unit_size);
    pass = pass && unit_size == 32; // always 32

    return pass;
}

/// Test for CdiAvmValidateBaselineVersionString.
static bool TestValidateBaselineVersion()
{
    bool pass = true;

    CdiAvmBaselineProfileVersion version;
    CdiReturnStatus rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, "01.00", &version);
    CHECK(kCdiStatusOk == rs);
    CHECK(1 == version.major);
    CHECK(0 == version.minor);

    // Not setting output parameter is OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, "01.00", NULL);
    CHECK(kCdiStatusOk == rs);

    // Not providing a version string is not OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmVideo, NULL, &version);
    CHECK(kCdiStatusInvalidParameter == rs);

    // Not providing a version string is not OK.
    rs = CdiAvmValidateBaselineVersionString(kCdiAvmAncillary, "7.98", &version);
    CHECK(kCdiStatusProfileNotSupported == rs);

    return pass;
}

/// Test for CdiAvmRegisterBaselineProfile.
static bool TestRegisterBaselineProfile()
{
    bool pass = true;

    CdiAvmVTableApi config = {
        .make_config_ptr = NULL,
        .parse_config_ptr = NULL,
        .get_unit_size_ptr = NULL,
        .key_get_array_ptr = NULL,
        .structure_size = sizeof(CdiAvmBaselineConfig)
    };

    CdiReturnStatus expected_status[] = { kCdiStatusOk, kCdiStatusDuplicateBaselineVersion };
    // Check that registering the same profile functions multiple times is prevented.
    for (int i = 0; i<5; ++i) {
        CdiReturnStatus rs = CdiAvmRegisterBaselineProfile(kCdiAvmVideo, "19.84", &config);
        CHECK(expected_status[i != 0] == rs);
    }

    return pass;
}

/// Test ParseAncillaryDataPayloadHeader.
static bool TestParseAncillaryDataPayloadHeader()
{
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |           ANC_Count           | F |         reserved          |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    const uint8_t raw_payload_headers[5][4] = {
        // All zero.
        { 0x00, 0x00, 0x00, 0x00 },
        // All bits set at even fields.
        { 0xff, 0xff, 0x3f, 0xff },
        // All bits set at odd fields.
        { 0x00, 0x00, 0xc0, 0x00 },
        // All one at even fields.
        { 0x00, 0x01, 0x00, 0x01 },
        // All one at odd fields.
        { 0x00, 0x00, 0x40, 0x00 }
    };

    bool pass = true;
    uint16_t ancillary_data_packet_count;
    CdiFieldKind field_kind;

    ParseAncillaryDataPayloadHeader((uint32_t *) raw_payload_headers[0], &ancillary_data_packet_count, &field_kind);
    CHECK(0 == ancillary_data_packet_count);
    CHECK(kCdiFieldKindUnspecified == field_kind);

    ParseAncillaryDataPayloadHeader((uint32_t *) raw_payload_headers[1], &ancillary_data_packet_count, &field_kind);
    CHECK(0xffff == ancillary_data_packet_count);
    CHECK(kCdiFieldKindUnspecified == field_kind);

    ParseAncillaryDataPayloadHeader((uint32_t *) raw_payload_headers[2], &ancillary_data_packet_count, &field_kind);
    CHECK(0 == ancillary_data_packet_count);
    CHECK(kCdiFieldKindInterlacedSecond == field_kind);

    ParseAncillaryDataPayloadHeader((uint32_t *) raw_payload_headers[3], &ancillary_data_packet_count, &field_kind);
    CHECK(1 == ancillary_data_packet_count);
    CHECK(kCdiFieldKindUnspecified == field_kind);

    ParseAncillaryDataPayloadHeader((uint32_t *) raw_payload_headers[4], &ancillary_data_packet_count, &field_kind);
    CHECK(0 == ancillary_data_packet_count);
    CHECK(kCdiFieldKindInvalid == field_kind);

    return pass;
}

/// Test ParseAncillaryDataPacketHeader.
static bool TestParseAncillaryDataPacketHeader()
{
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |C|   Line_Number       |   Horizontal_Offset   |S|  StreamNum  |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |         DID       |        SDID       |   Data_Count      | o-+--- UDW0 bits
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    const uint8_t raw_adp_headers[5][8] = {
        // All zero.
        { 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00 },
        // All bits set at even fields.
        { 0x80, 0x0f, 0xff, 0x7f,
          0x00, 0x3f, 0xf0, 0x03 },
        // All bits set at odd fields.
        { 0x7f, 0xf0, 0x00, 0x80,
          0xff, 0xc0, 0x0f, 0xfc },
        // All one at even fields.
        { 0x80, 0x00, 0x01, 0x01,
          0x00, 0x00, 0x10, 0x01 },
        // All one at odd fields.
        { 0x00, 0x10, 0x00, 0x80,
          0x00, 0x40, 0x00, 0x04 },
    };

    bool pass = true;

    struct AncillaryDataPacket packet;
    struct AncillaryDataPayloadErrors errors = { 0 };

    ParseAncillaryDataPacketHeader((uint32_t*) raw_adp_headers[0], &packet, &errors);
    CHECK(false == packet.is_color_difference_channel);
    CHECK(0 == packet.line_number);
    CHECK(0 == packet.horizontal_offset);
    CHECK(false == packet.is_valid_source_stream_number);
    CHECK(0 == packet.source_stream_number);
    CHECK(0 == packet.did);
    CHECK(0 == packet.sdid);
    CHECK(0 == packet.data_count);
    CHECK(0 == packet.user_data[0]);
    // Because our simple bit patterns don't have parity bits set correctly, parity errors are
    // expected in this test. See TestParityBits for a dedicated test looking at the parity bits.
    CHECK(3 == errors.parity_errors);

    ParseAncillaryDataPacketHeader((uint32_t*) raw_adp_headers[1], &packet, &errors);
    CHECK(true == packet.is_color_difference_channel);
    CHECK(0 == packet.line_number);
    CHECK(0xfff == packet.horizontal_offset);
    CHECK(false == packet.is_valid_source_stream_number);
    CHECK(0x7f == packet.source_stream_number);
    CHECK(0 == packet.did);
    CHECK(0xff == packet.sdid);
    CHECK(0 == packet.data_count);
    CHECK(0 != packet.user_data[0]);
    CHECK(2*3 == errors.parity_errors);

    ParseAncillaryDataPacketHeader((uint32_t*) raw_adp_headers[2], &packet, &errors);
    CHECK(false == packet.is_color_difference_channel);
    CHECK(0x7ff == packet.line_number);
    CHECK(0 == packet.horizontal_offset);
    CHECK(true == packet.is_valid_source_stream_number);
    CHECK(0 == packet.source_stream_number);
    CHECK(0xff == packet.did);
    CHECK(0 == packet.sdid);
    CHECK(0xff == packet.data_count); // it's an 8-bit value with parity bits!
    CHECK(0 == packet.user_data[0]);
    CHECK(3*3 == errors.parity_errors);

    ParseAncillaryDataPacketHeader((uint32_t*) raw_adp_headers[3], &packet, &errors);
    CHECK(true == packet.is_color_difference_channel);
    CHECK(0 == packet.line_number);
    CHECK(1 == packet.horizontal_offset);
    CHECK(false == packet.is_valid_source_stream_number);
    CHECK(1 == packet.source_stream_number);
    CHECK(0 == packet.did);
    CHECK(1 == packet.sdid);
    CHECK(0 == packet.data_count);
    CHECK(0 != packet.user_data[0]);
    CHECK(4*3 == errors.parity_errors);

    ParseAncillaryDataPacketHeader((uint32_t*) raw_adp_headers[4], &packet, &errors);
    CHECK(false == packet.is_color_difference_channel);
    CHECK(1 == packet.line_number);
    CHECK(0 == packet.horizontal_offset);
    CHECK(true == packet.is_valid_source_stream_number);
    CHECK(0 == packet.source_stream_number);
    CHECK(1 == packet.did);
    CHECK(0 == packet.sdid);
    CHECK(1 == packet.data_count);
    CHECK(0 == packet.user_data[0]);
    CHECK(5*3 == errors.parity_errors);

    return pass;
}

/// @brief Parity8 reference implementation.
static bool ReferenceParity8(uint8_t value)
{
    unsigned parity = 0;
    while (value) {
        parity ^= value & 1;
        value >>= 1;
    }
    return parity;
}

/// Test the parity-bit checking logic.
static bool TestParityBits(void)
{
    bool pass = true;

    static uint32_t raw_adp_header[2] = { 0x00000000, 0x00000ffc};
    struct AncillaryDataPacket packet;
    struct AncillaryDataPayloadErrors errors = { 0 };

    for (uint8_t i = 0; i<UINT8_MAX; ++i) {
        // Set data_count with its parity bits.
        uint32_t did = (i << 22) + (ReferenceParity8(i) << 30) + (!ReferenceParity8(i) << 31);
        uint32_t sdid = (i << 12) + (ReferenceParity8(i) << 20) + (!ReferenceParity8(i) << 21);
        uint32_t data_count = (i << 2) + (ReferenceParity8(i) << 10) + (!ReferenceParity8(i) << 11);
        raw_adp_header[1] = htonl(did | sdid | data_count);
        ParseAncillaryDataPacketHeader(raw_adp_header, &packet, &errors);
        CHECK(0 == packet.user_data[0]);
        CHECK_NO_PAYLOAD_ERRORS(errors);
    };

    return pass;
}


/// Test parsing full ancillary data packets.
static bool TestParseAncillaryDataPacket(void)
{
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |C|   Line_Number       |   Horizontal_Offset   |S|  StreamNum  |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |         DID       |        SDID       |   Data_Count      |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //                          User_Data_Words...
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //                                 |   Checksum_Word   |word_align |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    const uint8_t raw_packets[] = {
        // First packet (all bits or zero bits set).
        0x00, 0x00, 0x00, 0x00,
        0x80, 0x20, 0x04, 0x13,  // four UDWs
        0xff, 0x00, 0x3f, 0xf0,
        0x02, 0xaa, 0x00, 0x00,  // 0xaa where checksum expected
        // Second packet (ones and zeros).
        0x00, 0x00, 0x00, 0x00,
        0x80, 0x20, 0x08, 0x24,  // nine UDWs
        0x01, 0x00, 0x00, 0x10,
        0x00, 0x01, 0x00, 0x00,
        0x10, 0x00, 0x01, 0xaa,  // 0xaa where checksum expected
        0x80, 0x00, 0x00, 0x00,
        // Third packet (edge case: zero UDWs).
        0x00, 0x00, 0x00, 0x00,
        0x80, 0x20, 0x08, 0x02,  // zero UDWs
        0xaa, 0x00, 0x00, 0x00,  // 0xaa where checksum expected
    };

    bool pass = true;

    // Because our simple bit patterns don't have the checksum set as expected, checksum errors are
    // expected in this test. See TestPacketChecksum for a dedicated test looking at the checksum.
    int expected_checksum_errors = 0;

    struct AncillaryDataPacket packet;
    struct AncillaryDataPayloadErrors errors = { 0 };
    uint32_t* adp_net_ptr = (uint32_t*)raw_packets;
    int offset = 0;

    // First packet.
    offset = ParseAncillaryDataPacket(adp_net_ptr, &packet, &errors);
    CHECK(4 == offset);
    CHECK(4 == packet.data_count);
    CHECK(0x3ff == packet.user_data[0]);
    CHECK(0x000 == packet.user_data[1]);
    CHECK(0x3ff == packet.user_data[2]);
    CHECK(0x000 == packet.user_data[3]);
    CHECK_PAYLOAD_ERRORS(errors, ++expected_checksum_errors, 0);
    adp_net_ptr += 4;

    // Second packet.
    offset = ParseAncillaryDataPacket(adp_net_ptr, &packet, &errors);
    CHECK(6 == offset);
    CHECK(9 == packet.data_count);
    for (uint16_t i = 0; i < packet.data_count; ++i) {
        CHECK(((i + 1) & 1) == packet.user_data[i]);
    }
    CHECK_PAYLOAD_ERRORS(errors, ++expected_checksum_errors, 0);
    adp_net_ptr += 6;

    // Third packet.
    offset = ParseAncillaryDataPacket(adp_net_ptr, &packet, &errors);
    CHECK(3 == offset);
    CHECK(0 == packet.data_count);
    CHECK_PAYLOAD_ERRORS(errors, ++expected_checksum_errors, 0);

    return pass;
}

/// Test the checksum calculating logic.
static bool TestPacketChecksum(void)
{
    bool pass = true;

    uint8_t raw_packet_[] = {
        0x00, 0x00, 0x00, 0x00,
        0x42, 0x20, 0x64, 0x08, // two UDWs
        0x00, 0xff, 0xea, 0xa0,
    };
    uint32_t* raw_packet_ptr = (uint32_t*)raw_packet_;
    struct AncillaryDataPacket packet;
    struct AncillaryDataPayloadErrors payload_errors = { 0 };
    const uint32_t checksum_mask = 0x1ff;

    // Calculate the checksum of the header
    const uint32_t did_with_parity = 0x108;
    const uint32_t sdid_with_parity = 0x206;
    const uint32_t data_count_with_parity = 0x102;
    const uint32_t header_checksum = (did_with_parity + sdid_with_parity + data_count_with_parity);

    // udw1's b8 is zero, but with a few increments b8 will flip to one.
    uint32_t udw1 = 0x0f8;
    for (int i = 0; i < 20; ++i, ++udw1) {
        uint32_t checksum = (header_checksum + udw1) & checksum_mask;
        uint32_t b9 = (~checksum & 0x100) << 1; // b9 is NOT b8
        checksum = (checksum & checksum_mask) + b9;
        uint32_t udws_word = (udw1 << 14) + (checksum << 4);
        raw_packet_ptr[2] = htonl(udws_word);
        ParseAncillaryDataPacket(raw_packet_ptr, &packet, &payload_errors);
        CHECK(0x08 == packet.did);
        CHECK(0x06 == packet.sdid);
        CHECK(0x02 == packet.data_count);
        CHECK(0 == packet.user_data[0]);
        CHECK(udw1 == packet.user_data[1]);
        CHECK_NO_PAYLOAD_ERRORS(payload_errors); // This checks that the checksum was correct.
    }
    return pass;
}

/// Test function.
static bool TestWriteAncillaryDataPayloadHeader(void)
{
    bool pass = true;

    struct Case {
        uint16_t packet_count;
        CdiFieldKind field_kind;
    } cases[] = {
        { 12, kCdiFieldKindUnspecified },
        { 17, kCdiFieldKindInterlacedFirst },
        { 11, kCdiFieldKindInterlacedSecond }
    };

    for (int i = 0; i < CDI_ARRAY_ELEMENT_COUNT(cases); ++i) {
        uint32_t buf;
        WriteAncillaryDataPayloadHeader(&buf, cases[i].packet_count, cases[i].field_kind);
        struct Case c;
        ParseAncillaryDataPayloadHeader(&buf, &c.packet_count, &c.field_kind);
        CHECK(cases[i].packet_count == c.packet_count);
        CHECK(cases[i].field_kind == c.field_kind);
    }

    return pass;
}

/// Helper for some test functions.
static struct AncillaryDataPacket MakePacket(bool c, uint16_t ln, uint16_t ho, bool s, uint8_t ssrc, uint16_t did,
    uint16_t sdid, uint8_t dc)
{
    struct AncillaryDataPacket packet = { 0 };
    packet.is_color_difference_channel = c;
    packet.line_number = ln;
    packet.horizontal_offset = ho;
    packet.is_valid_source_stream_number = s;
    packet.source_stream_number = ssrc;
    packet.did = did;
    packet.sdid = sdid;
    packet.data_count = dc;
    memset(packet.user_data, 0, sizeof(packet.user_data));
    return packet;
}

/// Test WriteAncillaryDataPacketHeader.
static bool TestWriteAncillaryDataPacketHeader(void)
{
    bool pass = true;

    uint32_t buf[2];
    struct AncillaryDataPacket recv_packet = { 0 };
    struct AncillaryDataPacket send_packet = { 0 };
    struct AncillaryDataPayloadErrors payload_errors = { 0 };
    uint32_t checksum = 0;

    send_packet = MakePacket(true, 12, 1011, true, 67, 12, 113, 17);
    send_packet.user_data[0] = 0x100;

    WriteAncillaryDataPacketHeader(buf, &send_packet, &checksum);
    ParseAncillaryDataPacketHeader(buf, &recv_packet, &payload_errors);
    CHECK_EQUAL_OBJECTS(send_packet, recv_packet);
    CHECK_NO_PAYLOAD_ERRORS(payload_errors);
    CHECK(0 != checksum);

    checksum = 0;
    send_packet = MakePacket(true, 2, 47, false, 11, 99, 98, 97);
    send_packet.user_data[0] = 0x300;
    WriteAncillaryDataPacketHeader(buf, &send_packet, &checksum);
    ParseAncillaryDataPacketHeader(buf, &recv_packet, &payload_errors);
    CHECK_EQUAL_OBJECTS(send_packet, recv_packet);
    CHECK_NO_PAYLOAD_ERRORS(payload_errors);
    CHECK(0 != checksum);

    return pass;
}

/// Test WriteAncillaryDataPacket.
static bool TestWriteAncillaryDataPacket(void)
{
    bool pass = true;

    uint32_t buf[100];
    struct AncillaryDataPayloadErrors payload_errors = { 0 };

    // We intentionally wrap around in this loop
    for (uint8_t data_count = 100; data_count != 99; ++data_count) {
        struct AncillaryDataPacket send_packet = { 0 };
        struct AncillaryDataPacket recv_packet = { 0 };
        send_packet = MakePacket(true, 2, 47, false, 11, 99, 98, data_count);
        for (int i = 0; i < data_count; ++i) {
            send_packet.user_data[i] = 512 + i;
        }
        int offset = WriteAncillaryDataPacket(buf, &send_packet);
        int expected_offset = GetAncillaryDataPacketSize(data_count);
        CHECK(expected_offset == offset);
        offset = ParseAncillaryDataPacket(buf, &recv_packet, &payload_errors);
        CHECK(expected_offset == offset);
        for (uint8_t i = 0; i < data_count; ++i) {
            if (512U + i != recv_packet.user_data[i]) {
                fprintf(stderr, "received user_data[%d] = %u, expected %u\n", i, recv_packet.user_data[i], 512 + i);
            }
        }
        CHECK_EQUAL_OBJECTS(send_packet, recv_packet);
        CHECK_NO_PAYLOAD_ERRORS(payload_errors);
    }
    return pass;
}

/// Test CdiAvmGetAncillaryDataPayloadSize
static bool TestGetAncPayloadSize(void)
{
    bool pass = true;

    uint8_t data_counts[] = { 0, 25, 17, 112, 255, 1 };

    CHECK((1) * 4 == CdiAvmGetAncillaryDataPayloadSize(0, data_counts));
    CHECK((1 + 3) * 4 == CdiAvmGetAncillaryDataPayloadSize(1, data_counts));
    CHECK((1 + 3 + 11) * 4 == CdiAvmGetAncillaryDataPayloadSize(2, data_counts));
    CHECK((1 + 3 + 11 + 8) * 4 == CdiAvmGetAncillaryDataPayloadSize(3, data_counts));
    CHECK((1 + 3 + 11 + 8 + 38) * 4 == CdiAvmGetAncillaryDataPayloadSize(4, data_counts));
    CHECK((1 + 3 + 11 + 8 + 38 + 82) * 4 == CdiAvmGetAncillaryDataPayloadSize(5, data_counts));
    CHECK((1 + 3 + 11 + 8 + 38 + 82 + 3) * 4 == CdiAvmGetAncillaryDataPayloadSize(6, data_counts));

    return pass;
}

/// Helper for Packetize/Unpacketize tests.
const CdiAvmAncillaryDataPacket* GenerateAncDataPacket(int packet_num)
{
    static CdiAvmAncillaryDataPacket packet = {
        .packet_offset = 0,
        .packet_size = 0,
        .is_color_difference_channel = true,
        .line_number = 2,
        .horizontal_offset = 47,
        .is_valid_source_stream_number = true,
        .source_stream_number = 11,
        .did = 99,
        .sdid = 98,
        .data_count = 0,
        .user_data = { 0 }
    };

    // Initialize user_data once.
    if (0 == packet.user_data[0]) {
        for (int i = 0; i < 255; ++i) {
            packet.user_data[i] = (31 + i) % 256;
        }
    }
    assert(31 == packet.user_data[0]);

    // Compute packet offset.
    uint8_t data_counts[] = { 6, 255, 15, 0 };
    int offset = 1; // one word for payload header
    for (int i = 0; i < packet_num; i++) {
        offset += GetAncillaryDataPacketSize(data_counts[i % 4]);
    }

    // Update packet contents.
    packet.data_count = data_counts[packet_num % 4];
    packet.packet_offset = 4 * offset; // size in words to size in bytes
    packet.packet_size = 4 * GetAncillaryDataPacketSize(packet.data_count);
    switch (packet_num % 4) {
        case 0:
            packet.user_data[5] = (100 + packet_num) & 0xff;
            break;
        case 1:
            packet.user_data[  6] = (100 + packet_num) & 0xff;
            packet.user_data[254] = 112;
            break;
        case 2:
            packet.user_data[14] = (100 + packet_num) & 0xff;
            break;
        case 3:
            break;
        default:
            assert(0);
    }

    return &packet;
}

/// Helper for Packetize and Unpacketize tests.
static bool CheckEqualAncPackets(const CdiAvmAncillaryDataPacket* p1, const CdiAvmAncillaryDataPacket* p2)
{
    bool pass = true;
    CHECK(p1->is_color_difference_channel == p2->is_color_difference_channel);
    CHECK(p1->line_number == p2->line_number);
    CHECK(p1->horizontal_offset == p2->horizontal_offset);
    CHECK(p1->is_valid_source_stream_number == p2->is_valid_source_stream_number);
    CHECK(p1->source_stream_number == p2->source_stream_number);
    CHECK(p1->did == p2->did);
    CHECK(p1->sdid == p2->sdid);
    CHECK(p1->data_count == p2->data_count);
    if (pass) {
        for (int i = 0; i < p1->data_count; ++i) {
            CHECK(p1->user_data[i] == p2->user_data[i]);
        }
    }
    return pass;
}

/// Callback used by TestPacketizeAnc. Generates four different ANC packets.
static const CdiAvmAncillaryDataPacket* PacketizeAncCb(void* user_data_ptr)
{
    struct GenPacketControl* ctrl = (struct GenPacketControl*)user_data_ptr;
    if (ctrl->next_anc_packet < ctrl->max_anc_packet) {
        return GenerateAncDataPacket(ctrl->next_anc_packet++);
    } else {
        return NULL;
    }
}

/// Callback used by TestUnpacketizeAncillaryData. Compares decoded with expected packets.
static void UnpacketizeAncCb(void* user_data_ptr, CdiFieldKind field_kind, const CdiAvmAncillaryDataPacket* packet_ptr,
    bool has_parity_error, bool has_checksum_error)
{
    struct GenPacketControl* ctrl = (struct GenPacketControl*)user_data_ptr;
    if (NULL != packet_ptr) {
        const CdiAvmAncillaryDataPacket* expected_packet_ptr = GenerateAncDataPacket(ctrl->next_anc_packet++);
        bool equal = CheckEqualAncPackets(expected_packet_ptr, packet_ptr);
        ctrl->field_kind = field_kind;
        ctrl->num_mismatched_packets += equal ? 0 : 1;
        ctrl->num_parity_errors += has_parity_error ? 1 : 0;
        ctrl->num_checksum_errors += has_checksum_error ? 1 : 0;
        ctrl->last_packet_offset = packet_ptr->packet_offset;
        ctrl->last_packet_size = packet_ptr->packet_size;
    } else {
        ctrl->max_anc_packet = ctrl->next_anc_packet;
    }
}

/// Callback used by TestAncillaryDataPayloadChunks. Copies data to a buffer.
static void UnpacketizeAncCb2(void* user_data_ptr, CdiFieldKind field_kind, const CdiAvmAncillaryDataPacket* packet_ptr,
    bool has_parity_error, bool has_checksum_error)
{
    struct GenPacketControl2* ctrl_ptr = (struct GenPacketControl2*)user_data_ptr;
    int rc = 0;
    if (NULL != packet_ptr) {
        char* dest_ptr = ctrl_ptr->buffer_ptr + packet_ptr->packet_offset;
        rc = CdiCoreGather(ctrl_ptr->sgl_ptr, packet_ptr->packet_offset, dest_ptr, packet_ptr->packet_size);
    } else {
        // Copy payload header (first four bytes).
        rc = CdiCoreGather(ctrl_ptr->sgl_ptr, 0, ctrl_ptr->buffer_ptr, 4);
    }
    if (-1 == rc) {
        ctrl_ptr->error_occurred = true;
    }
    (void)field_kind;
    (void)has_parity_error;
    (void)has_checksum_error;
}

/// Helper for serialize/deserialize test.
static int ComputeRequiredBufferSize(const struct GenPacketControl* control)
{
    struct GenPacketControl control_copy = *control;
    uint16_t num_anc_packets = 0;
    uint8_t data_counts[1024];
    const CdiAvmAncillaryDataPacket* packet_ptr;
    while (NULL != (packet_ptr = PacketizeAncCb(&control_copy))) {
        data_counts[num_anc_packets++] = packet_ptr->data_count;
    }

    return CdiAvmGetAncillaryDataPayloadSize(num_anc_packets, data_counts);
}

/// Test the copy functions between public and internal AncillaryDataPacket structs.
static bool TestAncillaryInternalToExternalCopying(void)
{
    extern int CdiAvmCopyAncillaryDataPacket(CdiAvmAncillaryDataPacket* dest_packet_ptr,
    const CdiAvmAncillaryDataPacket* source_packet_ptr);

    bool pass = true;

    // Initialize packet with arbitrary values.
    CdiAvmAncillaryDataPacket packet = {
        .is_color_difference_channel = true,
        .line_number = 2,
        .horizontal_offset = 47,
        .is_valid_source_stream_number = false,
        .source_stream_number = 11,
        .did = 99,
        .sdid = 98,
        .data_count = UINT8_MAX,
        .user_data = { 0 }
    };
    packet.user_data[  0] = 255;
    packet.user_data[100] = 231;
    packet.user_data[117] = 117;
    packet.user_data[200] = 19;
    packet.user_data[254] = 255; // largest possible index is 254, assign largest possible 8-bit value

    CdiAvmAncillaryDataPacket packet_copy;
    int parity_errors = CdiAvmCopyAncillaryDataPacket(&packet_copy, &packet);
    CHECK(CheckEqualAncPackets(&packet, &packet_copy));
    CHECK(0 == parity_errors);

    return pass;
}

/// Test CdiAvmPacketizeAncillaryData.
static bool TestPacketizeAncillaryData(void)
{
    bool pass = true;

    struct GenPacketControl control = { kCdiFieldKindUnspecified, 0, 15, 0, 0, 0, 0, 0 };

    // Set up a buffer.
    char buffer[1024];
    int size_in_bytes = 0;
    CdiFieldKind field_kind = kCdiFieldKindInterlacedFirst;

    // Check use with invalid arguments.
    control = (struct GenPacketControl){ kCdiFieldKindUnspecified, 0, 1, 0, 0, 0, 0, 0 };
    size_in_bytes = -14;
    CdiReturnStatus status = CdiAvmPacketizeAncillaryData(PacketizeAncCb, field_kind, &control, buffer, &size_in_bytes);
    CHECK(kCdiStatusInvalidParameter == status);

    // Check for status code indicating not enough memory.
    control = (struct GenPacketControl){ kCdiFieldKindUnspecified, 0, 15, 0, 0, 0, 0, 0 };
    size_in_bytes = sizeof(buffer);
    assert(CDI_ARRAY_ELEMENT_COUNT(buffer) < ComputeRequiredBufferSize(&control));
    status = CdiAvmPacketizeAncillaryData(PacketizeAncCb, field_kind, &control, buffer, &size_in_bytes);
    CHECK(kCdiStatusBufferOverflow == status);
    CHECK(0 == size_in_bytes);

    // Check successful run.
    // Check for status code indicating not enough memory.
    control = (struct GenPacketControl){ kCdiFieldKindUnspecified, 0, 7, 0, 0, 0, 0, 0 };
    size_in_bytes = sizeof(buffer);
    int expected_payload_size = ComputeRequiredBufferSize(&control);
    assert(CDI_ARRAY_ELEMENT_COUNT(buffer) > expected_payload_size);
    status = CdiAvmPacketizeAncillaryData(PacketizeAncCb, field_kind, &control, buffer, &size_in_bytes);
    CHECK(kCdiStatusOk == status);
    CHECK(expected_payload_size == size_in_bytes);

    return pass;
}

/// Test CdiAvmUnpacketizeAncillaryData.
static bool TestUnpacketizeAncillaryData(void)
{
    bool pass = true;
    CdiReturnStatus rs = kCdiStatusOk;
    struct GenPacketControl control = { 0 };

    control.field_kind = kCdiFieldKindUnspecified;
    rs = CdiAvmUnpacketizeAncillaryData(MakeAncillaryDataPayload(1), UnpacketizeAncCb, &control);
    CHECK(kCdiStatusInvalidParameter == rs);
    CHECK(kCdiFieldKindUnspecified == control.field_kind);

    rs = CdiAvmUnpacketizeAncillaryData(MakeAncillaryDataPayload(2), UnpacketizeAncCb, &control);
    CHECK(kCdiStatusInvalidParameter == rs);
    CHECK(kCdiFieldKindUnspecified == control.field_kind);

    rs = CdiAvmUnpacketizeAncillaryData(MakeAncillaryDataPayload(3), UnpacketizeAncCb, &control);
    CHECK(kCdiStatusInvalidPayload == rs);
    CHECK(kCdiFieldKindUnspecified == control.field_kind);

    rs = CdiAvmUnpacketizeAncillaryData(MakeAncillaryDataPayload(4), UnpacketizeAncCb, &control);
    CHECK(kCdiStatusOk == rs);
    CHECK(kCdiFieldKindInterlacedFirst == control.field_kind);
    CHECK(7 == control.max_anc_packet);
    CHECK(0 == control.num_mismatched_packets);
    CHECK(0 == control.num_parity_errors);
    CHECK(0 == control.num_checksum_errors);
    CHECK(sizeof(anc_payload) == control.last_packet_offset + control.last_packet_size);

    return pass;
}

/// Test packet_offset and packet_size.
static bool TestAncillaryDataPayloadChunks(void)
{
    bool pass = true;

    // We use packet_offset and packet_size to copy chunks of the payload into a new buffer.
    // Then we check that the contents match the original payload buffer.
    char buffer[CDI_ARRAY_ELEMENT_COUNT(anc_payload)] = { 0 };
    const CdiSgList* sgl_ptr = MakeAncillaryDataPayload(4);
    struct GenPacketControl2 control = {
        .buffer_ptr = buffer,
        .sgl_ptr = sgl_ptr,
        .error_occurred = false
     };

    CdiReturnStatus rs = CdiAvmUnpacketizeAncillaryData(sgl_ptr, UnpacketizeAncCb2, &control);
    CHECK(kCdiStatusOk == rs);
    CHECK(false == control.error_occurred);
    CHECK(0 == memcmp(anc_payload, buffer, CDI_ARRAY_ELEMENT_COUNT(anc_payload)));

    return pass;
}

/// Helper macro.
#define RUN_TEST(test_func)                                            \
    do { if (!test_func()) {                                           \
        CDI_LOG_THREAD(kLogError, "AVM test "#test_func" failed.");    \
        rs = kCdiStatusFatal;                                          \
    } } while (false)

CdiReturnStatus TestUnitAvmApi(void)
{
    // Run the actual tests.
    CdiReturnStatus rs = kCdiStatusOk;
    RUN_TEST(TestGetBaselineUnitSize);
    RUN_TEST(TestValidateBaselineVersion);
    RUN_TEST(TestRegisterBaselineProfile);
    RUN_TEST(TestParseAncillaryDataPayloadHeader);
    RUN_TEST(TestParseAncillaryDataPacketHeader);
    RUN_TEST(TestParseAncillaryDataPacket);
    RUN_TEST(TestParityBits);
    RUN_TEST(TestPacketChecksum);
    RUN_TEST(TestWriteAncillaryDataPayloadHeader);
    RUN_TEST(TestWriteAncillaryDataPacketHeader);
    RUN_TEST(TestWriteAncillaryDataPacket);
    RUN_TEST(TestAncillaryInternalToExternalCopying);
    RUN_TEST(TestGetAncPayloadSize);
    RUN_TEST(TestPacketizeAncillaryData);
    RUN_TEST(TestUnpacketizeAncillaryData);
    RUN_TEST(TestAncillaryDataPayloadChunks);
    return rs;
}
