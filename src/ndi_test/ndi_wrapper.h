// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in ndi_wrapper.c and ndi_test.c.
 */

#ifndef NDI_WRAPPER_H__
#define NDI_WRAPPER_H__

#include <stdbool.h>
#include <stdint.h>

#include "cdi_baseline_profile_02_00_api.h"
#include "cdi_log_api.h"
#include "cdi_os_api.h"
#include "fifo_api.h"
#include "cdi_pool_api.h"
#include <Processing.NDI.Lib.h>

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

typedef struct TestConnectionInfo TestConnectionInfo; ///< Forward reference
typedef struct TestSettings TestSettings; ///< Forward reference

/**
 * @brief A structure that labels the various NDI frames.
 *
 */
typedef enum {
    kNdiVideo = 0,      ///< Labels video frames as zero.
    kNdiAudio = 1,      ///< Labels audio frames as one.
    kNdiMetaData = 2,   ///< Labels metadata frames as two.
} NdiFrameType;

/**
 * @brief A structure for storing all info related to a NDI frame, and a specific connection,
 * including test settings, connection configuration data from the SDK, and state information for the test connection.
 *
 */
typedef struct {
    TestConnectionInfo* connect_info_ptr;       ///< Pointer to all info related to a specific connection.
    NdiFrameType frame_type;                    ///< NDI frame type.
    /// @brief Store of NDI media frame.
    union NdiDataType {
        NDIlib_video_frame_v2_t video_frame;    ///< NDI video frame.
        NDIlib_audio_frame_v2_t audio_frame;    ///< NDI audio frame.
        NDIlib_metadata_frame_t metadata;       ///< NDI metadata.
    } data;                                     ///< Store of NDI media frame.
    int p_data_size;                            ///< Calculated p_data size (if value is not zero).
    volatile uint32_t ref_count;                ///< Reference counter; if value == 0, can free frame memory.

     CdiSgList rx_sgl;                           ///< CDI Rx SGL
} FrameData;

/**
 * @brief A structure for storing breakdown of NDI timestamp in seconds, milliseconds, and nanoseconds.
 *
 */
typedef struct {
    uint32_t ndi_time_in_s;     ///< NDI time in seconds.
    int64_t ndi_time_in_ms;     ///< NDI time in milliseconds.
    uint32_t ndi_time_in_ns;    ///< NDI time in nanoseconds.
} NdiTime;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief A function which initializes the NDI SDK.
 *
 * @return true or false.
 */
bool NdiInitialize(void);

/**
 * @brief Show available NDI sources and exit.
 */
void NdiShowSources(void);

/**
 * @brief A function which creates a NDI finder, looks for NDI sources, creates a NDI receiver, and connects to a
 * NDI source to receive stream information.
 *
 * @param test_settings_ptr Pointer to Test Settings argument to find NDI source based from the command line, if given.
 *
 * @return Returns a NDI receiver that is connected to a NDI source.
 */
NDIlib_recv_instance_t NdiCreateReceiver(const TestSettings* test_settings_ptr);

/**
 * @brief A function which creates a NDI sender.
 *
 * @param test_settings_ptr Pointer to Test Settings argument to create NDI source based from the command line, if given.
 *
 * @return Returns a NDI sender.
 */
NDIlib_send_instance_t NdiCreateSender(const TestSettings* test_settings_ptr);

/**
 * @brief A function that releases the memory of a NDI video, audio, or metadata frame, and puts back memory pool
 * allocation.
 *
 * @param arg_ptr A pointer to an Frame Data structure that is to be released.
 */
void NdiReleasePayload(FrameData* arg_ptr);

/**
 * @brief A function that maps information of NDI frame to an AVM structure that is compatible with CDI.
 *
 * @param arg_ptr Frame Data pointer which stores NDI frame.
 * @param data_size_ptr Argument passed in to CDI send containing size of data.
 * @param data_ptr Argument passed in to CDI send containing data.
 * @param timestamp_ptr Argument passed in to CDI send containing timestamp of data.
 * @param avm_baseline_config_ptr Argument passed in to CDI send containing configuration of data.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus NdiConvertNdiToCdi(FrameData* arg_ptr, int* data_size_ptr, void** data_ptr, int64_t* timestamp_ptr,
                                   CdiAvmBaselineConfig* avm_baseline_config_ptr);

/**
 * @brief Convert CDI payload to NDI frame.
 *
 * @return A value from the CdiReturnStatus enumeration.
 */
CdiReturnStatus NdiConvertCdiToNdi(const CdiPtpTimestamp* cdi_timestamp_ptr, const CdiAvmBaselineConfig* avm_config_ptr,
                                   int payload_size, const CdiSgList* sgl_ptr, FrameData* frame_data_ptr);

/**
 * @brief A function that captures NDI frames that is to be written to the memory pool and put in the Payload FIFO.
 * Function also reads from Callback FIFO to determine if NDI frame memory should be released or resent. Function
 * uses a receding time clock element to determine if video frames are being sent consistently at the expected time.
 * If video frame is not seen in expected time, function resends last seen video frame, else it keeps sending
 * new video frames.
 *
 * @param ndi_thread_data_ptr Contains necessary and unchanged data like Test Connections Information.
 *
 * @return Always returns 0.
 */
CDI_THREAD NdiReceivePayloadThread(void* ndi_thread_data_ptr);

/**
 * @brief Thread used to transmit NDI frames.
 *
 * @param ndi_thread_data_ptr Pointer to NDI thread data.
 *
 * @return Always returns 0.
 */
CDI_THREAD NdiTransmitPayloadThread(void* ndi_thread_data_ptr);

/**
 * @brief A function that breaks down the NDI Timestamp, in 100ns format, into seconds, milliseconds, and nanoseconds.
 *
 * @param ndi_timestamp NDI timestamp.
 *
 * @return Returns a structure containing breakdown of NDI timestamp in seconds, milliseconds, and nanoseconds.
 */
NdiTime NdiTimeBreakdown(int64_t ndi_timestamp);

#endif // NDI_WRAPPER_H__
