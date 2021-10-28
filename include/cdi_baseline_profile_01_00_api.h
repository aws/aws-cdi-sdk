// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_BASELINE_PROFILE_01_00_API_H__
#define CDI_BASELINE_PROFILE_01_00_API_H__

/**
 * @file
 * @brief
 * This file contains declarations and definitions for the CDI AVM baseline profile API functions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cdi_avm_api.h"
#include "cdi_baseline_profile_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// @brief SMPTE 2110-20 Uncompressed video frame sample formats.
/// NOTE: Any changes made here MUST also be made to "video_sampling_key_array" in cdi_utility_api.c.
typedef enum {
    kCdiAvmVidYCbCr444, ///< @brief SMPTE 2110 4:4:4 YUV video sample format
    kCdiAvmVidYCbCr422, ///< @brief SMPTE 2110 4:2:2 YUV video sample format
    kCdiAvmVidRGB,      ///< @brief SMPTE 2110 RGB (linear) video sample format
} CdiAvmVideoSampling;

/// @brief Define the use of an alpha channel along with video data.
typedef enum {
    kCdiAvmAlphaUnused, ///< Alpha channel not being used.

    /**
     * @brief For every set of video sample pixels there is an alpha pixel of the same bit depth and format being sent.
     * For example if kCdiAvmVidRGBLinear is being used the payload has one red sample, one green sample, one blue
     * sample, and one alpha sample for every pixel of the frame.  For YCbCr colorspace there is one alpha
     * sample for every luma sample sent.
     */
    kCdiAvmAlphaUsed
} CdiAvmVideoAlphaChannel;

/// @brief SMPTE 2110-20 Uncompressed video frame bit depths.
/// NOTE: Any changes made here MUST also be made to "video_bit_depth_key_array" in cdi_utility_api.c.
typedef enum {
    kCdiAvmVidBitDepth8,   ///<  8 bit integer samples
    kCdiAvmVidBitDepth10,  ///< 10 bit integer samples
    kCdiAvmVidBitDepth12,  ///< 12 bit integer samples
} CdiAvmVideoBitDepth;

/// @brief SMPTE 2110-20 Uncompressed video frame colorimetry.
/// NOTE: Any changes made here MUST also be made to "colorimetry_key_array" in cdi_utility_api.c.
typedef enum {
    kCdiAvmVidColorimetryBT601,    ///< Recommendation ITU-R BT.601-7
    kCdiAvmVidColorimetryBT709,    ///< Recommendation ITU-R BT.709-6
    kCdiAvmVidColorimetryBT2020,   ///< Recommendation ITU-R BT.2020-2
    kCdiAvmVidColorimetryBT2100,   ///< Recommendation ITU-R BT.2100 Table 2 titled "System colorimetry"
    kCdiAvmVidColorimetryST2065_1, ///< SMPTE ST 2065-1 Academy Color Encoding Specification (ACES)
    kCdiAvmVidColorimetryST2065_3, ///< Academy Density Exchange Encoding (ADX) in SMPTE ST 2065-3
    kCdiAvmVidColorimetryXYZ,      ///< ISO 11664-1 section titled "1931 Observer"
} CdiAvmColorimetry;

/// @brief SMPTE 2110-20 Media type parameters for Transfer Characteristic System (TCS)
/// NOTE: Any changes made here MUST also be made to "tcs_key_array" in cdi_utility_api.c
typedef enum {
    kCdiAvmVidTcsSDR,           ///< Standard Dynamic Range video streams. Recommendation ITU-R BT.2020
    kCdiAvmVidTcsPQ,            ///< Perceptual Quantization (PQ) high dynamic range. Recommendation ITU-R BT.2100
    kCdiAvmVidTcsHLG,           ///< Hybrid Log-Gamma (HLG) high dynamic range. Recommendation ITU-R BT.2100
    kCdiAvmVidTcsLinear,        ///< Linear encoded floating point samples (dept=16f) all values fall in range 0.0 - 1.0
    kCdiAvmVidTcsBT2100LINPQ,   ///< PQ with floating point representation. Recommendation ITU-R BT.2100-0
    kCdiAvmVidTcsBT2100LINHLG,  ///< HLG with floating point representation. Recommendation ITU-R BT.2100-0
    kCdiAvmVidTcsST2065_1,      ///< Video stream of linear encoded floating point as specified in SMPTE ST 2065-1
    kCdiAvmVidTcsST428_1,       ///< Video stream using transfer characteristic specified in SMPTE ST 428-1
    kCdiAvmVidTcsDensity,       ///< Video streams of density encoded samples such as those defined in SMPTE ST 2065-3
} CdiAvmVideoTcs;

