// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains definitions and implementation of various unit tests for checking the functionality of the
 * timeout.c API functions.
 */

#include "cdi_logger_api.h"
#include "cdi_os_api.h"
#include "cdi_test.h"
#include "timeout.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief A structure to be used by the unit test callback function to signal to the test when the callback occurred
 * and whether the callback was successful.
 */
typedef struct CallBackUserData {
    uint64_t        expiration_us;  ///<Expected timeout in microseconds.
    int             callback_number; ///<The order in which the callback is expected to be received.
    bool            pass; ///<Pass/fail status for a given timeout set by the callback function.
    CdiSignalType  signal; ///<Signal back to the main test thread indicate the callback function was executed.
} CallBackUserData;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief A callback function to be executed when a timer expires. This function checks the time when the timer
 * callback occurs and compares the callback time with the expected timer expiration time that was stored when the
 * timer was originally set. If the actual time is within a specified range of the expected time then callback
 * user data has its pass field set as true.
 *
 * @param data_ptr is a pointer to the CdiTimeoutCbData that is used for timeout callbacks. This structure contains
 * a user data pointer to CallBackUserData which stores the expected expiration time and the pass signal that
 * was mentioned above
 */
static void TimerCallback(const CdiTimeoutCbData* data_ptr)
{
    CallBackUserData* user_data_ptr = (CallBackUserData*)data_ptr->user_data_ptr;
    uint64_t cb_time = CdiOsGetMicroseconds() / 1000;
    uint64_t success_time_max = (user_data_ptr->expiration_us + 3000) / 1000;
    uint64_t success_time_min = (user_data_ptr->expiration_us - 500) / 1000;
    CdiOsSignalSet(user_data_ptr->signal);
    if ((cb_time >= success_time_min) && (cb_time <= success_time_max)) {
        user_data_ptr->pass = true;
    } else {
        user_data_ptr->pass = false;
        CDI_LOG_THREAD(kLogInfo, "Callback number[%d] received at time [%u]ms with expiration of[%u]ms",
                       user_data_ptr->callback_number, cb_time, user_data_ptr->expiration_us / 1000);
    }
}

/**
 * @brief A test to create a timeout instance add a timer to it, let it expire and check the callback.
 *
 * @param timeout_ms is a timeout to set for the created timer
 *
 * @return true if test pass, false if failure
 */
static bool OneShotTimeoutTest(int timeout_ms)
{
    bool pass = true;

    CallBackUserData user_data;
    CdiOsSignalCreate(&user_data.signal);
    user_data.callback_number = 1;
    CDI_LOG_THREAD(kLogInfo, "Performing 1 shot test for timeout of [%d]ms", timeout_ms);

    CdiTimeoutInstanceHandle timer_handle = NULL;
    if (kCdiStatusOk == CdiTimeoutCreate(NULL, &timer_handle)) {
        pass = true;
    } else {
        pass = false;
        CDI_LOG_THREAD(kLogError, "Failed to create Timeout");
    }

    if (pass) {
        user_data.expiration_us = CdiOsGetMicroseconds();
        user_data.expiration_us = user_data.expiration_us + (timeout_ms * 1000);
        TimeoutHandle timeout_handle = NULL;
        pass = CdiTimeoutAdd(timer_handle, &TimerCallback, 1000*timeout_ms, &user_data, &timeout_handle);
        if (!pass) {
            CDI_LOG_THREAD(kLogError, "Failed to add timeout");
        }
    }

    if (pass) {
        bool timeout_on_timeouts;
        CdiOsSignalWait(user_data.signal, (timeout_ms + 1) * 2, &timeout_on_timeouts);
        if (timeout_on_timeouts) {
            CDI_LOG_THREAD(kLogError, "Timeout occurred waiting for callback function signal");
            pass = false;
        } else if (user_data.pass) {
        } else {
            CDI_LOG_THREAD(kLogError, "Callback failed to report pass");
            pass = false;
        }
    }

    CdiOsSignalDelete(user_data.signal);
    user_data.signal = NULL;

    CdiTimeoutDestroy(timer_handle);

    return pass;
}

/**
 * @brief A test to set multiple timers.
 *
 * @param num_timers sets how many timers are added to the timeout instance
 * @param reverse if true, each successive timer object has an earlier deadline as the list is built.
 *
 * @return true if passes, false if failed
 */
