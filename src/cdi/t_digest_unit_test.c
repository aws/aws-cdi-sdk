// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and implementation of various unit tests for checking the functionality of the
 * t_digest.c module.
 */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "configuration.h"
#include "logger_api.h"
#include "cdi_os_api.h"
#include "utilities_api.h"
#include "t_digest.h"

#ifdef DEBUG_T_DIGEST_UNIT_TEST

/// @brief Define for the number of samples to run in the TestUniformRand test
#define TEST_URAND_SAMPLES (300000)
/// @brief Define for the number of samples to run in the TestSkewedRand test.
#define TEST_SRAND_SAMPLES (300)
/// @brief Define for the number of samples to run in the TestRunTime test.
#define TEST_RUNTIME_SAMPLES (200000000)

/// @brief Define for the number of percentiles to check in this test.
#define TEST_NUM_PERCENTILES (14)

/// @brief Macro used to do a boolean test and return a message if the test fails.
#define COMPARE_RETURN_MSG(message, test) do { if (!(test)) return message; } while (0)

/// @brief Macro used to run a test function that returns a message; this macro returns that message if it is not NULL.
#define RUN_TEST(test, run_flag) do { if (run_flag) { char* message = test(); tests_run++; \
          if (message) { return message; } } } while (0)

/// @brief Define to limit the number of lines of an input data file to read.
#define MAX_FILE_LINES (10000)

/// @brief Define for the number of microseconds in a second.
#define USEC_PER_SEC (1000000)

/// @brief Variable used to count the number of tests that have been run.
static int tests_run;

#ifdef DEBUG_T_DIGEST_ARRAYS
/**
 * @brief Function to print an array of float's of the requested size.
 *
 * @param array_in Array of float values.
 * @param size The number of entries of the array.
 */
static void PrintArray(uint32_t* array_in, int size)
{
    printf("[");
    for (int i = 0; i < size; i++) {
        if (i == (size - 1)) {
            printf("%u]\n", array_in[i]);
        } else {
            printf("%u, ", array_in[i]);
        }
    }
}
#endif

/**
 * @brief Function used by qsort to make decisions about how to sort.
 *
 * @param c1_ptr Pointer to the first value to sort.
 * @param c2_ptr Pointer to the second value to sort.
 * @return -1 if c1 < c2, 1 if c1 > c2, and 0 if they are equal.
 */
static int SortComp(const void* c1_ptr, const void* c2_ptr)
{
    uint32_t r1 = *(uint32_t*)(c1_ptr);
    uint32_t r2 = *(uint32_t*)(c2_ptr);
    if (r1 < r2) {
         return -1;
    } else if (r1 > r2) {
         return 1;
    } else {
         return 0;
    }
}

/**
 * @brief Function to get a random number within the range min to max, but to distribute the samples more closely around
 * the 30% and 70% positions in that range as follows:
 * 40% of samples are within 10% of the 30% point in the range.
 * 40% of samples are within 10% of the 70% point in the range.
 * 20% of the samples are randomly distributed across the entire range.
 *
 * @param min The minimum allowable number.
 * @param max The maximum allowable number.
 * @return A random float number with the desired distribution within the specified range.
 */
static uint32_t GetRandFromToSkewed(uint32_t min, uint32_t max)
{
    uint32_t rand_num = rand();
    uint32_t range = (max - min);
    uint32_t div = RAND_MAX / range;
    if (rand_num % 100 < 20) { // 20% of the time, use a random number in the given range
        return min + (rand_num / div);
    } else if (rand_num % 100 < 60) { // 40% of the time, sample is close to the 30% position with small variance
        return min + (0.3 * range) + (rand_num / (div * 10));
    } else { // 40% of the time, sample is close to the 70% position with small variance
        return min + (0.7 * range) + (rand_num / (div * 10));
    }
}

/**
 * @brief Function to get a random number within the range min to max.
 *
 * @param min The minimum allowable number.
 * @param max The maximum allowable number.
 * @return A random float number within the desired range.
 */
static uint32_t GetRandFromTo(uint32_t min, uint32_t max)
{
    uint32_t range = (max - min);
    uint32_t div = RAND_MAX / range;
    return min + (rand() / div);
}

/**
 * @brief This is a generic function that takes in a digest and an array of input samples and then adds all the samples
 * to the digest and then checks some main percentile values for correctness.
 *
 * @param td_handle Pointer to the t-Digest instance to use for this test.
 * @param data_array The array of input samples.
 * @param num_entries The number of input samples in the data_array.
 * @return Message for a failure; NULL if no errors.
 */