/// @brief SMPTE 2110-20 Media type parameter for setting encoding range
/// NOTE: Any change made here MUST also be made to "range_key_array" in cdi_utility_api.c
typedef enum {
    /// @brief When paired with ITU Rec BT.2100 sets values to ranges specified in table 9 of ITU Rec BT.2100. In any
    /// other context corresponds to ranges set in STMPE RP 2077.
    kCdiAvmVidRangeNarrow,
    /// @brief Invalid in the context of ITU Rec BT.2100. In other contexts corresponds to ranges set in STMPE RP 2077
    kCdiAvmVidRangeFullProtect,
    /// @brief When paired with ITU Rec BT.2100 sets values to ranges specified in table 9 of ITU Rec BT.2100. In any
    /// other context corresponds to ranges set in STMPE RP 2077.
    kCdiAvmVidRangeFull
} CdiAvmVideoRange;

/// @brief Video payload configuration data. Used to define the format of the video payload conforming to the CDI
/// baseline video profile.
typedef struct {
    /// @brief Baseline profile version. NOTE: Must be first element (see CdiAvmBaselineProfileVersion).
    CdiAvmBaselineProfileVersion version;
    uint16_t width;                        ///< Video frame width in pixels.
    uint16_t height;                       ///< Video frame height in pixels.
    CdiAvmVideoSampling sampling;          ///< Video frame sampling format.
    CdiAvmVideoAlphaChannel alpha_channel; ///< Alpha channel type.
    CdiAvmVideoBitDepth depth;             ///< Video frame bit depth.
    uint32_t frame_rate_num;               ///< Video frame rate numerator.
    uint32_t frame_rate_den;               ///< Video frame rate denominator.
    CdiAvmColorimetry colorimetry;         ///< Video frame colorimetry.
    bool interlace;                        ///< If set true indicates interlaced or Progressive segmented Frame (PsF).
    bool segmented;                        ///< If true indicates PsF. Invalid to set without setting interlace true.
    CdiAvmVideoTcs tcs;                    ///< Transfer Characteristic System used.
    CdiAvmVideoRange range;                ///< Signal encoding range of the sample values.

    /// @brief Pixel Aspect Ratio (PAR) width is the first of two integer values that make up PAR. PAR width and
    /// height should be the smallest integer values that create the correct PAR value.
    uint16_t par_width;

    /// @brief Pixel Aspect Ratio (PAR) height is the second of two integer values that make up PAR. PAR width and
    /// height should be the smallest integer values that create the correct PAR value.
    uint16_t par_height;

    /**
     * To specify sending partial frames use start_vertical_pos, start_horizontal_pos, vertical_size, and
     * horizontal_size to specify a rectangle being sent.  The start_vertical_pos and start_horizontal_pos specify the
     * zero based starting coordinates of the rectangle to be sent where (0,0) is the upper left corner of the
     * frame. The size of the rectangle is specified by vertical_size and horizontal_size where the rectangle is
     * vertical_size lines tall and each line is horizontal_size pixels long. Using values of zero for horizontal_size
     * or vertical_size indicates no horizontal and or vertical cropping is being performed. In this way if all of the
     * variables mentioned are set to 0 a full uncropped frame is indicated.
     *
     * @brief For transferring partial video frames in a payload, this value specifies the y-axis value of the first
     * line of video frame data (zero based). For transferring all lines of data in a frame the value must be zero.
     */
    uint16_t start_vertical_pos;

    /// @brief Number of video lines in payload. If zero, all lines of data in frame is assumed.
    /// @see start_vertical_pos
    uint16_t vertical_size;

    /// @brief For transferring partial video frames in a payload, this value specifies the x-axis value of the first
    /// pixel of video frame data (zero based). For transferring all pixels in a video line the value must be zero.
    /// @see start_vertical_pos
    uint16_t start_horizontal_pos;

    /// @brief Number of pixels per line in the payload. If zero, entire lines of pixels are assumed.
    /// @see start_vertical_pos
    uint16_t horizontal_size;
} CdiAvmVideoConfig;

