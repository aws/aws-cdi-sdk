// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#include "riff.h"
#include "cdi_avm_payloads_api.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>

#include "test_control.h"
#include "test_common.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// True if strings match, where rhs is a string literal or static string.
#define STRINGS_MATCH(lhs, rhs) (0 == CdiOsStrNCmp((lhs), (rhs), strlen(rhs)))

/// Define TestConsoleLog.
#define TestConsoleLog SimpleConsoleLog

/// Helper macro for calling CdiAvmUnpacketizeAncillaryData.
#define MAKE_SGL(sgl, buffer_ptr, buffer_size)  \
    CdiSglEntry _entry = {                      \
        .address_ptr = (void*)buffer_ptr,       \
        .size_in_bytes = buffer_size,           \
        .internal_data_ptr = NULL,              \
        .next_ptr = NULL                        \
    };                                          \
    const CdiSgList sgl = {                     \
        .total_data_size = buffer_size,         \
        .sgl_head_ptr = &_entry,                \
        .sgl_tail_ptr = &_entry,                \
        .internal_data_ptr = NULL               \
    };

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Check that file is a RIFF file.
 *
 * @param read_file_handle Handle to file.
 * @param file_path_str File path.
 * @param file_header_ptr Pointer to file header.
 *
 * @return True if and only if file is recognized as RIFF file.
 */
static bool IsRiffFile(CdiFileID read_file_handle, const char* file_path_str, RiffFileHeader* file_header_ptr)
{
    uint32_t bytes_read = 0;
    bool return_val = CdiOsRead(read_file_handle, file_header_ptr, sizeof(RiffFileHeader), &bytes_read);

    if (!return_val || (sizeof(RiffFileHeader) != bytes_read)) {
        CDI_LOG_THREAD(kLogError, "Failed to read RIFF file header from file [%s].", file_path_str);
        return_val = false;
    }

    // Parse the header file.
    if (return_val) {
        // Check for "RIFF" four_cc marker.
        if (!STRINGS_MATCH(file_header_ptr->chunk_header.four_cc, "RIFF")) {
            CDI_LOG_THREAD(kLogError, "[%s] is not a RIFF file (four_cc code received is not 'RIFF').", file_path_str);
            return_val = false;
        }
    }
    return return_val;
}

/**
 * Return FourCC as a C string.
 *
 * @param four_cc Array holding a "four-character code".
 *
 * @return NUL-terminated static string of length four.
 */
static const char* FourCC(const char four_cc[4])
{
    static char four_cc_string[5] = { 0 };
    four_cc_string[0] = four_cc[0];
    four_cc_string[1] = four_cc[1];
    four_cc_string[2] = four_cc[2];
    four_cc_string[3] = four_cc[3];
    return four_cc_string;
}

/**
 * Return whitespaces.
 *
 * @param indentation Number of space characters.
 *
 * @return NUL-terminated static string of length indentation.
 */
static const char* Space(int indentation)
{
    static char space_string[128];
    assert(indentation < ARRAY_ELEMENT_COUNT(space_string));

    for (int i = 0; i < indentation; ++i) {
        space_string[i] = ' ';
    }
    space_string[indentation] = 0;

    return space_string;
}

/**
 * Write printable characters to buffer.
 *
 * @param indentation Number of whitespace characters to prefix each line with.
 * @param chunk_header A RIFF chunk header.
 * @param data_ptr Pointer to buffer with RIFF chunk data.
 * @param max_line_length Maximum number of characters to output per line.
 * @param print_buffer_ptr Pointer to print buffer.
 */
static void StringDumpChunk(int indentation, RiffChunkHeader chunk_header, const char* data_ptr,
    int max_line_length, char* print_buffer_ptr)
{
    int i = 0;
    for (; i < indentation; ++i) {
        print_buffer_ptr[i] = ' ';
    }

    i += snprintf(print_buffer_ptr + i, max_line_length - i, "%s (%4"PRIu32"): ", FourCC(chunk_header.four_cc),
        chunk_header.size);

    uint32_t j = 0;
    while (i < max_line_length && j < chunk_header.size) {
        print_buffer_ptr[i++] = isprint((unsigned char)data_ptr[j]) ? data_ptr[j] : '.';
        j++;
    }

    // Indicate whether there is more data than we can print on one line.
    if (i < max_line_length) {
        print_buffer_ptr[i] = '<';
    } else {
        assert(i == max_line_length);
        print_buffer_ptr[i] = '>';
    }
    print_buffer_ptr[max_line_length + 1] = 0;
}

