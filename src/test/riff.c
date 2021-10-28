
// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#include "riff.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>

#include "test_control.h"
#include "cdi_test.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// True if strings match, where rhs is a string literal or static string.
#define STRINGS_MATCH(lhs, rhs) (0 == CdiOsStrNCmp((lhs), (rhs), strlen(rhs)))

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
        print_buffer_ptr[i++] = isprint(data_ptr[j]) ? data_ptr[j] : '.';
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

/**
 * Show RIFF data by sub chunk.
 *
 * @param file_handle Handle to RIFF file.
 * @param size Size in bytes of list of sub chunks.
 * @param indentation Number of spaces to indent output by.
 * @param max_line_length Number of spaces per line to output.
 */
static void ShowRiffList(CdiFileID file_handle, uint32_t size, int indentation, int max_line_length)
{
    uint32_t list_bytes_read = 0;
    while (list_bytes_read < size) {
        uint32_t bytes_read = 0;
        RiffChunkHeader chunk_header;
        bool success = CdiOsRead(file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
        if (!success || (sizeof(RiffChunkHeader) != bytes_read)) {
            TestConsoleLog(kLogError, "Failed to read chunk header.");
            TestConsoleLog(kLogError, "list_bytes_read = [%u], size = [%u]", list_bytes_read, size);
            return;
        }
        list_bytes_read += bytes_read;

        // Show this chunk.
        if (STRINGS_MATCH(chunk_header.four_cc, "LIST")) {
            char four_cc[4];
            success = CdiOsRead(file_handle, four_cc, 4, &bytes_read);
            if (!success || (4 != bytes_read)) {
                TestConsoleLog(kLogError, "Failed to read form type.");
                return;
            }
            list_bytes_read += bytes_read;
            TestConsoleLog(kLogInfo, "%s%s (%"PRIu32" bytes):", Space(indentation), FourCC(four_cc), chunk_header.size);
            ShowRiffList(file_handle, chunk_header.size - 4, indentation + 2, max_line_length);
        } else {
            char* p = CdiOsMemAlloc(chunk_header.size);
            assert(NULL != p);
            success = CdiOsRead(file_handle, p, chunk_header.size, &bytes_read);
            if (!success || (chunk_header.size != bytes_read)) {
                CdiOsMemFree(p);
                TestConsoleLog(kLogError, "Failed to read data for [%s] chunk (%"PRIu32" vs %"PRIu32" expected).",
                    FourCC(chunk_header.four_cc), bytes_read, chunk_header.size);
                return;
            }
            char print_buffer[256] = { 0 };
            assert(max_line_length < ARRAY_ELEMENT_COUNT(print_buffer));
            StringDumpChunk(indentation, chunk_header, p, max_line_length, print_buffer);
            CdiOsMemFree(p);
            TestConsoleLog(kLogInfo, "%s", print_buffer);
        }
        list_bytes_read += chunk_header.size;
    }
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool StartRiffPayloadFile(StreamSettings* stream_settings_ptr, CdiFileID read_file_handle)
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

bool GetNextRiffPayloadSize(TestConnectionInfo* connection_info_ptr, StreamSettings* stream_settings_ptr,
                            CdiFileID read_file_handle, int* ret_payload_size_ptr)
{
    bool return_val = true;

    RiffChunkHeader chunk_header; // Buffer for holding chunk headers four_cc code and chunk size.

    uint32_t bytes_read = 0;
    if (read_file_handle) {
        return_val = CdiOsRead(read_file_handle, &chunk_header, sizeof(RiffChunkHeader), &bytes_read);
    } else {
        TEST_LOG_CONNECTION(kLogError, "No file handle for RIFF File");
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
        TEST_LOG_CONNECTION(kLogError, "Failed to read chunk header from file [%s]. Read [%d] header bytes.",
        stream_settings_ptr->file_read_str, bytes_read);
        return_val = false;
    }

    // For now check if the chunk ID is "ANC ". NOTE: this check may be removed or expanded in the future to support
    // additional chunk IDs.
    // payload types.
    if (!STRINGS_MATCH(chunk_header.four_cc, "ANC ")) {
        TEST_LOG_CONNECTION(kLogError, "RIFF File [%s] subchunk ID is not 'ANC '.", stream_settings_ptr->file_read_str);
        return_val = false;
    }

    if (return_val) {
        *ret_payload_size_ptr = chunk_header.size;
        // Payload size just be larger than the larger RIFF file payload in the source file.
        if (*ret_payload_size_ptr > stream_settings_ptr->payload_size) {
            TEST_LOG_CONNECTION(kLogError, "Payload size from RIFF file [%d] is larger than the payload buffer [%d].",
                                ret_payload_size_ptr, stream_settings_ptr->payload_size);
            return_val = false;
        }
    }

    return return_val;
}


/**
 * @brief Prints information about the contents of a RIFF file.
 *
 * @param file_path_str Pointer to the file path string.
 */
void ReportRiffFileContents(const char* file_path_str)
{
    CdiFileID file_handle;
    if (!CdiOsOpenForRead(file_path_str, &file_handle)) {
        return;
    }

    RiffFileHeader file_header;
    if (!IsRiffFile(file_handle, file_path_str, &file_header)) {
        return;
    }

    // Print the contents.
    TestConsoleLog(kLogInfo, "");
    TestConsoleLog(kLogInfo, "%4s (%"PRIu32" bytes):", FourCC(file_header.form_type), file_header.chunk_header.size);
    ShowRiffList(file_handle, file_header.chunk_header.size - 4, 2, 80);

    CdiOsClose(file_handle);
}

