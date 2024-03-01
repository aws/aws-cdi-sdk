// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and functions for the NDI-CDI Converter application.
*/

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "cdi_baseline_profile_02_00_api.h"
#include "test_common.h"
#include "ndi_test.h"
#include "ndi_wrapper.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief Enable this define to disable NDI video frame repeat (enabled by default for pending test/logic changes as
/// needed). Note: When a static image is used, NDI only transmits one video frame per second. So for CDI, we should be
// repeating frames.
#define DISABLE_REPEAT_NDI_VIDEO_FRAME

/// @brief Default timeout for RECV call.
#define DEFAULT_RECV_TIMEOUT_MS                 (1*1000) // 1 second is default.

/// @brief Expected frame rate multiplied by this value determines how long to wait before repeating a frame.
#define REPEAT_FRAME_TIMEOUT_FACTOR             (1.3)

/// @brief Number of bytes in CDI audio sample. CDI requests 24-bit int for audio, so needs three bytes.
#define CDI_BYTES_PER_AUDIO_SAMPLE              (3)

/// @brief Default sleep time in milliseconds until new sources are found on the network. Set to wait at most 1 second.
#define NDI_TIMEOUT_FOR_FINDING_SOURCES_IN_MS   (1000)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************


//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Convert a CDI timestamp to NDI.
 *
 * @return NDI timestamp value.
 */
static int64_t CDITimestampToNdi(const CdiPtpTimestamp* cdi_timestamp_ptr)
{
    return cdi_timestamp_ptr->seconds * 1000000000 + cdi_timestamp_ptr->nanoseconds;
}

/**
 * @brief Find maximum between two numbers.
 *
 * @param num1 Float number.
 * @param num2 Float number.
 *
 * @return Returns the greater number of the two given.
 */
static double MAX(float num1, float num2)
{
    return (num1 > num2 ) ? num1 : num2;
}

/**
 * @brief Find minimum between two numbers.
 *
 * @param num1 Float number.
 * @param num2 Float number.
 *
 * @return Returns the lesser number of the two given.
 */
static double MIN(float num1, float num2)
{
    return (num1 > num2 ) ? num2 : num1;
}

/**
 * @brief Convert audio from 32-bit float Little-Endian to 24-bit int Big-Endian PCM.
 * NDI audio frame appear in stacked channels in 32-bit float Little Endian format and
 * CDI audio frame is requested to be in interleaved channels in 24-bit int Big-Endian PCM.
 * Function goes through each NDI channel, reads audio segment in 4 byte segments, converts segment from
 * 32-bit float Little-Endian to 24-bit int Big-Endian PCM, and places 24-bit int segment in jumps,
 * that match interleaved spacing with corresponding number of channels, in temporary buffer. Temporary buffer replaces
 * original NDI audio buffer and the size of the audio in bytes is updated to its new size.
 *
 * @param original_audio_ptr 32-bit float audio given from NDI audio frame.
 * @param num_channels Number of audio channels given from NDI audio frame.
 * @param num_samples Number of audio samples given from NDI audio frame.
 * @param channel_stride_in_bytes Length of channel stride in bytes given from NDI audio frame.
 * @param audio_size_byte_ret_ptr New total length of audio size in bytes for CDI.
 *
 * @return Returns status of whether function was successful or not.
 */
static bool NdiToCdiAudioConversion(float* original_audio_ptr, int num_channels, int num_samples,
                                    int channel_stride_in_bytes, int* audio_size_byte_ret_ptr)
{
    // Point to the beginning memory of the original audio.
    unsigned char* base_uc_ptr = (unsigned char*)original_audio_ptr;

    // Create a temporary buffer which will store the CDI interleaved audio buffer.
    static unsigned char* temp_buffer_ptr = NULL;
    static uint32_t temp_buffer_size;
    uint32_t interleaved_buffer_size = num_channels * num_samples * 3;

    // Allocate memory for temporary buffer.
    if (temp_buffer_ptr == NULL) {
        temp_buffer_ptr = CdiOsMemAllocZero(interleaved_buffer_size + 1000);
        temp_buffer_size = interleaved_buffer_size;
    } else if (temp_buffer_size != interleaved_buffer_size) {
        // if interleaved buffer size changes, reallocate memory for temporary buffer.
        CdiOsMemFree(temp_buffer_ptr);
        temp_buffer_ptr = CdiOsMemAllocZero(interleaved_buffer_size + 1000);
        temp_buffer_size = interleaved_buffer_size;
    }

    // Validate that pointers are non-NULL.
    assert(original_audio_ptr && temp_buffer_ptr);

    // for each NDI channel, insert 24-bit int audio segment in correct spot of temp buffer.
    for (int current_channel = 0; current_channel < num_channels; current_channel++) {
        // Memory location of where to write 24-bit int for this channel in temp buffer.
        unsigned char* interleaved_dest_ptr = temp_buffer_ptr + (current_channel * CDI_BYTES_PER_AUDIO_SAMPLE);
        // Memory location of where to read 32-bit float for this channel in original NDI audio.
        float* channel_src_ptr = (float*)(base_uc_ptr + (current_channel * channel_stride_in_bytes));

        // for each channel sample, convert 32-bit float to 24-bit int + insert segment in correct spot of temp buffer.
        for (int current_sample = 0; current_sample < num_samples; current_sample++) {
            // Get 4 byte sample of 32-bit float of current NDI audio memory location.
            float sample_float = *channel_src_ptr;
            // 4 byte sample is a frequency wave constrained to [-1, 1].
            // Code ensures sample is in range, but will pick sample frequency to scale up.
            // Scaled sample will make a large number where most significant bits are no longer in decimal place.
            double scaled_double = MAX(-1.0, MIN(1.0, sample_float)) * 0x7fffffff;
            // Get integer portion of large number.
            // Integer portion now represents audio frequency.
            signed int scaled_signed_int = (signed int)scaled_double;

            // Shifting of 3 most important bytes to turn Little-Endian into Big-Endian with 3 byte representation
            // for CDI interleaved destination.
            interleaved_dest_ptr[2] = (unsigned char)(scaled_signed_int >> 8);
            interleaved_dest_ptr[1] = (unsigned char)(scaled_signed_int >> 16);
            interleaved_dest_ptr[0] = (unsigned char)(scaled_signed_int >> 24);

            // Moves original NDI audio memory location by 4 bytes for next 32-bit float read for channel.
            // Note: The audio samples in the NDI source audio are not channel interleaved.
            channel_src_ptr++;

            // Updates memory location of where to put next 24-bit int audio for channel.
            // Updates by number of 3 byte channels in between current location and next interleaved location
            // for same channel.
            interleaved_dest_ptr += num_channels * CDI_BYTES_PER_AUDIO_SAMPLE;
        }
    }
    // Memory copy newly written temp buffer with 24-bit int audio to original audio for later use.
    memcpy(original_audio_ptr, temp_buffer_ptr, (size_t)interleaved_buffer_size);

    // Update size of new audio buffer with 24-bit int audio.
    *audio_size_byte_ret_ptr = interleaved_buffer_size;
    return true;
}