/// Control structure for Anc unpacketize callback.
struct UnpacketizeAncControl
{
    char* print_buffer_ptr; ///< Pointer to print buffer.
    int max_line_length; ///< Maximum number of characters to print.
};

/**
 * Callback used by UnpacketizeAncPayload. Writes user data into results buffer.
 *
 * @see CdiAvmUnpacketizeAncCallback.
 */
static void ShowAncCallback(void* context_ptr, CdiFieldKind field_kind,
    const CdiAvmAncillaryDataPacket* packet_ptr, bool has_data_count_parity_error, bool has_checksum_error)
{
    struct UnpacketizeAncControl* ctrl_ptr = (struct UnpacketizeAncControl*)context_ptr;
    if (NULL != packet_ptr && 0 < ctrl_ptr->max_line_length) {
        int i = snprintf(ctrl_ptr->print_buffer_ptr, ctrl_ptr->max_line_length, "DID/SDID/UDWs: 0x%x/0x%x/%u, ",
            packet_ptr->did, packet_ptr->sdid, packet_ptr->data_count);
        ctrl_ptr->max_line_length -= i;
        ctrl_ptr->print_buffer_ptr += i;
    }
    if (NULL == packet_ptr && has_data_count_parity_error) {
        int i = snprintf(ctrl_ptr->print_buffer_ptr, ctrl_ptr->max_line_length, "!PARITY ERROR ");
        ctrl_ptr->max_line_length -= i;
        ctrl_ptr->print_buffer_ptr += i;
    }
    if (NULL == packet_ptr && has_checksum_error) {
        int i = snprintf(ctrl_ptr->print_buffer_ptr, ctrl_ptr->max_line_length, "!CHECKSUM ERROR");
        ctrl_ptr->max_line_length -= i;
        ctrl_ptr->print_buffer_ptr += i;
    }
    (void)field_kind; // unused
    (void)has_data_count_parity_error; // unused
    (void)has_checksum_error; // unused
}

/**
 * Helper for extracting CEA-608 encoded closed captions: translate character code.
 *
 * @param cc Character code to translate.
 *
 * @return ASCII character code.
 */
static char Translate608(uint8_t cc)
{
    const char table[] = {
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '!', '"', '#', '$', '%', '&', '\'',
        '(', ')', 'a', '+', ',', '-', '.', '/', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';',
        '<', '=', '>', '?', '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
        'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', 'e', ']', 'i', 'o', 'u', 'a', 'b', 'c',
        'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w',
        'x', 'y', 'z', 'c', '%', 'N', 'n', '+' };
    assert(cc < ARRAY_ELEMENT_COUNT(table));

    // cc is assumed a standard characters per ANSI/CTA-608-E S-2019, Table 50.
    return table[cc];
}

/**
 * Callback used by UnpacketizeAncPayload. Writes user data into results buffer.
 *
 * @see CdiAvmUnpacketizeAncCallback.
 */
static void ShowCCsCallback(void* context_ptr, CdiFieldKind field_kind,
    const CdiAvmAncillaryDataPacket* packet_ptr, bool has_data_count_parity_error, bool has_checksum_error)
{
    struct UnpacketizeAncControl* ctrl_ptr = (struct UnpacketizeAncControl*)context_ptr;
    if (NULL != packet_ptr && 0 < ctrl_ptr->max_line_length) {
        if (0x61 == packet_ptr->did) {
            if (0x02 == packet_ptr->sdid) {
                // CEA-608 data (see SMPTE ST 334-1:2015, Table 1).
                assert(3 ==  packet_ptr->data_count);
                bool is_field1 = packet_ptr->user_data[0] & 0x80;
                uint8_t cc1 = packet_ptr->user_data[1] & 0x7f;
                uint8_t cc2 = packet_ptr->user_data[2] & 0x7f;
                if (is_field1 && 0x19 < cc1 && cc1 < 0x80) {
                    // Standard characters.
                    *ctrl_ptr->print_buffer_ptr++ = Translate608(cc1);
                    *ctrl_ptr->print_buffer_ptr++ = Translate608(cc2);
                    ctrl_ptr->max_line_length -= 2;
                }
            }
            if (0x01 == packet_ptr->sdid) {
                // TODO: CEA-708 Closed captioning
                assert(0); // Need implementation here.
            }
        }
    }
    (void)field_kind; // unused
    (void)has_data_count_parity_error; // unused
    (void)has_checksum_error; // unused
}