static char* TestGenericArray(TDigestHandle td_handle, uint32_t* data_array, int num_entries)
{
    uint32_t max = 0;
    uint32_t min = UINT32_MAX;

    // Log max and min samples from input array so we can use this info later for comparison.
    for (int i = 0; i < num_entries; i++) {
        max = CDI_MAX(data_array[i], max);
        min = CDI_MIN(data_array[i], min);
    }
#ifdef DEBUG_T_DIGEST_ARRAYS
    TDIGEST_LOG_THREAD(kLogInfo, "Sending Samples: ");
    PrintArray(&data_array[0], num_entries);
#endif

    // Now add all generated samples to the digest one by one. Time it so we can see how fast it runs.
    clock_t start = clock();
    for (int i = 0; i < num_entries; i++) {
        TDigestAddSample(td_handle, data_array[i]);
    }
    clock_t end = clock();
    double total = ((double)(end) - (double)(start)) / (double)(CLOCKS_PER_SEC);
    CDI_LOG_THREAD(kLogInfo, "Total time to add all samples: %f (%f per samples)", total,
                       total / (double)num_entries);

    // Now get percentile measurements from the digest.
    int percentile_array[TEST_NUM_PERCENTILES] = { 0, 1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 99, 100 };
    uint32_t results[TEST_NUM_PERCENTILES] = { 0 };
    for (int i = 0; i < TEST_NUM_PERCENTILES; i++) {
        TDigestGetPercentileValue(td_handle, percentile_array[i], &results[i]);
    }

    // Test percentile values that were retrieved above against our known input sample set.
    qsort(data_array, num_entries, sizeof(uint32_t), &SortComp);

    // We know 0 and 1 because those are the min and max, respectively.
    // For all others, check that error is less than .5%.
    COMPARE_RETURN_MSG("Percentile 0 failed check.", results[0] == min);
    int total_error = 0;
    for (int i = 0; i < TEST_NUM_PERCENTILES; i++) {
        int exp_index_num = num_entries * percentile_array[i];
        int exp_index = CDI_MAX(exp_index_num / 100 + ((exp_index_num % 100 != 0) ? 1 : 0) - 1, 0);
        CDI_LOG_THREAD(kLogInfo, "Percentile %d:  expected %lu, got %lu, error %lu", percentile_array[i],
                       data_array[exp_index], results[i], abs(data_array[exp_index]-results[i]));

        total_error += results[i] - data_array[exp_index];
    }
    TDIGEST_LOG_THREAD(kLogInfo, "The average error is %d.", total_error);
    COMPARE_RETURN_MSG("Percentile 100 failed check.", results[TEST_NUM_PERCENTILES-1] == max);
    return NULL;
}