/// @brief SMPTE 2110-30 Uncompressed audio channel groupings.
/// NOTE: Any changes made here MUST also be made to "audio_channel_grouping_key_array" in cdi_utility_api.c and
/// "channel_grouping_symbols" in baseline_profile.c.
typedef enum {
    kCdiAvmAudioM,        ///< Mono.
    kCdiAvmAudioDM,       ///< Dual mono (M1, M2).
    kCdiAvmAudioST,       ///< Standard stereo (left, right).
    kCdiAvmAudioLtRt,     ///< Matrix stereo (Left Total, Right Total).
    kCdiAvmAudio51,       ///< 5.1 Surround (L, R, C, LFE, Ls, Rs),
    kCdiAvmAudio71,       ///< 7.1 Surround (L, R, C, LFE, Lss, Rss, Lrs, Rrs).
    kCdiAvmAudio222,      ///< 22.2 Surround (SMPTE ST 2036-2, Table 1).
    kCdiAvmAudioSGRP,     ///< One SDI audio group (1, 2, 3, 4).
} CdiAvmAudioChannelGrouping;

/// @brief SMPTE 2110-30 Uncompressed audio sample rates.
/// NOTE: Any changes made here MUST also be made to "audio_sample_rate_key_array" in cdi_utility_api.c.
typedef enum {
    kCdiAvmAudioSampleRate48kHz, ///< 48 kHz audio sample rate.
    kCdiAvmAudioSampleRate96kHz, ///< 96 kHz audio sample rate.
} CdiAvmAudioSampleRate;

/// @brief Audio payload configuration data. Used to define the format of the audio payload conforming to the CDI
/// baseline audio format.
typedef struct {
    /// @brief Baseline profile version. NOTE: Must be first element (see CdiAvmBaselineProfileVersion).
    CdiAvmBaselineProfileVersion version;
    /// @brief The audio grouping concept comes from SMPTE ST 2110-30 specification, Section 6.2.2. It is intended for
    /// sending an entire multi-channel 2110-30 audio stream over a single CDI AVM audio stream.
    CdiAvmAudioChannelGrouping grouping;
    CdiAvmAudioSampleRate sample_rate_khz; ///< Audio sample rate in kHz.
    char language[3]; ///< Zero, two or three character language code padded with '\0' characters,
} CdiAvmAudioConfig;

/// @brief Ancillary Data payload configuration data. Used to define the format of the ancillary data payload conforming
/// to the CDI baseline ancillary data format.
typedef struct {
    /// @brief Baseline profile version. NOTE: Must be first element (see CdiAvmBaselineProfileVersion).
    CdiAvmBaselineProfileVersion version;
} CdiAvmAncillaryDataConfig;

/// @brief Structure that aggregates the audio, video, and ancillary data structures into a single structure.
struct CdiAvmBaselineConfig {
    ///< @brief Indicates which union member applies. NOTE: To maintain compatibility with future profile revisions,
    /// this must be the first element in this structure and be immediately followed by the union below. In addition,
    /// the first element of each union item must be CdiAvmBaselineProfileVersion. This confirms to future profiles
    /// which use a new structure type called CdiAvmBaselineConfigCommon.
    CdiBaselineAvmPayloadType payload_type;

    union {
        /// @brief Union of data determined by payload_type. NOTE: First element of each part of this union must be
        /// CdiAvmBaselineProfileVersion. See NOTE above.
        CdiAvmVideoConfig video_config;
        CdiAvmAudioConfig audio_config;
        CdiAvmAncillaryDataConfig ancillary_data_config;
    };
};

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#endif // CDI_BASELINE_PROFILE_01_00_API_H__