/**
 * Write printable characters to buffer.
 *
 * @param indentation Number of whitespace characters to prefix each line with.
 * @param chunk_header A RIFF chunk header.
 * @param data_ptr Pointer to buffer with RIFF chunk data.
 * @param max_line_length Maximum number of characters to output per line.
 * @param print_buffer_ptr Pointer to print buffer.
 * @param mode Dump mode.
 *
 * @return Success or failure.
 */
static bool ShowAncPayload(int indentation, RiffChunkHeader chunk_header, const char* data_ptr,
    int max_line_length, char* print_buffer_ptr, int mode)
{
    // Indent unless it's CC dump mode.
    CdiAvmUnpacketizeAncCallback* callback = ShowAncCallback;
    int i = 0;
    if (kRiffDumpClosedCaptions == mode) {
        callback = ShowCCsCallback;
        i = strlen(print_buffer_ptr);
    } else {
        for (; i < indentation; ++i) {
            print_buffer_ptr[i] = ' ';
        }
    }
    struct UnpacketizeAncControl ctrl = {
        .print_buffer_ptr = print_buffer_ptr + i,
        .max_line_length = max_line_length - i
    };

    MAKE_SGL(sgl, data_ptr, chunk_header.size);
    CdiReturnStatus rs = CdiAvmUnpacketizeAncillaryData(&sgl, callback, &ctrl);
    if (kCdiStatusOk != rs) {
        CDI_LOG_THREAD(kLogError, "Error processing ANC payload [%s].", CdiCoreStatusToString(rs));
        // Fall back on StringDumpChunk.
        StringDumpChunk(indentation, chunk_header, data_ptr, max_line_length, print_buffer_ptr);
    }

    return kCdiStatusOk == rs;
}

/**
 * Check that chunk data is ancillary data.
 *
 * @param data_ptr Pointer to chunk data.
 * @param data_size Size in bytes.
 *
 * @return True if and only if data is decodable as ancillary data.
 */
static bool CheckAncPayload(const char* data_ptr, uint32_t data_size)
{
    // We don't want to print anything here, hence max_line_length = 0.
    char print_buffer[1];
    struct UnpacketizeAncControl ctrl = {
        .print_buffer_ptr = print_buffer,
        .max_line_length = 0
    };

    MAKE_SGL(sgl, data_ptr, data_size);
    CdiReturnStatus rs = CdiAvmUnpacketizeAncillaryData(&sgl, ShowAncCallback, &ctrl);
    return kCdiStatusOk == rs;
}

/**
 * Show RIFF data by sub chunk.
 *
 * @param file_handle Handle to RIFF file.
 * @param size Size in bytes of list of sub chunks.
 * @param indentation Number of spaces to indent output by.
 * @param max_line_length Number of spaces per line to output.
 * @param mode Dump mode selecting the kind of data to show.
 *
 * @return Success or failure.
 */