/**
 * @brief Simple test that adds 3 samples to the digest and then checks the percentile values.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestSimple3()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }
    TDigestAddSample(td_handle, 0);
    TDigestAddSample(td_handle, 5);
    TDigestAddSample(td_handle, 10);

    uint32_t value_at_percentile;
    TDigestGetPercentileValue(td_handle, 10, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P10 check.", value_at_percentile == 0);
    TDigestGetPercentileValue(td_handle, 50, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P50 check.", value_at_percentile == 5);
    TDigestGetPercentileValue(td_handle, 90, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P90 check.", value_at_percentile == 10);
    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief Simple test that adds 100 samples to the digest and then checks the percentile values.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestSimple100()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }

    for (int i = 1; i <= 100; i++) {
        TDigestAddSample(td_handle, i);
    }
    uint32_t value_at_percentile = 0;
    TDigestGetPercentileValue(td_handle, 10, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P10 check.", value_at_percentile == 10);
    TDigestGetPercentileValue(td_handle, 50, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P50 check.", value_at_percentile == 50);
    TDigestGetPercentileValue(td_handle, 90, &value_at_percentile);
    COMPARE_RETURN_MSG("Failed P90 check.", value_at_percentile == 90);

    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief Simple test that adds TEST_RUNTIME_SAMPLES uniform random samples to the digest and measures the amount of
 * time it takes.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestRunTime()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    srand(time(NULL));
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }

    // Generate an array of random values.
    uint32_t* input_array = (uint32_t*)CdiOsMemAllocZero(sizeof(uint32_t) * TEST_RUNTIME_SAMPLES);
    for (int i = 0; i < TEST_RUNTIME_SAMPLES; i++) {
        input_array[i] = GetRandFromTo(0, 100);
    }
    // Now add all generated samples to the digest one by one. Time it so we can see how fast it runs.
    clock_t start = clock();
    for (int i = 0; i < TEST_RUNTIME_SAMPLES; i++) {
        TDigestAddSample(td_handle, input_array[i]);
    }
    clock_t end = clock();
    double total = ((double)(end) - (double)(start)) / (double)(CLOCKS_PER_SEC);
    double per = total / (double)(TEST_RUNTIME_SAMPLES) * USEC_PER_SEC;
    CDI_LOG_THREAD(kLogInfo, "Total time to add all samples: %.03f seconds (%.03f microseconds per sample)", total, per);
    (void)per; // This allows compile to work when logging is turned off.
    CdiOsMemFree(input_array);

    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief Simple test that adds TEST_URAND_SAMPLES uniform random samples to the digest and then checks for percentile
 * values.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestUniformRand()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    srand(time(NULL));
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }

    // Generate all input samples for the test. Track max and min so they can be checked later.
    uint32_t actual[TEST_URAND_SAMPLES] = { 0 };
    for (int i = 0; i < TEST_URAND_SAMPLES; i++) {
        actual[i] = GetRandFromTo(0, 100);
    }

    TestGenericArray(td_handle, &actual[0], TEST_URAND_SAMPLES);

    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief Simple test that adds TEST_SRAND_SAMPLES skewed random samples to the digest and then checks for percentile
 * values. "Skewed random samples" are samples that tend to be random within a certain range, with few outside that
 * range. In this case, the function GetRandFromToSkewed() is used to generate a distribution with two main focal
 * ranges and a lighter random distribution outside of those focal ranges.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestSkewedRand()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    srand(time(NULL));
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }

    // Generate all input samples for the test. Track max and min so they can be checked later.
    uint32_t actual[TEST_SRAND_SAMPLES] = { 0 };
    for (int i = 0; i < TEST_SRAND_SAMPLES; i++) {
        actual[i] = GetRandFromToSkewed(0, 100);
    }

    TestGenericArray(td_handle, &actual[0], TEST_SRAND_SAMPLES);

    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief This test reads input samples from a file and feeds them into the digest and then checks for expected
 * percentile values. This is intended to allow users to collect their own data from an actual cdi_test test run and
 * feed it in. The file must be in a format where each line is a sample.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestRealDataFromFile()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }

    // Open file for reading.
    CdiFileID file_ptr;
    char* file_str = "representative_latency_times.txt";
    if (!CdiOsOpenForRead("representative_latency_times.txt", &file_ptr)) {
        CDI_LOG_THREAD(kLogError, "Error opening file [%s] for reading.", file_str);
        return NULL;
    }
    // Read file into array.
    uint32_t actual[MAX_FILE_LINES];
    int i;
    for (i = 0; i < MAX_FILE_LINES; i++) {
        fscanf(file_ptr, "%u\n", &actual[i]);
    }
    fclose(file_ptr);
    char* message = TestGenericArray(td_handle, &actual[0], MAX_FILE_LINES);
    TDigestDestroy(td_handle);
    return message;
}

/**
 * @brief This test verifies that the value NaN is returned under certain known circumstances, such as when the digest
 * is empty, or when a percentile outside of 0-100, inclusive, is requested.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* TestInvalidPercentiles()
{
    CDI_LOG_THREAD(kLogInfo, "\n");
    CDI_LOG_THREAD(kLogInfo, "Starting test: %s.", __func__);
    TDigestHandle td_handle = NULL;
    if (TDigestCreate(&td_handle)) {
        TDIGEST_LOG_THREAD(kLogInfo, "Successfully created t-Digest.");
    } else {
        TDIGEST_LOG_THREAD(kLogInfo, "Failed to create t-Digest.");
    }
    uint32_t value_at_percentile;
    COMPARE_RETURN_MSG("Unexpected value found at 0", !TDigestGetPercentileValue(td_handle, 0, &value_at_percentile));
    COMPARE_RETURN_MSG("Unexpected value found at 50", !TDigestGetPercentileValue(td_handle, 50, &value_at_percentile));
    COMPARE_RETURN_MSG("Unexpected value found at 100",
                       !TDigestGetPercentileValue(td_handle, 100, &value_at_percentile));
    TDigestAddSample(td_handle, 1);
    COMPARE_RETURN_MSG("Unexpected value found at -10",
                       !TDigestGetPercentileValue(td_handle, -10, &value_at_percentile));
    COMPARE_RETURN_MSG("Unexpected value found at 101",
                       !TDigestGetPercentileValue(td_handle, 101, &value_at_percentile));
    TDigestDestroy(td_handle);
    return NULL;
}

/**
 * @brief Runs all tests. Use the boolean parameter after the test name to enable or disable tests.
 *
 * @return Message for a failure; NULL if no errors.
 */
static char* AllTests()
{
    RUN_TEST(TestSimple3, true);
    RUN_TEST(TestSimple100, true);
    RUN_TEST(TestUniformRand, true);
    RUN_TEST(TestRunTime, false);
    RUN_TEST(TestSkewedRand, true);
    RUN_TEST(TestRealDataFromFile, true);
    RUN_TEST(TestInvalidPercentiles, true);
    return NULL;
}

/**
 * @brief Public wrapper for AllTests() above.
 *
 * @return True if pass and false if fail.
 */
bool CdiTestUnitTDigest(void) {
    CDI_LOG_THREAD(kLogInfo, "\nRunning tests for verification of the t_digest module.");
    char* result = AllTests();
    CDI_LOG_THREAD(kLogInfo, "Tests run: %d.", tests_run);
    if (result != NULL) {
        CDI_LOG_THREAD(kLogInfo, "%s", result);
        return false;
    } else {
        CDI_LOG_THREAD(kLogInfo, "All[%d] Unit Tests for t-Digest PASSED.", tests_run);
        return true;
    }
}
#endif // DEBUG_T_DIGEST_UNIT_TEST
