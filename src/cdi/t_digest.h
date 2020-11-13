// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * The declarations in this header file correspond to the definitions in t_digest.c.
 */
#ifndef T_DIGEST_H__
#define T_DIGEST_H__
#include <stdbool.h>

// The configuration.h file must be included first since it can have defines which affect subsequent files.
#include "configuration.h"

#include "cdi_logger_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/// @brief The maximum number of clusters allowed in a fully-merged digest.
#define MAX_MERGED_CLUSTERS (200)

/// @brief The maximum number of clusters allowed in the incoming sample buffer (i.e unmerged clusters).
#define MAX_UNMERGED_CLUSTERS (50)

/// @brief The maximum number of clusters allowed in this algorithm. This determines how many cluster structrues are
/// preallocated when the digest is created.
#define MAX_CLUSTERS (MAX_MERGED_CLUSTERS + MAX_UNMERGED_CLUSTERS)

/// @brief Macro to allow t-Digest logging to be turned on or off via define DEBUG_T_DIGEST_LOGGING.
#ifdef DEBUG_T_DIGEST_LOGGING
#define TDIGEST_LOG_THREAD(log_level, ...) CDI_LOG_THREAD(log_level, __VA_ARGS__);
#else
#define TDIGEST_LOG_THREAD(log_level, ...)
#endif

/// Opaque pointer for TDigest structure.
typedef struct TDigest* TDigestHandle;

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Function used to create a new t-digest.
 *
 * @param ret_td_handle_ptr Pointer for the handle for the new TDigest object.
 *
 * @return True if successful; false if not.
 */
bool TDigestCreate(TDigestHandle* ret_td_handle_ptr);

/**
 * @brief Function used to free t-digest memory.
 *
 * @param td_handle Handle for the TDigest object to use.
 */
void TDigestDestroy(TDigestHandle td_handle);

/**
 * @brief Function used to reset t-digest to begin collecting a new set of statistics. This function initializes all the
 * TDigest data members.
 *
 * @param td_handle Handle for the TDigest object to use.
 */
void TDigestClear(TDigestHandle td_handle);

/**
 * @brief Function used to create a new t-digest.
 *
 * @param td_handle Handle for the TDigest object to use.
 * @param value The value of the new sample to add to the digest.
 */
void TDigestAddSample(TDigestHandle td_handle, uint32_t value);

/**
 * @brief Function used to get the value at a given percentile.
 *
 * @param td_handle Handle for the TDigest object to use.
 * @param percentile The desired percentile between 0 and 1, inclusive.
 * @param value_at_percentile_ptr Pointer to the return value where resulting value of percentile search is stored.
 *
 * @return True if successful; false if not.
 */
bool TDigestGetPercentileValue(TDigestHandle td_handle, int percentile, uint32_t* value_at_percentile_ptr);

/**
 * @brief Function used to get the number of samples in the digest.
 *
 * @param td_handle Handle for the TDigest object to use.
 *
 * @return The number of samples in the digest.
 */
int TDigestGetCount(TDigestHandle td_handle);
#endif // T_DIGEST_H__