static bool ShowRiffList(CdiFileID file_handle, uint32_t size, int indentation, int max_line_length, int mode)
{
    char print_buffer[256] = { 0 };

    bool success = true;
    uint32_t list_bytes_read = 0;
    while (success && list_bytes_read < size) {
        uint32_t bytes_read = 0;
        RiffChunkHeader chunk_header;
        success = CdiOsRead(file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
        if (!success || (sizeof(RiffChunkHeader) != bytes_read)) {
            TestConsoleLog(kLogError, "Failed to read chunk header.");
            TestConsoleLog(kLogError, "list_bytes_read = [%u], size = [%u]", list_bytes_read, size);
            break;
        }
        list_bytes_read += bytes_read;

        // Show this chunk.
        if (STRINGS_MATCH(chunk_header.four_cc, "LIST")) {
            char four_cc[4];
            success = CdiOsRead(file_handle, four_cc, 4, &bytes_read);
            if (!success || (4 != bytes_read)) {
                TestConsoleLog(kLogError, "Failed to read form type.");
                break;
            }
            list_bytes_read += bytes_read;
            TestConsoleLog(kLogInfo, "%s%s (%"PRIu32" bytes):", Space(indentation), FourCC(four_cc), chunk_header.size);
            ShowRiffList(file_handle, chunk_header.size - 4, indentation + 2, max_line_length, mode);
        } else {
            char* p = CdiOsMemAlloc(chunk_header.size);
            assert(NULL != p);
            success = CdiOsRead(file_handle, p, chunk_header.size, &bytes_read);
            if (!success || (chunk_header.size != bytes_read)) {
                CdiOsMemFree(p);
                TestConsoleLog(kLogError, "Failed to read data for [%s] chunk (%"PRIu32" vs %"PRIu32" expected).",
                    FourCC(chunk_header.four_cc), bytes_read, chunk_header.size);
                break;
            }
            assert(max_line_length < ARRAY_ELEMENT_COUNT(print_buffer));
            switch (mode) {
                case kRiffDumpRaw:
                    StringDumpChunk(indentation, chunk_header, p, max_line_length, print_buffer);
                break;

                case kRiffDumpDid:
                case kRiffDumpClosedCaptions:
                    if (STRINGS_MATCH(chunk_header.four_cc, "ANC ")) {
                        if (0 != bytes_read % 4) {
                            TestConsoleLog(kLogWarning, "Invalid ANC chunk size [%u].", bytes_read);
                        }
                    }
                    success = ShowAncPayload(indentation, chunk_header, p, max_line_length, print_buffer, mode);
                break;

                default:
                assert(0);
            }
            CdiOsMemFree(p);

            // When extracting closed captions, don't print every chunk.
            // When printing DID/SDID, don't print empty lines for empty ANC packets.
            bool print_now = (kRiffDumpClosedCaptions == mode && (int)strlen(print_buffer) >= max_line_length)
                             || (kRiffDumpDid == mode && (int)strlen(print_buffer) > indentation)
                             || (kRiffDumpRaw == mode);
            if (print_now) {
                TestConsoleLog(kLogInfo, "%s", print_buffer);
                memset(print_buffer, 0, ARRAY_ELEMENT_COUNT(print_buffer));
            }
        }
        list_bytes_read += chunk_header.size;
    }
    if (success && strlen(print_buffer) > 0) {
        TestConsoleLog(kLogInfo, "%s", print_buffer);
    }
    return success;
}

/**
 * Check that a RIFF file contains ancillary data.
 *
 * @param file_handle Handle to RIFF file.
 * @param size Size in bytes of list of sub chunks.
 * @param verbose When true, log error messages explaining what's wrong with the file.
 *
 * @return True if and only if file contains ancillary data.
 */
static bool CheckFileContainsAncData(CdiFileID file_handle, uint32_t size, bool verbose)
{
    uint32_t list_bytes_read = 0;
    bool success = true;
    while (success && list_bytes_read < size) {
        uint32_t bytes_read = 0;
        RiffChunkHeader chunk_header;
        success = CdiOsRead(file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
        if (!success || (sizeof(RiffChunkHeader) != bytes_read)) {
            if (verbose) {
                TestConsoleLog(kLogError, "Failed to read chunk header.");
                TestConsoleLog(kLogError, "list_bytes_read = [%u], size = [%u]", list_bytes_read, size);
            }
            continue;
        }
        list_bytes_read += bytes_read;

        char* p = CdiOsMemAlloc(chunk_header.size);
        assert(NULL != p);
        success = CdiOsRead(file_handle, p, chunk_header.size, &bytes_read);
        if (!success || (chunk_header.size != bytes_read)) {
            if (verbose) {
                TestConsoleLog(kLogError, "Failed to read data for [%s] chunk (%"PRIu32" vs %"PRIu32" expected).",
                    FourCC(chunk_header.four_cc), bytes_read, chunk_header.size);
            }
            success = false;
        }

        if (success && STRINGS_MATCH(chunk_header.four_cc, "ANC ")) {
            if (0 != bytes_read % 4) {
                if (verbose) {
                    TestConsoleLog(kLogError, "Expected multiple of four as ANC chunk size, got [%u].", bytes_read);
                }
                success = false;
            } else {
                success = CheckAncPayload(p, chunk_header.size);
            }
        } else {
            if (verbose) {
                TestConsoleLog(kLogWarning, "Expected ANC chunk, got [%s].", FourCC(chunk_header.four_cc));
            }
            success = false;
        }
        CdiOsMemFree(p);
        list_bytes_read += chunk_header.size;
    }
    return success;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool StartRiffPayloadFile(const StreamSettings* stream_settings_ptr, CdiFileID read_file_handle)
{
    RiffFileHeader file_header;
    bool return_val = IsRiffFile(read_file_handle, stream_settings_ptr->file_read_str, &file_header);

    if (return_val) {
        // Check for "CDI " Form Type.
        if (!STRINGS_MATCH(file_header.form_type, "CDI ")) {
            CDI_LOG_THREAD(kLogError, "RIFF file [%s]: Form Type received is not 'CDI '.",
                           stream_settings_ptr->file_read_str, file_header.form_type);
            return_val = false;
        }
    }

    return return_val;
}

bool GetNextRiffChunkSize(const StreamSettings* stream_settings_ptr,
                          CdiFileID read_file_handle, int* ret_chunk_size_ptr)
{
    bool return_val = true;

    RiffChunkHeader chunk_header; // Buffer for holding chunk headers four_cc code and chunk size.

    uint32_t bytes_read = 0;
    if (read_file_handle) {
        return_val = CdiOsRead(read_file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
    } else {
        CDI_LOG_THREAD(kLogError, "No file handle for RIFF File");
    }

    // Ran out of subchunk headers to read so retry at the top of the file.
    if (return_val && (0 == bytes_read)) {
        if (CdiOsFSeek(read_file_handle, 0, SEEK_SET)) {
            return_val = StartRiffPayloadFile(stream_settings_ptr, read_file_handle);
        }
        if (return_val) {
            return_val = CdiOsRead(read_file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
        }
    }

    if (!return_val || (sizeof(RiffChunkHeader) != bytes_read)) {
        CDI_LOG_THREAD(kLogError, "Failed to read chunk header from file [%s]. Read [%d] header bytes.",
        stream_settings_ptr->file_read_str, bytes_read);
        return_val = false;
    }

    // For now check if the chunk ID is "ANC ". NOTE: this check may be removed or expanded in the future to support
    // additional chunk IDs.
    if (!STRINGS_MATCH(chunk_header.four_cc, "ANC ")) {
        CDI_LOG_THREAD(kLogError, "RIFF File [%s] subchunk ID is not 'ANC '.", stream_settings_ptr->file_read_str);
        return_val = false;
    }

    if (return_val) {
        *ret_chunk_size_ptr = chunk_header.size;
        // Payload size must be larger than the RIFF chunk size in the source file.
        if (*ret_chunk_size_ptr > stream_settings_ptr->payload_size) {
            CDI_LOG_THREAD(kLogError, "Payload size from RIFF file [%d] is larger than the payload buffer [%d].",
                           *ret_chunk_size_ptr, stream_settings_ptr->payload_size);
            return_val = false;
        }
    }

    return return_val;
}


bool ReportRiffFileContents(const char* file_path_str, int max_line_length, int mode)
{
    if (kRiffDumpNone == mode) {
        return false;
    }
    CdiFileID file_handle;
    if (!CdiOsOpenForRead(file_path_str, &file_handle)) {
        return false;
    }

    RiffFileHeader file_header;
    bool success = IsRiffFile(file_handle, file_path_str, &file_header);

    if (success) {
        // Print the contents.
        if (kRiffDumpClosedCaptions != mode) {
            TestConsoleLog(kLogInfo, "");
            TestConsoleLog(kLogInfo, "%4s (%"PRIu32" bytes):", FourCC(file_header.form_type), file_header.chunk_header.size);
        }
        success = ShowRiffList(file_handle, file_header.chunk_header.size - 4, 2, max_line_length, mode);
    }
    CdiOsClose(file_handle);

    return success;
}

bool RiffFileContainsAncillaryData(const char* file_path_str)
{
    CdiFileID file_handle;
    if (!CdiOsOpenForRead(file_path_str, &file_handle)) {
        return false;
    }

    RiffFileHeader file_header;
    if (!IsRiffFile(file_handle, file_path_str, &file_header)) {
        return false;
    }

    if (!STRINGS_MATCH(file_header.form_type, "CDI ")) {
        return false;
    }

    return CheckFileContainsAncData(file_handle, file_header.chunk_header.size - 4, true);
}
