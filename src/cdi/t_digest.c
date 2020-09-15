// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * An implementation of the t-digest percentile estimation algorithm developed by Ted Dunning and Otmar Ertl and
 * available here: https://github.com/tdunning/t-digest/blob/master/docs/t-digest-paper/histo.pdf
 *
 * This algorithm gathers samples of a given metric and stores them in clusters of samples such that each cluster
 * contains a mean and sample count and no other information. As clusters grow and the number of clusters grows,
 * clusters can be combined or created in order to meet algorithm requirements for the max number of clusters and
 * cluster weight (number of samples per cluster). Clusters near the edges of the distribution of
 * samples are scaled such that they contain less samples and clusters near the center of the distribution are scaled
 * such that they contain more samples. Such scaling has the effect of keeping estimation error low. (Nearly) exact
 * values for a given percentile can be calculated from this set of clusters by interpolating between cluster means.
 *
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.
#include "t_digest.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "logger_api.h"
#include "cdi_os_api.h"
#include "utilities_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************
/// @brief The maximum value for uint32_t type. Used to initialize the mimimum sample value in a digest.
#define MAX_POSSIBLE_SAMPLE_VALUE (UINT32_MAX)

/// @brief The amount of the distribution tail to force to be single-sample clusters.
#define TAIL_PERCENT_FOR_SINGLE_SAMPLE (2)

/// @brief The amount of times to retry merging before giving up.
#define MAX_FAILED_MERGE_COUNT (5)


/**
 * @brief This data structure represents a cluster. Each cluster in the t-digest contains a mean and a weight (number
 * of samples).
 */
typedef struct {
    /// @brief The average value of samples in this cluster. This is a cached value that is updated whenever clusters
    /// are merged. Mean is used during the sorting of clusters and this cached value greatly improves sort speed.
    uint32_t mean;

    uint64_t sum; ///< The sum of all values in this cluster.
    int samples; ///< The number of samples in this cluster.
} Cluster;

/**
 * @brief The main structure of the t-digest.
 */
typedef struct {

     uint32_t max_sample_value; ///< The maximum sample value. Only used in the last cluster.
     uint32_t min_sample_value; ///< The minimum sample value. Only used in the first cluster.

     int total_samples;  ///< The total number of samples in the digest. This is the sum of all cluster weights.
     int total_clusters; ///< The total number of clusters that have been created.

     bool fully_merged; ///< True if the digest is fully merged; false if it is not.
     int failed_count; ///< Counter for the number of consecutive failed merges.

     Cluster clusters[MAX_CLUSTERS]; ///< Array of all clusters in the t-digest.
} TDigest;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************
#ifdef DEBUG_T_DIGEST_ARRAYS
/**
 * @brief Function used to pretty print a t-Digest.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 */
static void TDigestPrint(TDigest* td_ptr)
{
    printf("==== TDigest State ====");
    printf("Max Value: %u", td_ptr->max_sample_value);
    printf("Min Value: %u", td_ptr->min_sample_value);
    printf("Samples: %d", td_ptr->total_samples);
    printf("Clusters: %d [", td_ptr->total_clusters);
    for (int i=0; i<td_ptr->total_clusters; i++) {
        printf("(%d, %lu, %u, %d)", i, td_ptr->clusters[i].sum,
               (uint32_t)(td_ptr->clusters[i].sum / td_ptr->clusters[i].samples), td_ptr->clusters[i].samples);
        if (i == (td_ptr->total_clusters-1)) {
            printf("]\n\n");
        } else {
            printf(", ");
        }
    }
}
#endif

/**
 * @brief Function used to compare two nodes by looking at their mean values. This is an input to the qsort function.
 *
 * @param operand1_ptr Pointer to the first cluster to compare.
 * @param operand2_ptr Pointer to the second cluster to compare.
 */