static bool MultipleTimersTest(unsigned int num_timers, bool reverse)
{
    bool pass = true;

    if (num_timers > MAX_TIMERS) {
        CDI_LOG_THREAD(kLogInfo, "Attempting to set more than [%d] timers. Will attempt to add all timers but only check for [%d] of returns",
                 MAX_TIMERS, MAX_TIMERS);
    }

    CdiTimeoutInstanceHandle timer_handle = NULL;
    if (kCdiStatusOk == CdiTimeoutCreate(NULL, &timer_handle)) {
        pass = true;
    } else {
        pass = false;
        CDI_LOG_THREAD(kLogError, "Failed to create timeout");
    }

    CdiOsSleep(2);
    CallBackUserData user_data[num_timers];
    CdiSignalType signals_array[num_timers];

    if (pass) {
        for (unsigned int i=0; i<num_timers; i++) {
            user_data[i].pass = false;
            CdiOsSignalCreate(&user_data[i].signal);
            int timeout_ms;
            if (reverse) {
                timeout_ms = 2*num_timers - i*2;
                signals_array[num_timers -1 -i] = user_data[i].signal;
            } else {
                timeout_ms = 2*i + 1;
                signals_array[i] = user_data[i].signal;
            }

            uint64_t curr_time_us = CdiOsGetMicroseconds();
            user_data[i].expiration_us = curr_time_us + (timeout_ms * 1000);
            TimeoutHandle timeout_handle[num_timers];
            if (!CdiTimeoutAdd(timer_handle, &TimerCallback, (timeout_ms * 1000), &user_data[i], &timeout_handle[i])) {
                if (i >= MAX_TIMERS) {
                    CDI_LOG_THREAD(kLogWarning, "Timeout add failed because there are too many active timers. This is not considered an error.");
                } else {
                    CDI_LOG_THREAD(kLogError, "Failed adding timer[%d]. There should be timers in pool available to add.", i);
                    pass = false;
                }
            }
            CdiOsSleepMicroseconds(100);
        }
    }

    unsigned int last_cb_count = -1;

    bool waiting_callbacks = true;

    while (waiting_callbacks) {
        unsigned int signal_index;
        CdiOsSignalsWait(signals_array, num_timers, false, (num_timers * 4), &signal_index);
        if (signal_index == (last_cb_count + 1)) {
            last_cb_count = signal_index;
            CdiOsSignalClear(signals_array[signal_index]);
            if (signal_index == (MAX_TIMERS -1) || signal_index == (num_timers - 1)) {
                waiting_callbacks = false;
            }
        } else if (signal_index == OS_SIG_TIMEOUT) {
            CDI_LOG_THREAD(kLogError, "Timeout waiting for signals from callback");
            waiting_callbacks = false;
            pass = false;
        } else {
            CDI_LOG_THREAD(kLogError, "Received callback signal out of order, received[%d]. Expected[%d]", signal_index,
                     last_cb_count + 1);
            last_cb_count = signal_index;
            CdiOsSignalClear(signals_array[signal_index]);
            pass = false;
            if (signal_index == (MAX_TIMERS -1) || signal_index == (num_timers - 1)) {
                waiting_callbacks = false;
            }
        }
    }

    if (num_timers > MAX_TIMERS) {
        last_cb_count = MAX_TIMERS;
    } else {
        last_cb_count = num_timers;
    }

    /// Check the pass status of all of the user_data structures sent to the callback function.
    /// The callback function logged errors for additional detail into the actual failure. Checking here as well to
    /// catch any user data structures where the callback didn't occur at all.
    for (unsigned int i=0; i<last_cb_count; i++) {
        if (!user_data[i].pass) {
            CDI_LOG_THREAD(kLogError, "ERROR: user_data[%d] reported failed either callback execution time was out of "
                           "range or callback failed to run", i);
            pass = false;
        }
    }

    for (unsigned int i=0; i<num_timers; i++) {
        CdiOsSignalDelete(user_data[i].signal);
        user_data[i].signal = NULL;
    }

    CdiTimeoutDestroy(timer_handle);

    return pass;
}

/**
 * @brief A test to set multiple timers, let them expire in order and check the callback values.
 *
 * @param num_timers sets how many timers to add and remove from the timeout instance
 *
 * @return true for pass, false for test failure
 */
