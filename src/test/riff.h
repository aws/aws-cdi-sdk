
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
 * @brief Get the size of the next payload from a RIFF file. The RIFF file specifies payload size in the file.
 * Once the payload size has been read the size can be used in GetNextPayload() to read the next payload.
 *
 * @param   connection_info_ptr     Pointer to test connection information.
 * @param   stream_settings_ptr     Pointer to stream settings.
 * @param   read_file_handle        The file handle to read RIFF payload header from.
 * @param   ret_payload_size_ptr    Returns the size of the next payload to be read from the RIFF file.
 *
 * @return                          If successful return true, otherwise returns false.
 *
 * @see StartRiffPayloadFile()
 */
bool GetNextRiffPayloadSize(TestConnectionInfo* connection_info_ptr, StreamSettings* stream_settings_ptr,
                            CdiFileID read_file_handle, int* ret_payload_size_ptr);

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
bool StartRiffPayloadFile(StreamSettings* stream_settings_ptr, CdiFileID read_file_handle);

/**
 * @brief Prints information about the contents of a RIFF file.
 *
 * @param file_path_str Pointer to the file path string.
 */
void ReportRiffFileContents(const char* file_path_str);


#endif // RIFF_H__