/**
 * @brief Convert CDI audio 24-bit Big-Endian interleaved PCM to NDI 32-bit float Little Endian non-interleaved.
 *
 * @param cdi_audio_ptr Pointer to CDI audio sample data.
 * @param cdi_audio_size Size in bytes of CDI audio data.
 * @param num_channels Number of audio channels.
 * @param num_samples_per_channel Number of audio samples per channel.
 * @param ndi_channel_stride_in_bytes Stride in bytes of each NDI channel.
 * @param ndi_audio_ptr Pointer where to write the NDI audio data.
 * @param ndi_size_ptr Size of the NDI audio data buffer.
 *
 * @return Returns status of whether function was successful or not.
 */
static bool CdiToNdiAudioConversion(const uint8_t* cdi_audio_ptr, int cdi_audio_size, int num_channels,
                                    int num_samples_per_channel, int ndi_channel_stride_in_bytes, float* ndi_audio_ptr,
                                    int* ndi_size_ptr)
{
    // Validate CDI audio contains the correct number of 24-bit audio samples.
    assert(cdi_audio_size <= num_channels * num_samples_per_channel * CDI_BYTES_PER_AUDIO_SAMPLE);

    int ndi_audio_size = num_channels * num_samples_per_channel * sizeof(float);
    // Validate that the NDI buffer is large enough to hold the NDI float audio samples.
    int ndi_audio_buffer_size = *ndi_size_ptr;
    assert(ndi_audio_size <= ndi_audio_buffer_size);

    // Validate that pointers are non-NULL.
    assert(cdi_audio_ptr && ndi_audio_ptr);
    uint8_t* ndi_audio_byte_ptr = (uint8_t*)ndi_audio_ptr;

    // For each channel, insert 24-bit int audio segment in correct spot of temp buffer.
    for (int current_channel = 0; current_channel < num_channels; current_channel++) {
        // Memory location of where to read CDI 24-bit int for this channel.
        const uint8_t* interleaved_src_ptr = cdi_audio_ptr + (current_channel * CDI_BYTES_PER_AUDIO_SAMPLE);

        // Memory location of where to write NDI 32-bit float for this channel.
        float* channel_dest_ptr = (float*)(ndi_audio_byte_ptr + (current_channel * ndi_channel_stride_in_bytes));

        // For each channel sample, convert CDI 24-bit int to NDI 32-bit float and store in NDI audio buffer.
        for (int current_sample = 0; current_sample < num_samples_per_channel; current_sample++) {
            // Get 24-bit Big-Endian CDI sample and convert to 32-bit Little-Endian.
            // Shift the 3 bytes to most significant position.
            signed int scaled_signed_int = (interleaved_src_ptr[0] << 24) | (interleaved_src_ptr[1] << 16) |
                                           (interleaved_src_ptr[2] << 8);
            double scaled_double = (double)scaled_signed_int;
            float sample_float = scaled_double / 0x7fffffff;
            sample_float = MAX(-1.0, MIN(1.0, sample_float));
            *channel_dest_ptr = sample_float;

            // Moves NDI audio memory location for next 32-bit float.
            channel_dest_ptr++;

            // Updates memory location of where to read next 24-bit CDI audio for channel.
            // Updates by number of 3 byte channels in between current location and next interleaved location
            // for same channel.
            interleaved_src_ptr += num_channels * CDI_BYTES_PER_AUDIO_SAMPLE;
        }
    }

    // Set returned size of NDI audio.
    *ndi_size_ptr = ndi_audio_size;

    return true;
}