static bool TimersSetAndClear(uint32_t num_timers)
{
    bool pass = true;

    if (num_timers > MAX_TIMERS) {
        CDI_LOG_THREAD(kLogInfo, "Attempting to set more than [%d] timers. Will attempt to add all timers but only check for [%d] of returns",
                 MAX_TIMERS, MAX_TIMERS);
    }

    CdiTimeoutInstanceHandle timer_handle = NULL;
    if (kCdiStatusOk == CdiTimeoutCreate(NULL, &timer_handle)) {
        pass = true;
    } else {
        pass = false;
        CDI_LOG_THREAD(kLogError, "Failed to create Timeout");
    }

    CdiOsSleep(2);
    CallBackUserData user_data[num_timers];

    TimeoutHandle timeout_handle[num_timers];
    if (pass) {
        for (unsigned int i=0; i<num_timers; i++) {
            int timeout_ms = 15;
            user_data[i].pass = false;
            CdiOsSignalCreate(&user_data[i].signal);
            uint64_t curr_time_us = CdiOsGetMicroseconds();
            user_data[i].expiration_us = curr_time_us + (timeout_ms * 1000);
            if (!CdiTimeoutAdd(timer_handle, &TimerCallback, (timeout_ms * 1000), &user_data[i], &timeout_handle[i])) {
                if (i >= MAX_TIMERS) {
                    CDI_LOG_THREAD(kLogWarning, "Timeout add failed because there are too many active timers. This is not considered an error.");
                } else {
                    CDI_LOG_THREAD(kLogError, "Failed adding timer[%d]. There should be timers in pool available to add.", i);
                    pass = false;
                }
            }
        }
    }

    if (pass) {
        for (unsigned int i=0; i<num_timers; i++) {
            if (!CdiTimeoutRemove(timeout_handle[i], timer_handle)) {
                if (i < MAX_TIMERS) {
                    CDI_LOG_THREAD(kLogError, "TimeoutRemove failed");
                    pass = false;
                } else {
                    CDI_LOG_THREAD(kLogInfo, "TimeoutRemove failed but more than [%d] timers have been returned so this is not considered an error", MAX_TIMERS);
                }
            }
        }
    }

    // wait for timers to expire
    CdiOsSleep(2000);

    if (pass) {
        for (unsigned int i=0; i<num_timers; i++) {
            if (CdiOsSignalGet(user_data[i].signal)) {
                pass = false;
                CDI_LOG_THREAD(kLogError, "Signal received for timer[%d] after it was cleared", i);
            }
        }
    }

    for (unsigned int i=0; i<num_timers; i++) {
        CdiOsSignalDelete(user_data[i].signal);
        user_data[i].signal = NULL;
    }

    CdiTimeoutDestroy(timer_handle);

    return pass;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/// Main routine to test the timeout function.
bool TestUnitTimeout(void)
{
    bool pass = true;

    if (pass) {
        CDI_LOG_THREAD(kLogInfo, "Starting TimersSetAndClear test");
        pass = TimersSetAndClear(MAX_TIMERS);
        if (pass) {
            CDI_LOG_THREAD(kLogInfo, "TimersSetAndClear test passed");
        } else {
            CDI_LOG_THREAD(kLogError, "TimersSetAndClear test failed");
        }
    }

    if (pass) {
        // Set a timeout instance and let the timers run out in order
        CDI_LOG_THREAD(kLogInfo, "Starting multiple_timers test forward order");
        pass = MultipleTimersTest(MAX_TIMERS, false);
        if (pass) {
            CDI_LOG_THREAD(kLogInfo, "Forward order MultipleTimersTest passed");
        } else {
            CDI_LOG_THREAD(kLogError, "Forward order MultipleTimersTest failed");
        }
    }

    if (pass) {
        CDI_LOG_THREAD(kLogInfo, "Starting multiple_timers test reverse order");
        pass = MultipleTimersTest(MAX_TIMERS, true);
        if (pass) {
            CDI_LOG_THREAD(kLogInfo, "Reverse order MultipleTimersTest passed");
        } else {
            CDI_LOG_THREAD(kLogError, "Reverse order MultipleTimersTest failed");
        }
    }

    // Let's make a bunch of one off timeout instances of different times
    for (int i=0; i<10; i++) {
        if (pass) {
            CDI_LOG_THREAD(kLogInfo, "Starting one shot test");
            pass = OneShotTimeoutTest(i);
            if (pass) {
                CDI_LOG_THREAD(kLogInfo, "One shot test passed");
            } else {
                CDI_LOG_THREAD(kLogError, "One shot test failed");
            }
        }
    }

    return pass;
}