static int TDigestClusterCompare(const void* operand1_ptr, const void* operand2_ptr)
{
    Cluster* cluster1_ptr = (Cluster*)(operand1_ptr);
    Cluster* cluster2_ptr = (Cluster*)(operand2_ptr);
    if (cluster1_ptr->mean < cluster2_ptr->mean) {
        return -1;
    } else if (cluster1_ptr->mean > cluster2_ptr->mean) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Function used to interpolate from input_sample1 to input_sample2 by sample_index/num_samples_of_interest.
 *
 * @param input_sample1 The starting sample point for the interpolation.
 * @param input_sample2 The end sample point for the interpolation.
 * @param sample_index The numerator of the interpolation ratio (sample_index out of total_interpolation_points).
 * @param total_interpolation_points The denominator of the interpolation ratio (sample_index out of
 * total_interpolation_points).
 *
 * @return The resulting value from the interpolation.
 */
static uint32_t TDigestInterpolate(const uint32_t input_sample1, const uint32_t input_sample2, int sample_index,
                                   int total_interpolation_points)
{
    uint32_t result;
    // Simple interpolation between the means of the two adjacent clusters based on the expected index of the
    // desired sample.
    uint32_t mean_delta = input_sample2 - input_sample1;
    if (mean_delta == 0) {
        result = input_sample1;
    } else {
        uint32_t left_mean_addition_num = (mean_delta * sample_index);
        uint32_t left_mean_addition = left_mean_addition_num / total_interpolation_points +
                                      ((left_mean_addition_num % total_interpolation_points != 0) ? 1 : 0);
        result = input_sample1 + left_mean_addition;
    }
    return result;
}

/**
 * @brief Function used to sort a TDigest structure.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 */
static void TDigestSort(TDigest* td_ptr)
{
    qsort(td_ptr->clusters, td_ptr->total_clusters, sizeof(Cluster), &TDigestClusterCompare);
}

/**
 * @brief Function used to find the cluster index where a given sample number resides. This function walks through all
 * clusters counting samples in each one until it finds the cluster with the requested sample number.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 * @param desired_sample The sample number for which we want to find the host cluster.
 * @param total_samples_ptr Pointer to the total number of samples in all clusters up to and including the cluster
 * selected by this function.
 *
 * @return The zero-based cluster index that is host to the desired sample.
 */
static int TDigestFindCluster(const TDigest* td_ptr, int desired_sample, int* total_samples_ptr)
{
    // Search through the clusters until we find the one that contains our desired sample.
    int i;
    for (i = 0; i < td_ptr->total_clusters; i++) {
        int next_total = *total_samples_ptr + td_ptr->clusters[i].samples;
        if (next_total >= desired_sample) {
            TDIGEST_LOG_THREAD(kLogInfo, "Found sample at cluster[%d], which has [%d] samples.", i,
                           td_ptr->clusters[i].samples);
            break;
        }
        *total_samples_ptr = next_total;
    }
    return i;
}

/**
 * @brief Function used to figure out if a proposed max percentile for a cluster is under the percentile limit for that
 * cluster.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 * @param cluster_index The current cluster index.
 * @return The maximum samples allowed for the given cluster index.
 */
static int TDigestGetClusterLimit(const TDigest* td_ptr, int cluster_index)

    // The above-referenced whitepaper indicates that "It is also possible to avoid evaluation of k and k^−1 by
    // estimating the maximum number of samples that can be in each candidate cluster directly from q. Such estimates
    // typically under-estimate the number of samples allowed, especially near the tails, but the size of the t-digest
    // is not substantially increased and accuracy can be somewhat increased."  We therefore take this approach and
    // avoid the high-cycle-cost of calculating sin or log functions for every single new cluster. The method being
    // used here is a simple triangle distribution, where the center clusters are allowed to have the most samples, and
    // the end clusters are allowed to have the least. The slope on either side of the center cluster is constant,
    // forming a triangle.
    //         ^                H = total samples at middle cluster
    //        /|\               B = max clusters
    //       / | \  H           S = total samples across all clusters
    //      /__+__\             b = current cluster index: 0 .. (B-1)
    //         B                h = sample limit at current cluster
    //
    //   1/2 * B * H = S   ==>    H = 2*S/B
    //   The slope of the triangle from 0 to B/2 is m = h/b = H/(B/2) = 2*H/B
    //   Note the slope is negative after B/2.
    //   Therefore, for any cluster b:
    //
    //     h = 4*S/(B^2) * {b     if b <  B/2
    //                     {(B-b) if b >= B/2
    //
    // Note that this algorithm gives some special treatment to the tails of the distribution by forcing 2% of the tail
    // samples to live in a cluster of 1. This greatly improves accuracy for percentile measurements close to 0 or 100.
    //
    // Also, if we encounter a situation where all clusters are already in use, then the ability to merge can sometimes
    // be limited if many clusters are already at or beyond 50% full. For example, if we have the following cluster
    // sizes in adjacent clusters where the cluster size limit is 10, then no merging will occur:
    //    { ... 5, 6, 5, 7, 5, ...}
    // To remedy this situation, every time a failed merge attempt occurs, we become more aggressive about allowing
    // merging by allowing increased cluster sizes. To do this we simply use the failed_count member of TDigest as a
    // cluster size multiplier.
{
    // NOTE: In this function we are using the current cluster index and MAX_MERGED_CLUSTERS to compute our location
    // within the distribution, but after a merge we may not have MAX_MERGED_CLUSTERS clusters. This can result in end
    // clusters being too full, reducing accuracy at the high end of the distribution. Logic could be improved if we use
    // 'q' (percentage of the way through the distribution) instead.

    // Multiply by 1 for first merge attempt, then get more aggressive the more tries we have done.
    int factor_multiplier = td_ptr->failed_count + 1;
    int factor_num = 4 * td_ptr->total_samples * factor_multiplier;
    int factor_den = MAX_MERGED_CLUSTERS * MAX_MERGED_CLUSTERS;
    int cluster_limit = 1;

    // Find the maximum number of samples for this cluster index.
    int cluster_limit_num = 0;
    if (cluster_index < MAX_MERGED_CLUSTERS / 2) {
        cluster_limit_num = factor_num * (cluster_index + 1); // +1 to convert from zero-based.
    } else {
        cluster_limit_num = factor_num * (MAX_MERGED_CLUSTERS - cluster_index);
    }
    cluster_limit = CDI_MAX(cluster_limit_num / factor_den, 1);

    // Keep the tails (+/-2%) limited to 1 sample, but for all others, allow 2 extra samples.
    // Also, force max to 1 until all clusters have been used. Then we free up the clusters to allow more samples.
    int tail_limit = (TAIL_PERCENT_FOR_SINGLE_SAMPLE * MAX_MERGED_CLUSTERS / 100);
    int abs_cluster_position_from_center = abs(cluster_index - (MAX_MERGED_CLUSTERS / 2));
    int abs_cluster_position_from_tail = MAX_MERGED_CLUSTERS / 2 - abs_cluster_position_from_center;
    if (abs_cluster_position_from_tail < tail_limit) {
        return 1;
    } else {
        return cluster_limit;
    }
}

/**
 * @brief Function used to merge all clusters of the t-digest. This function follows Algorithm 1 from the
 * above-referenced white paper by Dunning and Ertl, which provides a means to merge a t-digest with a list of
 * additional samples.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 *
 * @return True if merge was successful; false if not.
 */
static bool TDigestMerge(TDigest* td_ptr)
{
    bool ret = true;
    // We keep trying the merge until we are successful or until we reach the maximum retry count. Each time we retry
    // merging we become more generous about how many samples we allow each cluster to contain in an effort to make
    // merging easier. See TDigestGetClusterLimit() for more details. Don't do anything if there aren't any clusters.
    while (td_ptr->total_clusters > 0 && !td_ptr->fully_merged && td_ptr->failed_count <= MAX_FAILED_MERGE_COUNT) {
#ifdef DEBUG_T_DIGEST_ARRAYS
            TDIGEST_LOG_THREAD(kLogInfo, "Unmerged Digest");
            TDigestPrint(td_ptr);
#endif

        // Sort all merged and non-merged clusters by mean. Only sort on the first merge try because we know we
        // are already merged on subsequent attempts.
        if (td_ptr->failed_count == 0) {
            TDigestSort(td_ptr);
        }

        int max_cluster_samples = 0;  // The allowable samples for a given cluster.
        int accumulated_samples = 0;  // The total accumulated samples so far.
#ifdef DEBUG_T_DIGEST_ARRAYS
        TDIGEST_LOG_THREAD(kLogInfo, "Sorted Digest");
        TDigestPrint(td_ptr);
        printf("Clusters size limits: [");
        for (int i = 0; i < MAX_MERGED_CLUSTERS; i++) {
            int max_cluster_samples = TDigestGetClusterLimit(td_ptr, i);
            if (i != MAX_MERGED_CLUSTERS-1) {
                printf("%d, ", max_cluster_samples);
            } else{
                printf("%d]\n", max_cluster_samples);
            }
        }
#endif

        // Initialize max_cluster_samples for the first cluster.
        int cluster_index = 0; // Cluster index points to the current new cluster being built.
        max_cluster_samples = TDigestGetClusterLimit(td_ptr, cluster_index);

        // Loop through all clusters and rebuild the cluster array by combining as many input clusters as possible
        // for each output cluster. The number of output clusters will be less than or equal to the number of input
        // clusters, so we just overwrite input clusters as we create new output clusters. Note that we skip the
        // first cluster in the looping since it is our starting output cluster.
        /// NOTE: We choose to always loop forward to simplify the algorithm, but the white paper
        /// referenced above discusses a potential improvement to error rates near q=0 if looping is alternated
        /// between forward and reverse from merge to merge.
        for (int i = 1; i < td_ptr->total_clusters; i++) {
            int proposed_cluster_samples = td_ptr->clusters[cluster_index].samples + td_ptr->clusters[i].samples;
            TDIGEST_LOG_THREAD(kLogInfo, "The current new cluster[%d] already has [%d] samples and a limit of [%d].",
                           cluster_index, td_ptr->clusters[cluster_index].samples, max_cluster_samples);
            TDIGEST_LOG_THREAD(kLogInfo, "Proposing adding[%d] samples more from cluster [%d].",
                           td_ptr->clusters[i].samples, i);
            TDIGEST_LOG_THREAD(kLogInfo, "Cluster would hold samples[%d] through[%d] of[%d], or[%d] percent of total.",
                               accumulated_samples, accumulated_samples + td_ptr->clusters[i].samples,
                               td_ptr->total_samples, accumulated_samples * 100 / td_ptr->total_samples);
            if (proposed_cluster_samples <= max_cluster_samples) {
                TDIGEST_LOG_THREAD(kLogInfo, "Adding cluster[%d] to new cluster[%d].", i, cluster_index);
                // Basically, merge this old cluster into the current new cluster by adding the old count to the new
                // count and recalculating a mean based on the means and counts of the two clusters.
                td_ptr->clusters[cluster_index].samples += td_ptr->clusters[i].samples;
                td_ptr->clusters[cluster_index].sum += td_ptr->clusters[i].sum;
                td_ptr->clusters[cluster_index].mean = td_ptr->clusters[cluster_index].sum /
                                                       td_ptr->clusters[cluster_index].samples;
            } else { // Otherwise, we should create a new cluster from this input cluster.
                accumulated_samples += td_ptr->clusters[cluster_index].samples;
                // Increment the new cluster index and assign the current input cluster to this new output cluster.
                cluster_index++;
                TDIGEST_LOG_THREAD(kLogInfo, "Creating new cluster[%d].", cluster_index);
                td_ptr->clusters[cluster_index] = td_ptr->clusters[i];
                // Now calculate our new percentile position and find the percentile cluster for this cluster.
                max_cluster_samples = TDigestGetClusterLimit(td_ptr, cluster_index);

            }
            TDIGEST_LOG_THREAD(kLogInfo, "Cluster[%d] now has num samples[%d], sum[%lu], and mean[%d].", cluster_index,
                           td_ptr->clusters[cluster_index].samples, td_ptr->clusters[cluster_index].sum,
                           td_ptr->clusters[cluster_index].sum / td_ptr->clusters[cluster_index].samples);

        }
        if ((cluster_index + 1) > MAX_MERGED_CLUSTERS) {
            TDIGEST_LOG_THREAD(kLogInfo, "Digest was not fully merged - attempt[%d].", td_ptr->failed_count);
            ret = false;
            td_ptr->fully_merged = false;
            td_ptr->failed_count++;
        } else {
            TDIGEST_LOG_THREAD(kLogInfo, "Merged Digest in [%d] attempts.", td_ptr->failed_count + 1);
            ret = true;
            td_ptr->fully_merged = true;
            td_ptr->failed_count = 0;
        }
        td_ptr->total_clusters = cluster_index + 1;
#ifdef DEBUG_T_DIGEST_ARRAYS
        TDIGEST_LOG_THREAD(kLogInfo, "Merged Digest");
        TDigestPrint(td_ptr);
#endif
    };
    return ret;
}

/**
 * @brief Function to run the calculation for a percentile value.
 *
 * @param td_ptr Pointer to the TDigest object to use.
 * @param percentile Integer representing the percentile being calculated.
 *
 * @return The calculated value at the given percentile.
 */
static uint32_t TDigestCalculatePercentile(TDigest* td_ptr, int percentile)
{
    uint32_t value_at_percentile = 0;
    TDIGEST_LOG_THREAD(kLogInfo, "Scanning over [%d] clusters for percentile[%d].", td_ptr->total_clusters,
                       percentile);
    bool add_one = ((percentile * td_ptr->total_samples) % 100) != 0; // ceiling function
    int desired_sample = (percentile * td_ptr->total_samples / 100) + (add_one ? 1 : 0);
    int total_samples = 0;
    int cluster_index = TDigestFindCluster(td_ptr, desired_sample, &total_samples);

    // Cluster found. Now interpolate.
    // If our sample is below the mean of the cluster that was found, then we interpolate between this mean and the
    // mean of the cluster before it. If our sample is above this cluster's mean, then we interpolate between the
    // mean of this cluster and the cluster after it. If the sample is in an end cluster and is on the side of the
    // mean closest to the end, then we simply use the max/min value that was saved with the digest.
    // To interpolate, find the delta between mean of this cluster and the chosen neighbor cluster, and then find
    // the offset of the desired sample between those means. Then, simply use that ratio to find out how much to
    // add or subtract to the mean of the cluster hosting our desired sample. If the chosen cluster is at the edge
    // of the distribution (i.e. the first or last cluster) and our desired sample is on the half of that cluster
    // nearest the tail, we use the max or min as the neighbor value for the interpolation.
    // NOTE: This all works under the assumption that samples are more or less evenly distributed around the mean...
    // hint, this is where error gets introduced. This is explained thoroughly in the white paper mentioned above.

    Cluster* this_cluster_ptr = &td_ptr->clusters[cluster_index];
    uint32_t left_mean = 0;
    uint32_t right_mean = 0;
    int num_samples_of_interest = 0;
    int sample_index = 0;
    bool is_odd = (this_cluster_ptr->samples % 2) == 1;
    bool lower_half = desired_sample <= (total_samples + ((this_cluster_ptr->samples + 1) / 2));
    bool first_cluster = (cluster_index == 0);
    TDIGEST_LOG_THREAD(kLogInfo, "The desired sample [%d/%d] for percentile[%d] is in the %s half of cluster[%d].",
                   desired_sample, td_ptr->total_samples, percentile, lower_half ? "lower" : "upper",
                   cluster_index);

    if ((this_cluster_ptr->samples == 1)
        || (is_odd && (desired_sample == (total_samples + (this_cluster_ptr->samples + 1) / 2)))) {
        // If we're pointing right at the mean then use it. For example, say we have 7 samples in clusters before
        // this one and we have 3 samples in this cluster and our desired sample is 9.0. If this is true then we
        // will get 7+(3+1)/2 = 9. This only works for odd-sized clusters. While it is possible to have an even
        // number of samples average out to the mean, our algorithm doesn't allow us to think of an even-sized
        // cluster as having any samples exactly at the mean value. Also, if the current cluster only has one
        // sample, then we know what the value is supposed to be, so use the mean.
        TDIGEST_LOG_THREAD(kLogInfo, "Selecting the cluster's mean sample[%u].", this_cluster_ptr->mean);
        value_at_percentile = this_cluster_ptr->mean;
    } else if (lower_half) { // The sample is in lower half of the cluster.
        // If the desired sample is in the lower half of the cluster, then we are interpolating between this one
        // and the previous one.
        if (first_cluster) {
            if (this_cluster_ptr->samples < 3) {
                // If this is the first cluster, and the number of samples is below 3, we can assume the one to the
                // left of center is the t-digest's min_sample_value.
                TDIGEST_LOG_THREAD(kLogInfo, "Selecting the digest's minimum sample.");
                value_at_percentile = td_ptr->min_sample_value;
            } else {
                // If this is the first cluster, and the number of samples is more than 2, then we interpolate as
                // usual, but use the the min_sample_value as the left mean.
                left_mean = td_ptr->min_sample_value;
                right_mean = td_ptr->clusters[cluster_index].mean;
                num_samples_of_interest = td_ptr->clusters[cluster_index].samples / 2;
                sample_index = desired_sample;
                TDIGEST_LOG_THREAD(kLogInfo, "Interpolating to find sample[%d] over [%d] samples between[%u] "
                                   "and[%u].", sample_index, num_samples_of_interest, left_mean, right_mean);
            }
        } else {
            num_samples_of_interest = (td_ptr->clusters[cluster_index-1].samples + this_cluster_ptr->samples) / 2;
            left_mean = td_ptr->clusters[cluster_index-1].mean;
            right_mean = this_cluster_ptr->mean;
            sample_index = desired_sample - (total_samples - td_ptr->clusters[cluster_index-1].samples / 2);
            TDIGEST_LOG_THREAD(kLogInfo, "Interpolating to find sample[%d] over [%d] samples between[%u] "
                               "and[%u].", sample_index, num_samples_of_interest, left_mean, right_mean);
        }
    } else { // The sample is in the upper half of the cluster.
        // If the desired sample is in the upper half of the cluster, then we are interpolating between this one
        // and the next one.
        if ((cluster_index + 1) == td_ptr->total_clusters) { // Last cluster.
            TDIGEST_LOG_THREAD(kLogInfo, "Sample is in the last cluster.");
            if (this_cluster_ptr->samples < 3) {
                // If this is the last cluster, and the number of samples is below 3, we can assume the one to the
                // right of center is the t-digest's max_sample_value.
                TDIGEST_LOG_THREAD(kLogInfo, "Selecting the digest's maximum sample.");
                value_at_percentile = td_ptr->max_sample_value;
            } else {
                // If this is the last cluster, and the number of samples is more than 2, then we interpolate as
                // usual, but use the the min_sample_value as the left mean.
                left_mean = this_cluster_ptr->mean;
                right_mean = td_ptr->max_sample_value;
                num_samples_of_interest = td_ptr->clusters[cluster_index].samples / 2;
                sample_index = desired_sample - (total_samples + td_ptr->clusters[cluster_index].samples / 2);
                TDIGEST_LOG_THREAD(kLogInfo, "Interpolating to find sample[%d] over [%d] samples between[%u] "
                                   "and[%u].", sample_index, num_samples_of_interest, left_mean, right_mean);
            }
        } else { // Not the last cluster.
            num_samples_of_interest = (td_ptr->clusters[cluster_index+1].samples + this_cluster_ptr->samples) / 2;
            left_mean = this_cluster_ptr->mean;
            right_mean = td_ptr->clusters[cluster_index+1].mean;
            sample_index = desired_sample - (total_samples + td_ptr->clusters[cluster_index].samples / 2);
            TDIGEST_LOG_THREAD(kLogInfo, "Interpolating to find sample[%d] over [%d] samples between[%u] "
                               "and [%u].", sample_index, num_samples_of_interest, left_mean, right_mean);
        }
    }

    // For some simple cases, the value_at_percentile_ptr has already been been stored. For the rest, do it here.
    // Also, never divide by zero!  Both cases are covered by the num_samples_of_interest check, since that
    // variable is set above only when value_at_percentile_ptr is not already set.
    if (num_samples_of_interest != 0) {
        value_at_percentile = TDigestInterpolate(left_mean, right_mean, sample_index, num_samples_of_interest);
    }

    // Make sure we don't go beyond the min or max.
    if (value_at_percentile < td_ptr->min_sample_value) {
        value_at_percentile = td_ptr->min_sample_value;
    } else if (value_at_percentile > td_ptr->max_sample_value) {
        value_at_percentile = td_ptr->max_sample_value;
    }
    return value_at_percentile;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

bool TDigestCreate(TDigestHandle* ret_td_handle_ptr)
{
    TDigest* td_temp_ptr;
    td_temp_ptr = CdiOsMemAllocZero(sizeof(TDigest));
    if (NULL == td_temp_ptr) {
        return false;
    }
    TDigestClear((TDigestHandle)(td_temp_ptr));
    *ret_td_handle_ptr = (TDigestHandle)td_temp_ptr;
    return true;
}

void TDigestDestroy(TDigestHandle td_handle)
{
    if (td_handle) {
        CdiOsMemFree(td_handle);
    }
}

void TDigestClear(TDigestHandle td_handle)
{
    if (td_handle) {
        TDigest* td_ptr = (TDigest*)td_handle;
        td_ptr->min_sample_value = MAX_POSSIBLE_SAMPLE_VALUE;
        td_ptr->max_sample_value = 0;
        td_ptr->total_samples = 0;
        td_ptr->total_clusters = 0;
        td_ptr->fully_merged = true;
        td_ptr->failed_count = 0;
    }
}

void TDigestAddSample(TDigestHandle td_handle, uint32_t value)
{
    if (td_handle) {
        TDigest* td_ptr = (TDigest*)td_handle;
        TDIGEST_LOG_THREAD(kLogInfo, "Adding new value[%lu] to digest.", value);
        // Can't add samples if there are no unused clusters available. This should never happen unless a merge has
        // failed.
        if (td_ptr->total_clusters < MAX_CLUSTERS) {
            td_ptr->clusters[td_ptr->total_clusters] = (Cluster) {
                 .mean = value,
                 .sum = value,
                 .samples = 1,
            };
            // Check max and min values and update digest accordingly.
            if (value > td_ptr->max_sample_value) {
                TDIGEST_LOG_THREAD(kLogInfo, "Found new digest maximum[%lu].", value);
                td_ptr->max_sample_value = value;
            }
            if (value < td_ptr->min_sample_value) {
                TDIGEST_LOG_THREAD(kLogInfo, "Found new digest minimum[%lu].", value);
                td_ptr->min_sample_value = value;
            }
            td_ptr->total_clusters++;
            td_ptr->total_samples++;
            td_ptr->fully_merged = false;
        } else {
            TDIGEST_LOG_THREAD(kLogFatal, "Failed to add new value[%lu] to digest because there's no more space.",
                               value);
        }

        // If we have now used all clusters, merge what can be merged to make space for more samples.
        if (td_ptr->total_clusters >= MAX_CLUSTERS) {
            if (!TDigestMerge(td_ptr)) {
                TDIGEST_LOG_THREAD(kLogFatal, "Failed to merge digest.");
            }
        }
    }
}

bool TDigestGetPercentileValue(TDigestHandle td_handle, int percentile, uint32_t* value_at_percentile_ptr)
{
    if (NULL == td_handle) {
        return false;
    }

    TDigest* td_ptr = (TDigest*)td_handle;
    bool return_val = true;
    // Make sure the t-digest is fully merged before proceding. If it is not fully merged, then there has been at least
    // one single-sample cluster added to the end of the clusters array.
    if (!td_ptr->fully_merged) {
        if (!TDigestMerge(td_ptr)) {
            TDIGEST_LOG_THREAD(kLogFatal, "Failed to merge digest.");
        }
    }

    if (td_ptr->total_clusters == 0) {
        // If no clusters have been added yet, then we have nothing to compute and no valid value to return.
        return_val = false;
    } else if (percentile == 0) {
        // Skip computation and just return the minimum sample.
        *value_at_percentile_ptr = td_ptr->min_sample_value;
    } else if (percentile == 100) {
        // Skip computation and just return the maximum sample.
        *value_at_percentile_ptr = td_ptr->max_sample_value;
    } else if (percentile < 0 || percentile > 100) {
        // Don't do anything if an illegal percentile value is passed in (percentile < 0 or percentile > 100).
        CDI_LOG_THREAD(kLogError, "Illegal percentile request[%d]. Valid requests are between 0 and 100.", percentile);
        return_val = false;
    } else {
        // Compute the percentile value.
        *value_at_percentile_ptr = TDigestCalculatePercentile(td_ptr, percentile);
    }
    return return_val;
}

int TDigestGetCount(TDigestHandle td_handle)
{
    TDigest* td_ptr = (TDigest*)td_handle;
    return td_ptr->total_samples;
}