/**
 * @brief Convert a NDI video frame to CDI.
 *
 * @param frame_data_ptr Pointer to NDI frame data.
 * @param buffer_size_ptr Pointer where to write the returned size of the CDI payload.
 * @param payload_buffer_ptr Pointer where to write the returned pointer to the CDI payload data.
 * @param timestamp_ptr Pointer where to write the returned CDI timestamp.
 * @param avm_config_ptr Pointer where to write the return CDI AVM data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus NdiToCdiVideo(const FrameData* frame_data_ptr, int* buffer_size_ptr, void** payload_buffer_ptr,
                                     int64_t* timestamp_ptr, CdiAvmBaselineConfig* avm_config_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Buffer Information.
    *buffer_size_ptr = (frame_data_ptr->data.video_frame.yres *
                        frame_data_ptr->data.video_frame.line_stride_in_bytes);
    *payload_buffer_ptr = frame_data_ptr->data.video_frame.p_data;
    *timestamp_ptr = frame_data_ptr->data.video_frame.timestamp;

    // AVM Video Configuration.
    avm_config_ptr->payload_type = kCdiAvmVideo;
    avm_config_ptr->video_config.width = frame_data_ptr->data.video_frame.xres;
    avm_config_ptr->video_config.height = frame_data_ptr->data.video_frame.yres;
    avm_config_ptr->video_config.frame_rate_num = frame_data_ptr->data.video_frame.frame_rate_N;
    avm_config_ptr->video_config.frame_rate_den = frame_data_ptr->data.video_frame.frame_rate_D;
    avm_config_ptr->video_config.version.major = 01; // Test using baseline profile V01.00.
    avm_config_ptr->video_config.version.minor = 00;

    // Colorimetry: SD Resolutions= BT.601. HD Resolutions= Rec.709. UHD Resolutions= Rec.2020.
    if (frame_data_ptr->data.video_frame.xres < 1280) {
        avm_config_ptr->video_config.colorimetry = kCdiAvmVidColorimetryBT709;
    } else if (frame_data_ptr->data.video_frame.xres > 1920) {
        avm_config_ptr->video_config.colorimetry = kCdiAvmVidColorimetryBT2020;
    } else {
        avm_config_ptr->video_config.colorimetry = kCdiAvmVidColorimetryBT709;
    }

    avm_config_ptr->video_config.tcs = kCdiAvmVidTcsSDR; // Standard Dynamic Range video stream.
    avm_config_ptr->video_config.range = kCdiAvmVidRangeFull;
    avm_config_ptr->video_config.par_width = 1;
    avm_config_ptr->video_config.par_height = 1;
    avm_config_ptr->video_config.start_vertical_pos = 0;
    avm_config_ptr->video_config.vertical_size = 0; // 0= Use full frame size.
    avm_config_ptr->video_config.start_horizontal_pos = 0;
    avm_config_ptr->video_config.horizontal_size = 0; // 0= Use full frame size.

    // Picture/Pixel Aspect Ratio (default is set above).
    float calculated_aspect_ratio = (float)frame_data_ptr->data.video_frame.xres / frame_data_ptr->data.video_frame.yres;
    if (frame_data_ptr->data.video_frame.picture_aspect_ratio &&
        frame_data_ptr->data.video_frame.picture_aspect_ratio != calculated_aspect_ratio) {
        // Use picture aspect ratio vs calculated ratio to determine PAR width and height.
        avm_config_ptr->video_config.par_width = (uint16_t )(frame_data_ptr->data.video_frame.picture_aspect_ratio * 1000);
            avm_config_ptr->video_config.par_height = (uint16_t )(calculated_aspect_ratio * 1000);
    }

    // Video Frame Format.
    // Make default frame format type progressive.
    avm_config_ptr->video_config.interlace = false;
    avm_config_ptr->video_config.segmented = false;
    // if frame format type is interleaved/interlaced, change.
    if (frame_data_ptr->data.video_frame.frame_format_type == NDIlib_frame_format_type_interleaved) {
        avm_config_ptr->video_config.interlace = true;
    }

    // Video Type.
    // Maps NDI FourCC to corresponding CDI sampling.
    avm_config_ptr->video_config.alpha_channel = kCdiAvmAlphaUnused;
    avm_config_ptr->video_config.depth = kCdiAvmVidBitDepth8;
    if (frame_data_ptr->data.video_frame.FourCC ==
        (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('U', 'Y', 'V', 'Y')) {
        // UYVY to YCbCr422.
        avm_config_ptr->video_config.sampling = kCdiAvmVidYCbCr422;
    } else if (frame_data_ptr->data.video_frame.FourCC ==
            (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('U', 'Y', 'V', 'A')) {
        // UYVA to YCbCr422 with alpha channel.
        avm_config_ptr->video_config.sampling = kCdiAvmVidYCbCr422;
        avm_config_ptr->video_config.alpha_channel = kCdiAvmAlphaUsed;
    } else if (frame_data_ptr->data.video_frame.FourCC ==
            (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('N', 'V', '1', '2')) {
        // NV12 to YCbCr422.
        avm_config_ptr->video_config.sampling = kCdiAvmVidYCbCr422;
    } else if (frame_data_ptr->data.video_frame.FourCC ==
            (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('R', 'G', 'B', 'A')) {
        // RGBA to RGB with alpha.
        avm_config_ptr->video_config.sampling = kCdiAvmVidRGB;
        avm_config_ptr->video_config.alpha_channel = kCdiAvmAlphaUsed;
    } else if (frame_data_ptr->data.video_frame.FourCC ==
            (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('R', 'G', 'B', 'X')) {
        // RGBX to RGB.
        avm_config_ptr->video_config.sampling = kCdiAvmVidRGB;
    } else {
        // Video type not supported.
        CDI_LOG_THREAD(kLogInfo, "Video Source not supported in CDI.");
        rs = kCdiStatusInvalidPayload;
    }
    return rs;
}

/**
 * @brief Convert a CDI video frame to NDI.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus CdiToNdiVideo(const CdiPtpTimestamp* cdi_timestamp_ptr, const CdiAvmBaselineConfig* avm_config_ptr,
                                     int payload_size, const CdiSgList* sgl_ptr, FrameData* frame_data_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    assert(NULL == sgl_ptr->sgl_head_ptr->next_ptr); // Must be linear buffer from CDI (configured by kCdiLinearBuffer).
    frame_data_ptr->data.video_frame.p_data = sgl_ptr->sgl_head_ptr->address_ptr;
    uint8_t* payload_buffer_ptr = frame_data_ptr->data.video_frame.p_data;

    // Video Type. Maps 10-bit CDI to 8-bit NDI FourCC.
    if (avm_config_ptr->video_config.depth == kCdiAvmVidBitDepth10) {
        // Convert 10-bit to 8-bit.
		uint32_t pgroup;

		uint8_t* dest_ptr = (uint8_t*)payload_buffer_ptr;
		uint8_t* src_ptr = (uint8_t*)payload_buffer_ptr;
        int payload_size_10bit = payload_size;

		// For every pgroup (a pgroup being 2 pixels in this case because we use the U and V elements across 2 Y values).
		for (int i = 0; i < payload_size_10bit; i += 5) {
			*dest_ptr = *src_ptr; // Get 8 most significant bits from first 10-bit.
            pgroup = *(src_ptr + 1) << 24 | *(src_ptr + 2) << 16 | *(src_ptr + 3) << 8 | *(src_ptr + 4);
            *(dest_ptr + 1) = pgroup >> 22;
            *(dest_ptr + 2) = pgroup >> 12;
            *(dest_ptr + 3) = pgroup >> 2;
            src_ptr += 5;
            dest_ptr += 4;
		}
        payload_size = dest_ptr - (uint8_t*)payload_buffer_ptr;
    } else if (avm_config_ptr->video_config.depth != kCdiAvmVidBitDepth8) {
        CDI_LOG_THREAD(kLogInfo, "AVM invalid video bit depth[%s]. Only 8-bit supported.",
                       CdiAvmKeyEnumToString(kKeyAvmVideoBitDepthType, avm_config_ptr->video_config.depth,
                                             &avm_config_ptr->video_config.version));
        return kCdiStatusInvalidPayload;
    }

    frame_data_ptr->frame_type = kNdiVideo;
    frame_data_ptr->p_data_size = payload_size;
    frame_data_ptr->data.video_frame.p_data = payload_buffer_ptr;
    frame_data_ptr->ref_count = 0;

    // Buffer Information.
    assert(payload_size <= 1920*1080*3); // For 8-bit only
    frame_data_ptr->data.video_frame.p_data = payload_buffer_ptr;

    // Convert CDI timestamp to NDI.
    frame_data_ptr->data.video_frame.timestamp = CDITimestampToNdi(cdi_timestamp_ptr);

    // AVM Video Configuration.
    assert(avm_config_ptr->payload_type == kCdiAvmVideo);
    frame_data_ptr->data.video_frame.xres = avm_config_ptr->video_config.width;
    frame_data_ptr->data.video_frame.yres = avm_config_ptr->video_config.height;
    frame_data_ptr->data.video_frame.frame_rate_N = avm_config_ptr->video_config.frame_rate_num;
    frame_data_ptr->data.video_frame.frame_rate_D = avm_config_ptr->video_config.frame_rate_den;

    // Colorimetry is not passed by NDI API. This is assumed:
    // SD Resolutions= BT.601. HD Resolutions= Rec.709. UHD Resolutions= Rec.2020.

    // Picture/Pixel Aspect Ratio.
    frame_data_ptr->data.video_frame.picture_aspect_ratio = (float)avm_config_ptr->video_config.width / (float)avm_config_ptr->video_config.height;
    frame_data_ptr->data.video_frame.picture_aspect_ratio *= (float)avm_config_ptr->video_config.par_width /
                                                             (float)avm_config_ptr->video_config.par_height;

    // Video Frame Format.
    if (avm_config_ptr->video_config.interlace) {
        frame_data_ptr->data.video_frame.frame_format_type = NDIlib_frame_format_type_interleaved;
        // TODO MIKEH: Field needed? ie. NDIlib_frame_format_type_field_0 or NDIlib_frame_format_type_field_1
    } else {
        frame_data_ptr->data.video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
    }

    if (avm_config_ptr->video_config.sampling == kCdiAvmVidYCbCr422) {
        // YCbCr422 to UYVY.
        frame_data_ptr->data.video_frame.FourCC = (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('U', 'Y', 'V', 'Y');
        frame_data_ptr->data.video_frame.line_stride_in_bytes = avm_config_ptr->video_config.width * 2; // 8-bit UYVY
    } else if (avm_config_ptr->video_config.sampling == kCdiAvmVidRGB) {
        if (avm_config_ptr->video_config.alpha_channel == kCdiAvmAlphaUsed) {
            // RGBA to RGB with alpha.
            frame_data_ptr->data.video_frame.FourCC = (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('R', 'G', 'B', 'A');
            frame_data_ptr->data.video_frame.line_stride_in_bytes = avm_config_ptr->video_config.width * 4; // TODO Verify
        } else {
            // RGBX to RGB.
            frame_data_ptr->data.video_frame.FourCC = (NDIlib_FourCC_video_type_e)NDI_LIB_FOURCC('R', 'G', 'B', 'X');
            frame_data_ptr->data.video_frame.line_stride_in_bytes = avm_config_ptr->video_config.width * 3; // TODO Verify
        }
    } else {
        // Video type not supported.
        CDI_LOG_THREAD(kLogInfo, "AVM video source [%s] not supported in NDI.",
                       CdiAvmKeyEnumToString(kKeyAvmVideoSamplingType, avm_config_ptr->video_config.sampling,
                                             &avm_config_ptr->video_config.version));
        rs = kCdiStatusInvalidPayload;
    }
    return rs;
}

/**
 * @brief Convert a NDI audio frame to CDI.
 *
 * @param frame_data_ptr Frame Data pointer which stores NDI frame.
 * @param buffer_size_ptr Argument passed in to CDI send containing size of data.
 * @param payload_buffer_ptr Argument passed in to CDI send containing data.
 * @param timestamp_ptr Argument passed in to CDI send containing timestamp of data.
 * @param avm_config_ptr Argument passed in to CDI send containing configuration of data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus NdiToCdiAudio(FrameData* frame_data_ptr, int* buffer_size_ptr, void** payload_buffer_ptr,
                                     int64_t* timestamp_ptr, CdiAvmBaselineConfig* avm_config_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Buffer Information.
    assert(frame_data_ptr->p_data_size != 0);
    *buffer_size_ptr = frame_data_ptr->p_data_size;
    *payload_buffer_ptr = frame_data_ptr->data.audio_frame.p_data;
    *timestamp_ptr = frame_data_ptr->data.audio_frame.timestamp;

    // AVM Audio Configuration.
    avm_config_ptr->payload_type = kCdiAvmAudio;
    avm_config_ptr->audio_config.version.major = 01; // Test using baseline profile V01.00.
    avm_config_ptr->audio_config.version.minor = 00;

    // Sample Rate.
    if (frame_data_ptr->data.audio_frame.sample_rate == 48000) {
        avm_config_ptr->audio_config.sample_rate_khz = kCdiAvmAudioSampleRate48kHz;
    } else if (frame_data_ptr->data.audio_frame.sample_rate == 96000) {
        avm_config_ptr->audio_config.sample_rate_khz = kCdiAvmAudioSampleRate96kHz;
    } else {
        // Audio Sampling Rate not supported.
        CDI_LOG_THREAD(kLogWarning, "NDI audio sample rate[%d] not supported in CDI. Must be 48khz or 96khz.",
                       frame_data_ptr->data.audio_frame.sample_rate);
        rs = kCdiStatusInvalidPayload;
    }

    if (kCdiStatusOk == rs) {
        // Audio grouping. Maps number of audio channels to audio grouping.
        switch (frame_data_ptr->data.audio_frame.no_channels) {
            case 1: // Mono.
            avm_config_ptr->audio_config.grouping = kCdiAvmAudioM;
            break;
            case 2: // Standard Stereo (left, right).
            avm_config_ptr->audio_config.grouping = kCdiAvmAudioST;
            break;
            case 4: // One SDI audio group (1, 2, 3, 4).
            avm_config_ptr->audio_config.grouping = kCdiAvmAudioSGRP;
            break;
            case 6: // 5.1 Surround (L, R, C, LFE, Ls, Rs).
            avm_config_ptr->audio_config.grouping = kCdiAvmAudio51;
            break;
            case 8: // Surround (L, R, C, LFE, Lss, Rss, Lrs, Rrs).
            avm_config_ptr->audio_config.grouping = kCdiAvmAudio71;
            break;
            case 24: // 22.2 Surround (SMPTE ST 2036-2, Table 1).
            avm_config_ptr->audio_config.grouping = kCdiAvmAudio222;
            break;
            default: // Number of channels is not supported.
                CDI_LOG_THREAD(kLogInfo, "NDI [%d]channel audio is not supported in CDI.",
                               frame_data_ptr->data.audio_frame.no_channels);
                rs = kCdiStatusInvalidPayload;
            break;
        }
    }

    if (kCdiStatusOk == rs) {
        // Force english Language. TODO: Where to get from NDI?
        avm_config_ptr->audio_config.language[0] = 'e';
        avm_config_ptr->audio_config.language[1] = 'n';
        avm_config_ptr->audio_config.language[2] = 'g';
    }

    return rs;
}

/**
 * @brief Convert CDI audio to NDI audio.
 *
 * @param cdi_timestamp_ptr Pointer to CDI timestamp.
 * @param avm_config_ptr Pointer to CDI AVM configuration data.
 * @param payload_size Size of CDI payload in bytes.
 * @param sgl_ptr Pointer to CDI payload data in Scatter-Gather-List format.
 * @param frame_data_ptr Pointer where to write NDI frame data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus CdiToNdiAudio(const CdiPtpTimestamp* cdi_timestamp_ptr, const CdiAvmBaselineConfig* avm_config_ptr,
                                     int payload_size, const CdiSgList* sgl_ptr, FrameData* frame_data_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    assert(payload_size != 0);

    assert(NULL == sgl_ptr->sgl_head_ptr->next_ptr); // Must be linear buffer from CDI (configured by kCdiLinearBuffer).
    uint8_t* payload_buffer_ptr = sgl_ptr->sgl_head_ptr->address_ptr;

    frame_data_ptr->data.audio_frame.timestamp = CDITimestampToNdi(cdi_timestamp_ptr);

    // AVM Audio Configuration.
    assert(avm_config_ptr->payload_type == kCdiAvmAudio);
    frame_data_ptr->frame_type = kNdiAudio;
    frame_data_ptr->ref_count = 0;

    // Sample Rate.
    if (avm_config_ptr->audio_config.sample_rate_khz == kCdiAvmAudioSampleRate48kHz) {
        frame_data_ptr->data.audio_frame.sample_rate = 48000;
    } else if (avm_config_ptr->audio_config.sample_rate_khz == kCdiAvmAudioSampleRate96kHz) {
        frame_data_ptr->data.audio_frame.sample_rate = 96000;
    }

    // Grouping. Maps number of audio channels to audio grouping.
    switch (avm_config_ptr->audio_config.grouping) {
        case kCdiAvmAudioM: // Mono.
            frame_data_ptr->data.audio_frame.no_channels = 1;
        break;
        case kCdiAvmAudioST: // Standard Stereo (left, right).
            frame_data_ptr->data.audio_frame.no_channels = 2;
        break;
        case kCdiAvmAudioSGRP: // One SDI audio group (1, 2, 3, 4).
            frame_data_ptr->data.audio_frame.no_channels = 4;
        break;
        case kCdiAvmAudio51: // 5.1 Surround (L, R, C, LFE, Ls, Rs).
            frame_data_ptr->data.audio_frame.no_channels = 6;
        break;
        case kCdiAvmAudio71: // Surround (L, R, C, LFE, Lss, Rss, Lrs, Rrs).
            frame_data_ptr->data.audio_frame.no_channels = 8;
        break;
        case kCdiAvmAudio222: // 22.2 Surround (SMPTE ST 2036-2, Table 1).
            frame_data_ptr->data.audio_frame.no_channels = 24;
        break;
        default:
            // Number of channels is not supported.
            CDI_LOG_THREAD(kLogInfo, "CDI audio grouping[%d] not supported in NDI.",
                           CdiAvmKeyEnumToString(kKeyAvmAudioChannelGroupingType, avm_config_ptr->audio_config.grouping,
                                                 &avm_config_ptr->audio_config.version));
            rs = kCdiStatusInvalidPayload;
        break;
    }

    if (kCdiStatusOk == rs) {
        int num_samples_per_channel = payload_size / CDI_BYTES_PER_AUDIO_SAMPLE / frame_data_ptr->data.audio_frame.no_channels;
        frame_data_ptr->data.audio_frame.no_samples = num_samples_per_channel;
        frame_data_ptr->data.audio_frame.channel_stride_in_bytes = num_samples_per_channel * sizeof(float);
        if (!CdiToNdiAudioConversion(payload_buffer_ptr, payload_size, frame_data_ptr->data.audio_frame.no_channels,
                                     num_samples_per_channel, frame_data_ptr->data.audio_frame.channel_stride_in_bytes,
                                     frame_data_ptr->data.audio_frame.p_data, &frame_data_ptr->p_data_size)) {
            rs = kCdiStatusNonFatal;
        }
    }
    // TODO MIKEH Language?
    //avm_config_ptr->audio_config.language[0] = 'e';
    //avm_config_ptr->audio_config.language[1] = 'n';
    //avm_config_ptr->audio_config.language[2] = 'g';

    return rs;
}

/**
 * @brief Convert CDI metadata to NDI metadata.
 *
 * @param cdi_timestamp_ptr Pointer to CDI timestamp.
 * @param avm_config_ptr Pointer to CDI AVM configuration data.
 * @param payload_size Size of CDI payload in bytes.
 * @param sgl_ptr Pointer to CDI payload data in Scatter-Gather-List format.
 * @param frame_data_ptr Pointer where to write NDI frame data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus CdiToNdiMetadata(const CdiPtpTimestamp* cdi_timestamp_ptr, const CdiAvmBaselineConfig* avm_config_ptr,
                                        int payload_size, const CdiSgList* sgl_ptr, FrameData* frame_data_ptr)
{
    (void)cdi_timestamp_ptr;
    (void)avm_config_ptr;
    (void)payload_size;
    (void)sgl_ptr;

    frame_data_ptr->frame_type = kNdiMetaData;
    frame_data_ptr->ref_count = 0;

    // TODO: Implement as needed.

    return kCdiStatusOk;
}

/**
 * @brief Convert a NDI metadata frame to CDI.
 *
 * @param frame_data_ptr Frame Data pointer which stores NDI frame.
 * @param buffer_size_ptr Argument passed in to CDI send containing size of data.
 * @param payload_buffer_ptr Argument passed in to CDI send containing data.
 * @param timestamp_ptr Argument passed in to CDI send containing timestamp of data.
 * @param avm_config_ptr Argument passed in to CDI send containing configuration of data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
static CdiReturnStatus NdiToCdiMeta(FrameData* frame_data_ptr, int* buffer_size_ptr, void** payload_buffer_ptr,
                                    int64_t* timestamp_ptr, CdiAvmBaselineConfig* avm_config_ptr)
{
    // Buffer Information.
    *buffer_size_ptr = (frame_data_ptr->data.metadata.length);
    *payload_buffer_ptr = frame_data_ptr->data.metadata.p_data;
    *timestamp_ptr = frame_data_ptr->data.metadata.timecode;

    // AVM Metadata Configuration
    avm_config_ptr->payload_type = kCdiAvmAncillary;
    avm_config_ptr->ancillary_data_config.version.major = 01; // Test using baseline profile V01.00.
    avm_config_ptr->ancillary_data_config.version.minor = 00;

    // Function has no way to fail, so always return success.
    return kCdiStatusOk;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

NdiTime NdiTimeBreakdown(int64_t ndi_timestamp)
{
    // Calculate NDI timestamp in different units.
    NdiTime ndi_time_breakdown;
    // NDI time in seconds.
    ndi_time_breakdown.ndi_time_in_s = floor(ndi_timestamp / 10000000);
    // NDI time in nanoseconds.
    ndi_time_breakdown.ndi_time_in_ns = (ndi_timestamp - (ndi_time_breakdown.ndi_time_in_s * 10000000)) * 100;
    // NDI time in milliseconds.
    ndi_time_breakdown.ndi_time_in_ms = ndi_timestamp / 10000;
    return ndi_time_breakdown;
}

bool NdiInitialize(void) {
    // Not required, but "correct" (see the NDI SDK documentation).
    if (!NDIlib_initialize()) return false;

    return true;
}

NDIlib_send_instance_t NdiCreateSender(const TestSettings* test_settings_ptr)
{
    // Note: Must have initialized the NDI SDK using NdiInitialize().
	NDIlib_send_create_t NDI_send_create_desc;
    memset(&NDI_send_create_desc, 0, sizeof(NDI_send_create_desc));
	NDI_send_create_desc.p_ndi_name = test_settings_ptr->ndi_source_name;
    NDI_send_create_desc.clock_video = true; // clock using video

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
    if (NULL == pNDI_send) {
        CDI_LOG_THREAD(kLogInfo, "NDIlib_send_create() failed using [%s].", NDI_send_create_desc.p_ndi_name);
    }
    return pNDI_send;
}

void NdiShowSources(void)
{
    // Create Finder and Look for Sources.
    NDIlib_find_create_t source_finder = { 0 };
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&source_finder);
    if (!pNDI_find) {
        CDI_LOG_THREAD(kLogError, "NDIlib_find_create_v2 failed.");
        return;
    }

    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = NULL;

    // Wait until wanted source found on network, else proceed with first source found.
    do {
        CDI_LOG_THREAD(kLogInfo, "Looking for NDI sources...");

        // Get the updated list of sources.
        NDIlib_find_wait_for_sources(pNDI_find, NDI_TIMEOUT_FOR_FINDING_SOURCES_IN_MS);
        p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

        // Display all the sources.
        CDI_LOG_THREAD(kLogInfo, "NDI network sources (%u found):", no_sources);
        for (uint32_t i = 0; i < no_sources; i++) {
            CDI_LOG_THREAD(kLogInfo, "#[%u] Name[%s] IP[%s]", i + 1, p_sources[i].p_ndi_name, p_sources[i].p_ip_address);
        }
    } while (0 == no_sources);

    // Destroy the NDI finder. We needed to have access to the pointers to p_sources[source_index].
    NDIlib_find_destroy(pNDI_find);
}

NDIlib_recv_instance_t NdiCreateReceiver(const TestSettings* test_settings_ptr)
{
    // Note: Must have initialized the NDI SDK using NdiInitialize().

    // Create Finder and Look for Sources.
    NDIlib_find_create_t source_finder = { 0 };
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&source_finder);
    if (!pNDI_find) return 0;

    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = NULL;
    NDIlib_source_t sender = {0};
    sender.p_ndi_name = test_settings_ptr->ndi_source_name;
    // IP address and URL address are union type, so assign only one variable if seen.
    if (test_settings_ptr->ndi_source_ip) {
        sender.p_ip_address = test_settings_ptr->ndi_source_ip;
    } else if (test_settings_ptr->ndi_source_url) {
        sender.p_url_address = test_settings_ptr->ndi_source_url;
    }

    bool source_found = false;
    uint32_t source_index = 0;

    // Wait until wanted source found on network, else proceed with first source found.
    while (!source_found) {
        CDI_LOG_THREAD(kLogInfo, "Looking for NDI sources...");

        // Get the updated list of sources.
        NDIlib_find_wait_for_sources(pNDI_find, NDI_TIMEOUT_FOR_FINDING_SOURCES_IN_MS);
        p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

        // Display all the sources.
        CDI_LOG_THREAD(kLogInfo, "NDI network sources (%u found):", no_sources);
        for (uint32_t i = 0; i < no_sources; i++) {
            CDI_LOG_THREAD(kLogInfo, "#[%u] Name[%s] IP[%s]", i + 1, p_sources[i].p_ndi_name, p_sources[i].p_ip_address);

            source_index = i;

            if (sender.p_ndi_name != NULL) {
                // Source name specified.
                if (!strncmp(sender.p_ndi_name, p_sources[i].p_ndi_name, strlen(sender.p_ndi_name))) {
                    CDI_LOG_THREAD(kLogInfo, "NDI source found with specified NDI name.");
                    source_found = true;
                    break;
                }
            } else if (sender.p_ip_address != NULL) {
                // Source IP address or URL address specified.
                // Check if IP address of union matches.
                if (!strncmp(sender.p_ip_address, p_sources[i].p_ip_address, strlen(sender.p_ip_address))) {
                    CDI_LOG_THREAD(kLogInfo, "Source found with specified IP address.");
                    source_found = true;
                    break;
                }
                // Check if URL address of union matches.
                if (!strncmp(sender.p_url_address, p_sources[i].p_url_address, strlen(sender.p_url_address))) {
                    CDI_LOG_THREAD(kLogInfo, "NDI source found with specified URL address.");
                    source_found = true;
                    break;
                }
            } else {
                // No source specified, so just pick first source found.
                CDI_LOG_THREAD(kLogInfo, "NDI source found.");
                source_found = true;
                break;
            }
        }
        // if source specified and not found, ask to check specified source.
        if (!source_found) {
            CDI_LOG_THREAD(kLogInfo, "Specified NDI source not found. Trying again.");
        }
    }

    // Make receiver and connect receiver to source to receive information.
    // We now have at least one source, so we create a receiver to look at it.
    NDIlib_recv_create_v3_t recv_create = { 0 };
    // Set allow_video_fields to true so NDI captures 8-bit UYVY or 8-bit UYVY+A video buffer.
    recv_create.allow_video_fields = true;
    // Set bandwidth to highest so NDI gets the same stream being sent upstream.
    recv_create.bandwidth = NDIlib_recv_bandwidth_highest;
    // Set color_format to receive UYVY video if no alpha channel, and RGBA if alpha channel.
    recv_create.color_format = NDIlib_recv_color_format_UYVY_RGBA;
    NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&recv_create);

    if (pNDI_recv) {
        // Connect to the source.
        NDIlib_recv_connect(pNDI_recv, p_sources + source_index);
    }

    // Destroy the NDI finder. We needed to have access to the pointers to p_sources[source_index].
    NDIlib_find_destroy(pNDI_find);

    // Return NDI receiver.
    return pNDI_recv;
}

void NdiReleasePayload(FrameData* frame_data_ptr)
{
    if (frame_data_ptr->frame_type == kNdiVideo) {
        // Release video memory.
        NDIlib_recv_free_video_v2(frame_data_ptr->connect_info_ptr->pNDI_recv, &frame_data_ptr->data.video_frame);
    } else if (frame_data_ptr->frame_type == kNdiAudio) {
        // Release audio memory.
        NDIlib_recv_free_audio_v2(frame_data_ptr->connect_info_ptr->pNDI_recv, &frame_data_ptr->data.audio_frame);
    } else if (frame_data_ptr->frame_type == kNdiMetaData) {
        // Release metadata memory.
        NDIlib_recv_free_metadata(frame_data_ptr->connect_info_ptr->pNDI_recv, &frame_data_ptr->data.metadata);
    }
    // Put memory allocation used back into memory pool.
    CdiPoolPut(frame_data_ptr->connect_info_ptr->ndi_frame_data_pool_handle, frame_data_ptr);
}

CdiReturnStatus NdiConvertNdiToCdi(FrameData* frame_data_ptr, int* buffer_size_ptr, void** payload_buffer_ptr,
                                   int64_t* timestamp_ptr, CdiAvmBaselineConfig* avm_config_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (frame_data_ptr->frame_type == kNdiVideo) {
        // Convert video frame.
        rs = NdiToCdiVideo(frame_data_ptr, buffer_size_ptr, payload_buffer_ptr, timestamp_ptr, avm_config_ptr);
    } else if (frame_data_ptr->frame_type == kNdiAudio) {
        // Convert audio frame.
        rs = NdiToCdiAudio(frame_data_ptr, buffer_size_ptr, payload_buffer_ptr, timestamp_ptr, avm_config_ptr);
    } else if (frame_data_ptr->frame_type == kNdiMetaData) {
        // Convert metadata frame.
        rs = NdiToCdiMeta(frame_data_ptr, buffer_size_ptr, payload_buffer_ptr, timestamp_ptr, avm_config_ptr);
    }

    // Update error for NDI Thread return status if no preexisting error already determined.
    if (kCdiStatusOk != rs && kCdiStatusOk == frame_data_ptr->connect_info_ptr->ndi_thread_rs) {
        frame_data_ptr->connect_info_ptr->ndi_thread_rs = rs;
    }
    return rs;
}

CdiReturnStatus NdiConvertCdiToNdi(const CdiPtpTimestamp* cdi_timestamp_ptr, const CdiAvmBaselineConfig* avm_config_ptr,
                                   int payload_size, const CdiSgList* sgl_ptr, FrameData* frame_data_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    if (avm_config_ptr->payload_type == kCdiAvmVideo) {
        // Convert video frame.
        rs = CdiToNdiVideo(cdi_timestamp_ptr, avm_config_ptr, payload_size, sgl_ptr, frame_data_ptr);
    } else if (avm_config_ptr->payload_type == kCdiAvmAudio) {
        // Convert audio frame.
        rs = CdiToNdiAudio(cdi_timestamp_ptr, avm_config_ptr, payload_size, sgl_ptr, frame_data_ptr);
    } else if (avm_config_ptr->payload_type == kCdiAvmAncillary) {
        // Convert metadata frame.
        rs = CdiToNdiMetadata(cdi_timestamp_ptr, avm_config_ptr, payload_size, sgl_ptr, frame_data_ptr);
    }
    // Update error for NDI Thread return status if no preexisting error already determined.
    if (kCdiStatusOk != rs && kCdiStatusOk == frame_data_ptr->connect_info_ptr->ndi_thread_rs) {
        frame_data_ptr->connect_info_ptr->ndi_thread_rs = rs;
    }
    return rs;
}

CDI_THREAD NdiReceivePayloadThread(void* ptr)
{
    // Pointer to a structure holding all info related to a specific connection, including test settings, connection
    // configuration data from the SDK, and state information for the test connection.
    TestConnectionInfo* con_info_ptr = (TestConnectionInfo*)ptr;

    // Initialize thread return status.
    con_info_ptr->ndi_thread_rs = kCdiStatusOk;

    // Variable to hold last frame data pointer with video frame.
    FrameData* last_video_frame_data_ptr = NULL;

    // Expected video frame per second.
    // Used to determine when to resend last video frame if no new video frame seen in expected time.
    uint64_t video_frame_rate_us = 0; // Value is set from received video frames.

    // Used to calculate difference in timestamp for receding time clock.
    uint64_t last_video_frame_os_time_us = 0; // OS time of last video frame sent.
    int video_frame_repeat_counter = 0;
    uint64_t next_video_os_time_us = CdiOsGetMicroseconds() + DEFAULT_RECV_TIMEOUT_MS * 1000;

    while (con_info_ptr->ndi_thread_rs == kCdiStatusOk && !CdiOsSignalGet(con_info_ptr->ndi_thread_signal)) {
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_metadata_frame_t metadata_frame;
        FrameData* frame_data_ptr = NULL;
        bool reset_timeout = false;
        uint64_t current_os_time_us = CdiOsGetMicroseconds();
        uint32_t recv_timeout_ms = 0;
        if (next_video_os_time_us > current_os_time_us) {
            recv_timeout_ms = (next_video_os_time_us - current_os_time_us) / 1000; // Convert uS to mS
        }
#ifdef DISABLE_REPEAT_NDI_VIDEO_FRAME
        recv_timeout_ms = 1000;
#endif
        switch (NDIlib_recv_capture_v2(con_info_ptr->pNDI_recv, &video_frame, &audio_frame,
                                       &metadata_frame, recv_timeout_ms)) {
            // No data.
            case NDIlib_frame_type_none:
#ifndef DISABLE_REPEAT_NDI_VIDEO_FRAME
                // if have frame to repeat, repeat frame.
                if (last_video_frame_data_ptr != NULL) {
                    // Send last video frame if time left in expected time rate times out.
                    last_video_frame_data_ptr->ref_count++;
                    frame_data_ptr = last_video_frame_data_ptr;
                    // TODO: Determine if we need unique frame_data buffer to prevent thread sync issues with changing
                    // the timestamp here.
                    uint32_t frame_rate_ms = (video_frame.frame_rate_D * 1000) / video_frame.frame_rate_N;
                    frame_data_ptr->data.video_frame.timestamp += frame_rate_ms;
                }
                if (last_video_frame_os_time_us) {
                    video_frame_repeat_counter++;
                }
                reset_timeout = true;
#endif
                break;

            // Video data.
            case NDIlib_frame_type_video:
                last_video_frame_os_time_us = CdiOsGetMicroseconds(); // Update time video frame was received
                if (video_frame_repeat_counter) {
                    CDI_LOG_THREAD(kLogInfo, "Repeated a NDI video frame [%d] times.", video_frame_repeat_counter);
                }
                video_frame_repeat_counter = 0;

                // Get pointer to new FrameData buffer from memory pool.
                if (!CdiPoolGet(con_info_ptr->ndi_frame_data_pool_handle, (void**)&frame_data_ptr)) {
                    CDI_LOG_THREAD(kLogError, "Failed to Get Video Frame Pool Buffer.");
                    con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                    assert(false);
                } else {
                    frame_data_ptr->frame_type = kNdiVideo;
                    frame_data_ptr->data.video_frame = video_frame;
                    frame_data_ptr->ref_count = 2;
                }

                // Get frame rate from incoming NDI frame data.
                video_frame_rate_us = (video_frame.frame_rate_D * 1000000) / video_frame.frame_rate_N;

                // if have last video still, release last video frame so new video frame can take place.
                if (last_video_frame_data_ptr != NULL) {
                    assert(last_video_frame_data_ptr->ref_count > 0);
                    last_video_frame_data_ptr->ref_count--;
                    // if frame referenced no more, release payload.
                    if (last_video_frame_data_ptr->ref_count == 0) {
                        NdiReleasePayload(last_video_frame_data_ptr);
                    }
                }

                // Point to last video frame.
                last_video_frame_data_ptr = frame_data_ptr;
                break;

            // Audio data.
            case NDIlib_frame_type_audio:
                // Write Frame Data to memory pool and initialize values.
                if (!CdiPoolGet(con_info_ptr->ndi_frame_data_pool_handle, (void**)&frame_data_ptr)) {
                    CDI_LOG_THREAD(kLogError, "Failed to Get Audio Frame Pool Buffer.");
                    con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                    assert(false);
                } else {
                    frame_data_ptr->frame_type = kNdiAudio;
                    frame_data_ptr->data.audio_frame = audio_frame;
                }
                break;

            // Metadata.
            case NDIlib_frame_type_metadata:
                // Write Frame Data to memory pool and initialize values.
                if (!CdiPoolGet(con_info_ptr->ndi_frame_data_pool_handle, (void**)&frame_data_ptr)) {
                    CDI_LOG_THREAD(kLogError, "Failed to Get Metadata Frame Pool Buffer.");
                    con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                    assert(false);
                } else {
                    frame_data_ptr->frame_type = kNdiMetaData;
                    frame_data_ptr->data.metadata = metadata_frame;
                }
                // TODO: NDI does not have a standard for metadata frames that contains closed captions, subtitles, etc.
                // Need to translate NDI metadata to CDI.
                break;

            // Changed Status.
            case NDIlib_frame_type_status_change:
                CDI_LOG_THREAD(kLogInfo, "The device has changed status in some way.");
                break;

            // Error.
            default:
                // Error: stop thread, stop updating buffer.
                CDI_LOG_THREAD(kLogInfo, "NDI error returned from NDIlib_recv_capture_v2().");
                con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                break;
        }

        // TODO: This logic was re-written using OS time instead of NDI timestamps and needs to be tested/verified.
        // The existing repeating frame logic using NDI timestamps was not working correctly.
        if (reset_timeout) {
            if (0 == video_frame_repeat_counter) {
                next_video_os_time_us = current_os_time_us + (video_frame_rate_us * REPEAT_FRAME_TIMEOUT_FACTOR);
            } else {
                assert(0 != last_video_frame_os_time_us);
                next_video_os_time_us = last_video_frame_os_time_us + (video_frame_repeat_counter * video_frame_rate_us);
            }
        } else if (0 != last_video_frame_os_time_us) {
            next_video_os_time_us = last_video_frame_os_time_us + (video_frame_repeat_counter * video_frame_rate_us);
        } else {
            next_video_os_time_us = current_os_time_us + (video_frame_rate_us * REPEAT_FRAME_TIMEOUT_FACTOR);
        }

        if (kCdiStatusOk == con_info_ptr->ndi_thread_rs) {
            // if valid video, audio, or metadata frame, write Payload FIFO.
            if (frame_data_ptr) {
                frame_data_ptr->connect_info_ptr = con_info_ptr;

                // Convert audio from non-interleaved 32-bit float in Little-Endian to interleaved 24-bit int in
                // Big-Endian.
                if (frame_data_ptr->frame_type == kNdiAudio) {
                    if (!NdiToCdiAudioConversion(audio_frame.p_data, audio_frame.no_channels, audio_frame.no_samples,
                                                 audio_frame.channel_stride_in_bytes, &frame_data_ptr->p_data_size)) {
                        CDI_LOG_THREAD(kLogError, "Failed to Convert Audio to CDI format.");
                        con_info_ptr->ndi_thread_rs = kCdiStatusRxPayloadError;
                        assert(false);
                    }
                }

                // Write payload information to the Payload FIFO.
                if (kCdiStatusOk == con_info_ptr->ndi_thread_rs &&
                    !CdiFifoWrite(con_info_ptr->payload_fifo_handle, 1, NULL, &frame_data_ptr)) {
                    CDI_LOG_THREAD(kLogError, "Failed to write Payload FIFO.");
                    con_info_ptr->ndi_thread_rs = kCdiStatusFatal;
                    assert(false);
                }
            }

            // Read Callback FIFO.
            FrameData* user_data_callback_ptr = NULL;
            while (CdiFifoRead(con_info_ptr->callback_fifo_handle, 0, NULL, (void *)&user_data_callback_ptr)) {
                assert(user_data_callback_ptr != NULL);
                assert(user_data_callback_ptr->ref_count > 0);
                user_data_callback_ptr->ref_count--;
                // if frame referenced no more, release payload.
                if (user_data_callback_ptr->ref_count == 0) {
                    NdiReleasePayload(user_data_callback_ptr);
                }
            }
        }
    }

    // if NDI thread ending and repeated frame is not released, release frame.
    if (last_video_frame_data_ptr != NULL) {
        NdiReleasePayload(last_video_frame_data_ptr);
    }

    CDI_LOG_THREAD(kLogInfo, "NDI Receive thread is exiting.");
    return 0;
}

CDI_THREAD NdiTransmitPayloadThread(void* ptr)
{
    // Pointer to a structure holding all info related to a specific connection, including test settings, connection
    // configuration data from the SDK, and state information for the test connection.
    TestConnectionInfo* con_info_ptr = (TestConnectionInfo*)ptr;

    // Initialize thread return status.
    con_info_ptr->ndi_thread_rs = kCdiStatusOk;

    while (con_info_ptr->ndi_thread_rs == kCdiStatusOk && !CdiOsSignalGet(con_info_ptr->ndi_thread_signal)) {
        FrameData* frame_data_ptr = NULL;
        if (!CdiFifoRead(con_info_ptr->payload_fifo_handle, CDI_INFINITE, NULL, (void *)&frame_data_ptr)) {
            CDI_LOG_THREAD(kLogError, "Failed to read FIFO.");
            assert(false);
            break;
        }

        switch (frame_data_ptr->frame_type) {
            case kNdiVideo:
                NDIlib_send_send_video_v2(frame_data_ptr->connect_info_ptr->pNDI_send, &frame_data_ptr->data.video_frame);
            break;
            case kNdiAudio:
                NDIlib_send_send_audio_v2(frame_data_ptr->connect_info_ptr->pNDI_send, &frame_data_ptr->data.audio_frame);
                CdiPoolPut(con_info_ptr->ndi_audio_pool_handle, frame_data_ptr->data.audio_frame.p_data);
                frame_data_ptr->data.audio_frame.p_data = NULL;
            break;
            case kNdiMetaData:
                CDI_LOG_THREAD(kLogWarning, "Ignoring NDI metadata (not implemented to send).");
            break;
        }

        // Free CDI Rx payload buffer.
        CdiCoreRxFreeBuffer(&frame_data_ptr->rx_sgl);

        // Return frame data buffer to pool.
        CdiPoolPut(frame_data_ptr->connect_info_ptr->ndi_frame_data_pool_handle, frame_data_ptr);
    }

    CDI_LOG_THREAD(kLogInfo, "NDI transmit thread is exiting.");
    return 0;
}
