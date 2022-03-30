
// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations of functions creating payloads from RIFF files.
 */

#ifndef RIFF_H__
#define RIFF_H__

#include <stdint.h>

#include "cdi_os_api.h"
#include "test_control.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Symbols referring two different kinds of RIFF data to show.
enum RiffDumpMode {
    kRiffDumpNone,           ///< Don't dump anything.
    kRiffDumpRaw,            ///< Dump RIFF chunks.
    kRiffDumpDid,            ///< When a CID file, show DID and SDID of ancillary payloads.
    kRiffDumpClosedCaptions, ///< When a CID file, show closed caption data, if any.
};

/// Structure for the 8 byte chunk header that proceeds every payload.
typedef struct {
    /// Four character code for indicating the form type. The test checks for form type 'CDI '.
    char four_cc[4];
    /// The size of the chunk data in bytes. This size becomes stream_info->next_payload_size if using riff file for
    /// --read_file option.
    uint32_t size;
} RiffChunkHeader;

/// Structure for the 12 byte file header at the start of every RIFF file.
typedef struct {
    /// Chunk header for the RIFF chunk of the RIFF file.
    RiffChunkHeader chunk_header;
    /// The four character code that indicates the form type of the RIFF file. The test app looks for code 'CDI '.
    char form_type[4];

} RiffFileHeader;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Get the size of the next chunk from a RIFF file. The RIFF file specifies chunk size in the file.
 * Once the chunk size has been read the size can be used in GetNextPayload() to read the next chunk.
 *
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   read_file_handle        The file handle to read RIFF chunk header from.
 * @param   ret_chunk_size_ptr      Returns the size of the next chunk to be read from the RIFF file.
 *
 * @return                          If successful return true, otherwise returns false.
 *
 * @see StartRiffPayloadFile()
 */
bool GetNextRiffChunkSize(const StreamSettings* stream_settings_ptr,
                          CdiFileID read_file_handle, int* ret_chunk_size_ptr);

/**
 * @brief Reads the initial header information from the RIFF file and verifies that the file header indicates a valid
 * file. After this is performed the file is ready to read the next payload size using GetNextRiffPayloadSize().
 *
 *                                 RIFF format
 *                                   bytes
 *       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
 *      'R' 'I' 'F' 'F' / size 4 Bytes \/form = 'CDI '\/Chunk = 'ANC '\
 *      / chunk size 4B\/payload data is chunk_size in bytes in size...
 *      ...............................................................
 *      ...............................\/Chunk2='ANC '\/chunk2 size 4B\
 *      /payload number 2 is chunk2 size in bytes .....................
 *      ***************************************************************
 *      /Chunk-n='ANC '\/Chunk-n size \/Chunk N data of chunk-n size  \
 *      ...............................................................
 *
 * For additional RIFF file information please see https://johnloomis.org/cpe102/asgn/asgn1/riff.html.
 *
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   read_file_handle        The file handle to read RIFF file header from.
 *
 * @return                          If successful return true, otherwise returns false.
 */
bool StartRiffPayloadFile(const StreamSettings* stream_settings_ptr, CdiFileID read_file_handle);

/**
 * @brief Prints information about the contents of a RIFF file.
 *
 * @param file_path_str Pointer to the file path string.
 * @param max_line_length Maximum number of characters of description to print per chunk.
 * @param mode Dump mode selecting the kind of data to show.
 *
 * @return True when file was processed successfully, false when an error occurred.
 */
bool ReportRiffFileContents(const char* file_path_str, int max_line_length, int mode);

/**
 * Check if RIFF file data is decodable ancillary data. A run of cdi_test with --riff includes payload decoding as
 * one of the checks. A RIFF file that does contain actual ancillary data is therefore unsuitable as test input, as it
 * will cause payload errors by failing the decoding check. This function tells whether a RIFF file is suitable for
 * testing.
 *
 * @param file_path_str Pointer to the file path string.
 *
 * @return True if and only if the file contains decodable ancillary data.
 */
bool RiffFileContainsAncillaryData(const char* file_path_str);

#endif // RIFF_H__

