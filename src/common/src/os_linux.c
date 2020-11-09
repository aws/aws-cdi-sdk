// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the Linux definitions for OS functions for creating/deleting
 * threads, semaphores, mutexes, and also for sleeps and string copies.
 *
 */

/// @brief Linux definition.
#define __USE_XOPEN2K


/// @brief _GNU_SOURCE Linux required for pthread_setname_np().
#define _GNU_SOURCE

#include "cdi_os_api.h"

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "cdi_logger_api.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

// Provides visual output of thread pinning configuration.
//#define VIEW_THREAD_PINNING

/// @brief Linux definition of stack size.
#define THREAD_STACK_SIZE (1024*1024)

/// @brief Maximum number of threads that can register for notifications from another signal.
#define MAX_THREADS_WAITING (50)

/// Thread Info is kept in a doubly-linked list.
typedef struct CdiThreadInfo CdiThreadInfo;

/**
 * @brief Structure to keep track of thread state info.
 */
struct CdiThreadInfo
{
    /// Thread ID.
    pthread_t      thread_id;
    /// Name attached to thread, if any.
    char           thread_name_str[MAX_THREAD_NAME];
    /// Thread function that will be used in ThreadFuncHelper().
    ThreadFuncName thread_func;
    /// The argument given to thread_func.
    void*          thread_func_arg_ptr;
    /// Signal used to start the thread. If NULL, thread starts immediately.
    CdiSignalType start_signal;
    /// The signal that is set when thread_func returns.
    CdiSignalType  is_done;

    /// If non-zero, CdiOsThreadJoin() has been called to wait for the thread to exit.
    int exit;
};

/// @brief Forward declaration to create pointer to semaphore info when used.
typedef struct SemInfo SemInfo;
/**
 * @brief Structure used to hold semaphore state data.
 */
struct SemInfo
{
    sem_t       sem;            ///< Semaphore ID
    SemInfo*    sem_prev_ptr;   ///< Pointer to previous semaphore info in list.
    SemInfo*    sem_next_ptr;   ///< Pointer to next semaphore info in list.
};

/// @brief Forward declaration to create pointer to signal info when used.
typedef struct SignalInfo SignalInfo;
/**
 * @brief Structure used to hold signal state data.
 */
struct SignalInfo
{
    pthread_mutex_t mutex;    ///< Mutex to protect a signal.
    pthread_cond_t condition; ///< Condition variable for threads to wait.

    /// @brief Low bit is the current signal state. Upper bits are the current signal number we are at. This is used to
    /// guarantee that every thread goes through once, even if the signal has been reset.
    volatile uint32_t signal_count;

    /// @brief Keeps track of who else needs to be signaled.
    int num_other_sigs;       ///< Number of entries.
    struct SignalInfo* other_sigs_ptr_array[MAX_THREADS_WAITING]; ///< Other signals to wake up when this is signaled.
};

/// @brief Macro used within this file to handle generation of error messages either to the logger or stderr.
#define ERROR_MESSAGE(...) ErrorMessage(__FUNCTION__, __LINE__, __VA_ARGS__)

/// Maximum length of a single formatted message string.
#define MAX_FORMATTED_MESSAGE_LENGTH    (1024)

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// Preferred clock used when doing timing calculations, because unaffected by system clock adjustments.
static const clockid_t preferred_clock = CLOCK_MONOTONIC;

/// Array of data used to hold signal handlers.
static SignalHandlerInfo signal_handler_function_array[MAX_SIGNAL_HANDLERS];

/// Number of signal handlers in signal_handler_function_array.
static int signal_handler_count = 0;

/// If true, the CDI logger will be used to generate error messages, otherwise output will be sent to stderr.
static bool use_logger = false;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Generate an error message and send to logger or stderr.
 *
 * @param func_name_str Pointer to function name string.
 * @param line Source line number.
 * @param format_str Pointer to format string.
 * @param ... Variable length list of message arguments.
 */
static void ErrorMessage(const char* func_name_str, int line, const char* format_str, ...)
{
    char msg_buffer_str[MAX_FORMATTED_MESSAGE_LENGTH];
    msg_buffer_str[0] = '\0';
    va_list val;
    va_start(val, format_str);

    vsnprintf(msg_buffer_str, sizeof(msg_buffer_str), format_str, val);
    if (use_logger) {
        CdiLogger(CdiLoggerThreadLogGet(), kLogComponentGeneric, kLogError, func_name_str, line, msg_buffer_str);
    } else {
        fprintf(stderr, "[%s:%d] ERROR: %s.\r\n", func_name_str, line, msg_buffer_str);
    }

    va_end(val);
}

/**
 *  Get the time for a given delay (timeout) and return it.
 *
 *  @param spec A timespec pointer for the resulting time calculation.
 *  @param num_ms The number of milliseconds into the future to calculate.
 *  @param clock_id Clock to use (CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID).
 */
static void GetTimeout(struct timespec* spec, uint32_t num_ms, clockid_t clock_id)
{
    struct timespec this_time;

    if (clock_gettime(clock_id, &this_time) == -1) {
        ERROR_MESSAGE("Cannot get current time");
    }

    spec->tv_sec = this_time.tv_sec + (((num_ms * 1000L) + (this_time.tv_nsec/1000)) / 1000000L);
    spec->tv_nsec = 1000L * (((num_ms * 1000L) + (this_time.tv_nsec/1000)) % 1000000L);
}

/**
 *  This populates the sigaction structure with the appropriate flags and user-defined callback.
 *
 *  @param sig_act_ptr The pointer to the sigaction structure.
 *  @param func_ptr The pointer to the callback that executes on an intercepted signal.
 */
static void PopulateSigAction(struct sigaction* sig_act_ptr, SignalHandlerFunction func_ptr)
{
    memset(sig_act_ptr, 0, sizeof(struct sigaction));
    sig_act_ptr->sa_flags = SA_SIGINFO;
    sigemptyset(&sig_act_ptr->sa_mask);
    sig_act_ptr->sa_sigaction = func_ptr;
}

/**
 *  When we create a thread, we use ThreadFuncHelper so that the is_done signal can be set.  The only reason for this is
 *  so we can timeout when we try to join the thread.  (Our osSignalWait can timeout but pthread_join can't.)
 *
 *  @param thread_ptr The pointer to the thread info.
 */
static void* ThreadFuncHelper(void* thread_ptr)
{
    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_ptr;

    // Set any signal handlers that have been set for this thread.
    for (int i = 0; i < MAX_SIGNAL_HANDLERS; i++) {
        struct sigaction sig_act;
        PopulateSigAction(&sig_act, signal_handler_function_array[i].func_ptr);
        sigaction(signal_handler_function_array[i].signal_num, &sig_act, NULL);
    }

    if (thread_info_ptr->start_signal) {
        CdiOsSignalWait(thread_info_ptr->start_signal, CDI_INFINITE, NULL);
    }

    // No need to start thread if we are already waiting for it to exit via CdiOsThreadJoin().
    if (0 == CdiOsAtomicRead32(&thread_info_ptr->exit)) {
        (thread_info_ptr->thread_func)(thread_info_ptr->thread_func_arg_ptr);
    }

    CdiOsSignalSet(thread_info_ptr->is_done);

    return NULL;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void CdiOsUseLogger(void)
{
    use_logger = true;
}

// -- threads --
bool CdiOsSignalHandlerSet(int signal_num, SignalHandlerFunction func_ptr)
{
    bool ret = true;

    if (signal_handler_count >= MAX_SIGNAL_HANDLERS) {
        return false;
    }

    signal_handler_function_array[signal_handler_count].signal_num = signal_num;
    signal_handler_function_array[signal_handler_count].func_ptr = func_ptr;
    signal_handler_count++;

    struct sigaction sig_act;
    PopulateSigAction(&sig_act, func_ptr);

    if (0 != sigaction(signal_num, &sig_act, NULL)) {
        ret = false;
    }

    return ret;
}

bool CdiOsThreadCreatePinned(ThreadFuncName thread_func, CdiThreadID* thread_id_out_ptr, const char* thread_name_str,
                             void* thread_func_arg_ptr, CdiSignalType start_signal, int cpu_affinity)
{
    bool return_val = true;
    int temp_rc;
    CdiThreadInfo* thread_info_ptr;

    assert(thread_id_out_ptr != NULL);
    assert(thread_func != NULL);

    *thread_id_out_ptr = NULL;

    // Create a new thread record.
    thread_info_ptr = CdiOsMemAllocZero(sizeof(CdiThreadInfo));
    if (NULL == thread_info_ptr) {
        ERROR_MESSAGE("failed to allocate memory");
        return false;
    }
    *thread_id_out_ptr = (CdiThreadID)thread_info_ptr;

    // Name the thread; limit name to MAX_THREAD_NAME characters.
    if (thread_name_str != NULL) {
        strncpy(thread_info_ptr->thread_name_str, thread_name_str, MAX_THREAD_NAME);
        thread_info_ptr->thread_name_str[MAX_THREAD_NAME-1] = '\0'; // Ensure string is null-terminated.
    } else {
        thread_info_ptr->thread_name_str[0] = '\0'; // Ensure string is null-terminated.
    }

    thread_info_ptr->thread_func = thread_func;
    thread_info_ptr->thread_func_arg_ptr = thread_func_arg_ptr;
    thread_info_ptr->start_signal = start_signal;
    return_val = CdiOsSignalCreate(&thread_info_ptr->is_done);

    if (return_val) {
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) == 0) {
            if (cpu_affinity >= 0) {
                cpu_set_t s;
                CPU_ZERO(&s);
                CPU_SET(cpu_affinity, &s);
                if (pthread_attr_setaffinity_np(&attr, sizeof(s), &s) != 0) {
                    return_val = false;
                }
            }
        } else {
            ERROR_MESSAGE("failed to set thread attributes");
            return_val = false;
        }

        if (return_val) {
            temp_rc = pthread_create(&thread_info_ptr->thread_id, &attr, ThreadFuncHelper, thread_info_ptr);
            if (temp_rc) {
                ERROR_MESSAGE("pthread_create failed[%d]", temp_rc);
                return_val = false;
            } else if (NULL != thread_name_str) {
                // Set the thread name in the system (Linux specific). This is useful because these show up in the
                // output of GDB's "info threads", "ps -L", and possibly others. pthread_setname_np()'s man page says
                // that the name can be no longer than 16 characters including the terminating NUL. Further, the
                // function will fail if the string passed in is too long. Truncate to 15 characters.
                char tmp[16];
                strncpy(tmp, thread_name_str, sizeof(tmp));
                tmp[sizeof(tmp) - 1] = '\0';  // Function strncpy won't null terminate if truncated.
                pthread_setname_np(thread_info_ptr->thread_id, tmp);
            }
        }

        pthread_attr_destroy(&attr);
    }

    if (!return_val) {
        if (thread_info_ptr->is_done) {
            CdiOsSignalDelete(thread_info_ptr->is_done);
        }

        CdiOsMemFree(thread_info_ptr);
        *thread_id_out_ptr = NULL;
    }

    return return_val;
}

bool CdiOsThreadAllocData(CdiThreadData* handle_out_ptr)
{
    return (0 == pthread_key_create((pthread_key_t*)handle_out_ptr, NULL));
}

bool CdiOsThreadFreeData(CdiThreadData handle)
{
    return (0 == pthread_key_delete(handle));
}

bool CdiOsThreadSetData(CdiThreadData handle, void* content_ptr)
{
    if (0 == pthread_setspecific(handle, content_ptr)) {
        return true;
    } else {
        return false;
    }
}

bool CdiOsThreadGetData(CdiThreadData handle, void** content_out_ptr)
{
    // pthread_getspecific does not return errors. It returns NULL when the key does not exist, but NULL is a valid
    // return value for keys that do exist.
    *content_out_ptr = pthread_getspecific(handle);

    return true;
}

const char* CdiOsThreadGetName(CdiThreadID thread_id)
{
    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_id;
    return thread_info_ptr->thread_name_str;
}

bool CdiOsThreadJoin(CdiThreadID thread_id, uint32_t timeout_in_ms, bool* timed_out_ptr)
{
    int temp_rc;
    bool return_val = true;
    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_id;
    bool is_timeout;

    assert(thread_id != NULL);

    if (timed_out_ptr) {
        *timed_out_ptr = false;
    }

    // If thread uses a start signal, ensure that it is set so the thread can run before we shut it down.
    CdiOsAtomicInc32(&thread_info_ptr->exit); // Ensure value is non-zero before setting the signal.
    if (thread_info_ptr->start_signal) {
        CdiOsSignalSet(thread_info_ptr->start_signal);
    }

    return_val = CdiOsSignalWait(thread_info_ptr->is_done, timeout_in_ms, &is_timeout);
    if (!return_val) {
        ERROR_MESSAGE("CdiOsSignalWait");
    } else if (is_timeout) {
        ERROR_MESSAGE("Thread join exited with WAIT_TIMEOUT");
        if (timed_out_ptr) {
            *timed_out_ptr = true;
        }
        return_val = false;
    } else {
        temp_rc = pthread_join(thread_info_ptr->thread_id, NULL);
        if (temp_rc) {
            ERROR_MESSAGE("pthread_join exited with[%d]", temp_rc);
            return_val = false;
        } else {
            // Free the memory for this thread's info data structure.
            CdiOsSignalDelete(thread_info_ptr->is_done);
            CdiOsMemFree(thread_info_ptr);
        }
    }
    return return_val;
}

// -- semaphores --
bool CdiOsSemaphoreCreate(CdiSemID* ret_sem_handle_ptr, int sem_count)
{
    bool return_val = true;
    SemInfo* sem_info_ptr;
    int temp_rc;

    assert(ret_sem_handle_ptr != NULL);
    assert(sem_count >= 0);

    *ret_sem_handle_ptr = NULL;

    // Create a new thread record.
    sem_info_ptr = CdiOsMemAllocZero(sizeof(SemInfo));
    if (NULL == sem_info_ptr) {
        return_val = false;
        ERROR_MESSAGE("failed to allocate memory");
    } else {
        temp_rc = sem_init(&sem_info_ptr->sem, 0, sem_count);
        if (temp_rc < 0) {
            ERROR_MESSAGE("Cannot create Semaphores[%d]", temp_rc);
            CdiOsMemFree(sem_info_ptr);
            return_val = false;
        } else {
            *ret_sem_handle_ptr = (CdiSemID)sem_info_ptr;
        }
    }

    return return_val;
}

bool CdiOsSemaphoreDelete(CdiSemID sem_handle)
{
    bool return_val = true;

    if (sem_handle) {
        SemInfo* sem_info_ptr = (SemInfo*)sem_handle;

        if (0 == sem_destroy(&sem_info_ptr->sem)) {
            // Release semaphore info memory.
            CdiOsMemFree(sem_info_ptr);
        } else {
            ERROR_MESSAGE("sem_destroy() failed");
            return_val = false;
        }
    }

    return return_val;
}

bool CdiOsSemaphoreRelease(CdiSemID sem_handle)
{
    int temp_rc = 0;
    SemInfo *sem_info_ptr = (SemInfo*)sem_handle;

    assert(sem_info_ptr != NULL);

    temp_rc = sem_post(&sem_info_ptr->sem);
    if (0 != temp_rc) {
        ERROR_MESSAGE("sem_post() failed");
    }

    return temp_rc == 0;
}

bool CdiOsSemaphoreReserve(CdiSemID sem_handle, int timeout_in_ms)
{
    int temp_rc;
    SemInfo* sem_info_ptr = (SemInfo*)sem_handle;

    assert(sem_info_ptr != NULL);

    if (timeout_in_ms == (int)CDI_INFINITE) {
        temp_rc = sem_wait(&sem_info_ptr->sem);
    }
    else {
        struct timespec time_to_wait_until;
        GetTimeout(&time_to_wait_until, timeout_in_ms, CLOCK_REALTIME); // sem_timedwait uses CLOCK_REALTIME
        temp_rc = sem_timedwait(&sem_info_ptr->sem, &time_to_wait_until);
    }

    return temp_rc == 0;
}

int CdiOsSemaphoreValueGet(CdiSemID sem_handle)
{
    int temp_rc = 0;
    SemInfo* sem_info_ptr = (SemInfo*)sem_handle;

    assert(sem_info_ptr != NULL);

    sem_getvalue(&sem_info_ptr->sem, &temp_rc);
    return temp_rc;
}

// -- critical sections --
bool CdiOsCritSectionCreate(CdiCsID* cs_handle_ptr)
{
    bool return_val = true;
    pthread_mutexattr_t attr;

    return_val = (0 == pthread_mutexattr_init(&attr));
    if (return_val) {
        return_val = (0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
    }

    *cs_handle_ptr = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (*cs_handle_ptr == NULL) {
        return_val = false;
        ERROR_MESSAGE("failed to allocate memory");
    } else {
        pthread_mutex_init((pthread_mutex_t *)*cs_handle_ptr, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    return return_val;
}

void CdiOsCritSectionReserve(CdiCsID cs_handle)
{
    pthread_mutex_lock((pthread_mutex_t*)cs_handle);
}

void CdiOsCritSectionRelease(CdiCsID cs_handle)
{
    pthread_mutex_unlock((pthread_mutex_t*)cs_handle);
}

bool CdiOsCritSectionDelete(CdiCsID cs_handle)
{
    if (cs_handle) {
        pthread_mutex_destroy((pthread_mutex_t *)cs_handle);
        free(cs_handle);
    }

    return true;
}

// -- signals --
bool CdiOsSignalCreate(CdiSignalType* signal_handle_ptr)
{
    bool return_val = true;
    assert(NULL != signal_handle_ptr);

    *signal_handle_ptr = CdiOsMemAllocZero(sizeof(SignalInfo));
    if (NULL == *signal_handle_ptr) {
        return_val = false;
        ERROR_MESSAGE("failed to allocate memory");
    } else {
        SignalInfo* signal_info_ptr = *(SignalInfo**)signal_handle_ptr;
        pthread_condattr_t attr;
        int i;

        pthread_mutex_init(&signal_info_ptr->mutex, NULL);
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, preferred_clock);
        pthread_cond_init(&signal_info_ptr->condition, &attr);
        pthread_condattr_destroy(&attr);

        signal_info_ptr->signal_count = 0;

        signal_info_ptr->num_other_sigs = 0;

        for(i = 0; i < MAX_THREADS_WAITING; i++) {
            signal_info_ptr->other_sigs_ptr_array[i] = NULL;
        }
    }

    return return_val;
}


bool CdiOsSignalDelete(CdiSignalType signal_handle)
{
    bool return_val = true;

    if (signal_handle) {
        SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;
        assert(signal_info_ptr->num_other_sigs == 0);

        pthread_mutex_destroy(&signal_info_ptr->mutex);
        pthread_cond_destroy(&signal_info_ptr->condition);

        CdiOsMemFree(signal_info_ptr);
    }

    return return_val;
}

bool CdiOsSignalClear(CdiSignalType signal_handle)
{
    bool return_val = true;
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;
    assert(NULL != signal_handle);

    // There is no need to grab the lock here. Clear the bottom signal bit while leaving the rest alone.
    __sync_fetch_and_and(&signal_info_ptr->signal_count, ~1U);

    return return_val;
}

bool CdiOsSignalSet(CdiSignalType signal_handle)
{
    bool return_val = true;
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;
    int i;
    assert(NULL != signal_handle);

    pthread_mutex_lock(&signal_info_ptr->mutex);
    uint32_t signal_count = signal_info_ptr->signal_count;
    signal_count += 2;
    signal_count |= 1;
    signal_info_ptr->signal_count = signal_count;
    pthread_cond_broadcast(&signal_info_ptr->condition);

    // We have to give up our lock here since we may acquire another lock during the other signals processing.
    pthread_mutex_unlock(&signal_info_ptr->mutex);

    // To support wait multiple we need to wake up all listening threads that are waiting on another condition variable.
    if (signal_info_ptr->num_other_sigs > 0) {
        for (i = 0; i < MAX_THREADS_WAITING; i++) {
            // Make a copy of the pointer, since the value in the array can be set to NULL under our nose.
            SignalInfo* other_signal_info_ptr = signal_info_ptr->other_sigs_ptr_array[i];
            if (other_signal_info_ptr != NULL) {
                // To prevent race condition only signal a condition if you hold the lock to that condition.
                pthread_mutex_lock(&other_signal_info_ptr->mutex);
                pthread_cond_broadcast(&other_signal_info_ptr->condition);
                pthread_mutex_unlock(&other_signal_info_ptr->mutex);
            }
        }
    }

    return return_val;
}

bool CdiOsSignalGet(CdiSignalType signal_handle)
{
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    assert(signal_handle);

    return 1 & __sync_fetch_and_add(&signal_info_ptr->signal_count, 0);
}

bool CdiOsSignalReadState(CdiSignalType signal_handle)
{
    return CdiOsSignalGet(signal_handle);
}

bool CdiOsSignalWait(CdiSignalType signal_handle, uint32_t timeout_in_ms, bool* timed_out_ptr)
{
    bool return_val = true;
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;
    struct timespec time_to_wait_until;

    if (timed_out_ptr) {
        *timed_out_ptr = false;
    }

    if (timeout_in_ms != CDI_INFINITE) {
        GetTimeout(&time_to_wait_until, timeout_in_ms, preferred_clock);
    }

    uint32_t signal_count = signal_info_ptr->signal_count;
    if (!(signal_count & 1)) {
        pthread_mutex_lock(&signal_info_ptr->mutex);

        // Wait until the signal is set. Note that it is possible for us to sleep through a set/clear cycle. Even if
        // the signal is not currently set, we are released if the signal count changes.
        while (signal_info_ptr->signal_count == signal_count) {
            if (timeout_in_ms != CDI_INFINITE) {
                if (pthread_cond_timedwait(&signal_info_ptr->condition, &signal_info_ptr->mutex, &time_to_wait_until)) {
                    if (timed_out_ptr) {
                        *timed_out_ptr = true;
                    }
                    break;
                }
            } else {
                pthread_cond_wait(&signal_info_ptr->condition, &signal_info_ptr->mutex);
            }
        }

        pthread_mutex_unlock(&signal_info_ptr->mutex);
    } else {
        // The pthread_mutex functions imply a memory barrier. If we don't wait we must do our own.
        __sync_synchronize();
    }

    return return_val;
}

bool CdiOsSignalsWait(CdiSignalType* signal_array, uint8_t num_signals, bool wait_all, uint32_t timeout_in_ms,
                      uint32_t* ret_signal_index_ptr)
{
    bool return_val = true;
    SignalInfo** signal_info_ptr_array = (SignalInfo**)signal_array;
    uint32_t i;
    uint32_t signal_count_array[MAX_WAIT_MULTIPLE];
    int j;
    bool keep_waiting = true;

    if(num_signals > MAX_WAIT_MULTIPLE) {
        ERROR_MESSAGE("Exceeded maximum number of wait signals[%d]", MAX_WAIT_MULTIPLE);
        return false;
    }

    if (wait_all) {
        // Wait for all signals to be set.
        uint64_t start_ms = 0;

        if (timeout_in_ms != CDI_INFINITE) {
            start_ms = CdiOsGetMilliseconds();
        }

        if (ret_signal_index_ptr) {
            *ret_signal_index_ptr = 1;
        }

        while (keep_waiting && return_val) {
            // Check if all signals are active.
            keep_waiting = false;
            for (i = 0; i < num_signals; i++) {
                if (!(signal_info_ptr_array[i]->signal_count & 1)) {
                    // Signal is not active, wait on it.
                    bool timed_out;
                    uint32_t new_timeout_ms = CDI_INFINITE;

                    if (timeout_in_ms != CDI_INFINITE) {
                        uint64_t time_run_ms = CdiOsGetMilliseconds() - start_ms;
                        if (time_run_ms > timeout_in_ms) {
                            new_timeout_ms = 0;
                        } else {
                            new_timeout_ms = timeout_in_ms - time_run_ms;
                        }
                    }
                    return_val = CdiOsSignalWait((CdiSignalType)signal_info_ptr_array[i], new_timeout_ms, &timed_out);
                    if (timed_out) {
                        if (ret_signal_index_ptr) {
                            *ret_signal_index_ptr = OS_SIG_TIMEOUT;
                        }
                        keep_waiting = false;
                        break;
                    }
                    keep_waiting = true;
                }
            }
        }
        // The pthread_mutex functions imply a memory barrier. If we don't wait we must do our own.
        __sync_synchronize();

        return return_val;
    }

    // First, see if any signals are active.
    for (i = 0; i < num_signals; i++) {
        signal_count_array[i] = signal_info_ptr_array[i]->signal_count;
        if (signal_count_array[i] & 1) {
            keep_waiting = false;
            if (NULL != ret_signal_index_ptr) {
                *ret_signal_index_ptr = i;
            }
            break;
        }
    }

    if (keep_waiting) {

        // No signals currently active, we need to wait for the first signal and register with the remaining signals.
        struct timespec sTime;
        if(timeout_in_ms != CDI_INFINITE) {
            GetTimeout(&sTime, timeout_in_ms, preferred_clock);
        }

        pthread_mutex_lock(&signal_info_ptr_array[0]->mutex);
        // Register with all the other signals. We use atomics to increment the signal counts and set a pointer to our
        // signal. This is done to avoid needing to grab locks, since that will introduce messey race conditions.
        //
        // We need to have the lock to signal0 so that none of these signals can call signal0's condition variable until
        // we are sleeping on it.
        for (i = 1; i < num_signals; i++) {
            int previous_count = __sync_fetch_and_add(&signal_info_ptr_array[i]->num_other_sigs, 1);
            if(previous_count >= MAX_THREADS_WAITING) {
                ERROR_MESSAGE("__sync_fetch_and_add too high");
                __sync_fetch_and_sub(&signal_info_ptr_array[i]->num_other_sigs, 1);
                return_val = false;
                keep_waiting = false;
            }
            for (j = 0; j < MAX_THREADS_WAITING; j++) {
                if (__sync_bool_compare_and_swap(&signal_info_ptr_array[i]->other_sigs_ptr_array[j], NULL, signal_info_ptr_array[0])) {
                    break;
                }
            }
            if (j == MAX_THREADS_WAITING) {
                ERROR_MESSAGE("MAX_THREADS_WAITING");
                __sync_fetch_and_sub(&signal_info_ptr_array[i]->num_other_sigs, 1);
                return_val = false;
                keep_waiting = false;
            }
        }

        while (keep_waiting) {
            // Check if a signal is set.
            for (i = 0; i < num_signals; i++) {
                if(signal_info_ptr_array[i]->signal_count != signal_count_array[i]) {
                    keep_waiting = false;
                    if(NULL != ret_signal_index_ptr) {
                        *ret_signal_index_ptr = i;
                    }
                    break;
                }
            }
            if (keep_waiting) {
                // No signal set, wait on the first condition variable.
                if(timeout_in_ms != CDI_INFINITE) {
                    int ret_code = pthread_cond_timedwait(&signal_info_ptr_array[0]->condition, &signal_info_ptr_array[0]->mutex, &sTime);
                    if (ret_code) {
                        if (ret_signal_index_ptr) {
                            *ret_signal_index_ptr = OS_SIG_TIMEOUT;
                        }
                        keep_waiting = false;
                        break;
                    }
                } else {
                    pthread_cond_wait(&signal_info_ptr_array[0]->condition, &signal_info_ptr_array[0]->mutex);
                }
            }
        }

        // Done with the lock.
        pthread_mutex_unlock(&signal_info_ptr_array[0]->mutex);

        // Remove the registrations using atomics to avoid locks.
        for (i = 1; i < num_signals; i++) {
            for (j = 0; j < MAX_THREADS_WAITING; j++) {
                if (__sync_bool_compare_and_swap(&signal_info_ptr_array[i]->other_sigs_ptr_array[j], signal_info_ptr_array[0], NULL)) {
                    __sync_fetch_and_sub(&signal_info_ptr_array[i]->num_other_sigs, 1);
                    break;
                }
            }
        }
    }
    else {
        // The pthread_mutex functions imply a memory barrier. If we don't wait we must do our own.
        __sync_synchronize();
    }

    return return_val;
}

// -- Memory --
void* CdiOsMemAlloc(int32_t mem_size)
{
    void* mem_ptr = malloc(mem_size);

    if (NULL == mem_ptr) {
        ERROR_MESSAGE("malloc failed");
    }

    return mem_ptr;
}

void* CdiOsMemAllocZero(int32_t mem_size)
{
    void* mem_ptr = CdiOsMemAlloc(mem_size);

    if (NULL != mem_ptr) {
        memset(mem_ptr, 0, mem_size);
    }

    return mem_ptr;
}

void CdiOsMemFree(void* mem_ptr)
{
    assert(NULL != mem_ptr);
    free(mem_ptr);
}

void* CdiOsMemAllocHugePage(int32_t mem_size)
{
	void* mem_ptr = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (mem_ptr == MAP_FAILED) {
        ERROR_MESSAGE("mmap failed. Try adding \"vm.nr_hugepages = 1024\" to /etc/sysctl.conf. Then \"sudo sysctl -p\"");
        mem_ptr = NULL;
    }

    return mem_ptr;
}

void CdiOsMemFreeHugePage(void* mem_ptr, int mem_size)
{
	munmap(mem_ptr, mem_size);
}

// -- File --
bool CdiOsOpenForWrite(const char* file_name_str, CdiFileID* file_handle_ptr)
{
    bool return_val = true;
    *file_handle_ptr = fopen(file_name_str, "w+b");

    if (*file_handle_ptr == NULL) {
        ERROR_MESSAGE("Open for write failed. Filename[%s]", file_name_str);
        return_val = false;
    }
    return return_val;
}

bool CdiOsOpenForRead(const char *file_name_str, CdiFileID* file_handle_ptr)
{
    bool return_val = true;

    *file_handle_ptr = fopen(file_name_str, "rb");

    if (*file_handle_ptr == NULL) {
        return_val = false;
        ERROR_MESSAGE("Open for read failed. Filename[%s]", file_name_str);
    }

    return return_val;
}

bool CdiOsClose(CdiFileID file_handle)
{
    int return_val = 0;

    if (file_handle != NULL) {
        return_val = fclose((FILE *)file_handle);
    }

    if (return_val != 0) {
        ERROR_MESSAGE("Close failed[%d]", return_val);
        return_val = false;
    }

    return (return_val == 0) ? true : false;
}

bool CdiOsRead(CdiFileID file_handle, void* buffer_ptr, uint32_t byte_count, uint32_t* bytes_read_ptr)
{
    bool return_val = true;
    size_t num_bytes_read = 0;

    if (buffer_ptr && file_handle) {
        num_bytes_read = fread(buffer_ptr, 1, byte_count, (FILE*)file_handle);

        if (0 == num_bytes_read) {
            // Don't return an error if at EOF and no bytes were read (same behavior as Windows).
            if (!feof((FILE *)file_handle)) {
                ERROR_MESSAGE("fread() failed. Zero bytes read and not at EOF");
                return_val = false;
            }
        }

    } else {
        return_val = false;

        if (NULL == buffer_ptr) {
            ERROR_MESSAGE("NULL buffer used for fread()");
        }

        if (NULL == file_handle) {
            ERROR_MESSAGE("No file handle provided to fread()");
        }
    }

    if (bytes_read_ptr) {
        *bytes_read_ptr = (uint32_t)num_bytes_read;
    }

    return return_val;
}

bool CdiOsWrite(CdiFileID file_handle, const void* data_ptr, uint32_t byte_count)
{
    if (file_handle != NULL) {
        uint32_t written = (uint32_t)fwrite(data_ptr, 1, (size_t)byte_count, (FILE*)file_handle);
        if (written != byte_count) {
            // Don't want to log an error to prevent possible recursive call to logger.
            return false;
        }
    }

    return true;
}

bool CdiOsFlush(CdiFileID file_handle)
{
    return fflush(file_handle) == 0;
}

bool CdiOsFTell(CdiFileID file_handle, uint64_t* current_position_ptr)
{
    int64_t pos = -1LL;

    if (file_handle != NULL) {
        pos = ftell(file_handle);

        if (current_position_ptr) {
            *current_position_ptr = (uint64_t)pos;
        }

    }

    return pos != -1LL;
}

bool CdiOsFSeek(CdiFileID file_handle, int64_t offset, int position)
{

    if (file_handle != NULL) {
        return fseek(file_handle, offset, position) == 0;
    }

    return false;
}

bool CdiOsSplitPath(const char* filepath_str, char* filename_str, int filename_buf_size, char* directory_str,
                    int directory_buf_size) {
    int i=0;
    int last_dir_char = -1;
    bool ret_val = true;

    // Search the filepath string to find the '/' marking the end of the directory path.
    if (NULL != filepath_str) {
        while (filepath_str[i] != '\0') {
            if (filepath_str[i] == '/') {
                last_dir_char = i;
            }
            i++;
        }
    } else {
        ret_val = false;
    }

    if (ret_val) {
        // If a filename_str buffer is provided then send everything after the last '/' to filename_str.
        if ((filename_str != NULL) && filename_buf_size) {
            // Initialize filename_str to null terminator.
            filename_str[0] = '\0';
            if (i < filename_buf_size) {
                int start_filename_char = last_dir_char + 1;
                CdiOsStrCpy(filename_str, i+1, filepath_str + start_filename_char);
            } else {
                // Filename string is larger than the buffer.
                ret_val = false;
            }
        }

        // If a directory_str is provided then everything before and including the last '/' is sent to the buffer.
        // If there is no '/' in the path then directory_str is set to a null string.
        if ((directory_str != NULL)  && directory_buf_size && ret_val) {
            // Initialize the directory_str with null terminator.
            directory_str[0] = '\0';

            // Check if there is a directory path to return.
            if (last_dir_char != -1) {
                // Add one for zero offset and add one for null terminator.
                int dir_string_size = last_dir_char + 2;

                if (dir_string_size <= directory_buf_size) {
                    CdiOsStrCpy(directory_str, dir_string_size, filepath_str);
                } else {
                    // Directory string is too big for directory buffer.
                    ret_val = false;
                }
            }
        }
    }

    return ret_val;
}

bool CdiOsIsPathWriteable(const char* directory_str)
{
    bool ret_val = true;
    DIR* dir = opendir(directory_str);

    // Verify that the directory exists, return false if doesn't exist.
    if (dir) {
        closedir(dir);

        // The access() function returns 0 on success.
        if (access(directory_str, W_OK)) {
            ERROR_MESSAGE("Directory [%s] does not have write permissions.", directory_str);
            ret_val = false;
        }
    } else {
        ERROR_MESSAGE("Directory [%s] does not exist.", directory_str);
        ret_val = false;
    }

    return ret_val;
}

// -- Utilities - Strings, Sleep --

int CdiOsStrCpy(char* dest_str, uint32_t max_str_len, const char* src_str)
{
    uint32_t i;

    for (i = 0; i<max_str_len; i++) {
        dest_str[i] = src_str[i];
        if (src_str[i] == 0)
            return i;
    }
    dest_str[max_str_len-1] = 0;

    return max_str_len-1;
}

void CdiOsSleep(uint32_t milliseconds)
{
    struct timespec tTimeOut;
    int temp_rc;

    tTimeOut.tv_sec = milliseconds / 1000;
    tTimeOut.tv_nsec = (milliseconds % 1000) * 1000000;
    temp_rc = nanosleep(&tTimeOut, NULL);

    if (temp_rc != 0) {
        // If EINTR, the thread has been woken up, probably by Ctrl-C, return early in this case.
    }
}

void CdiOsSleepMicroseconds(uint32_t microseconds)
{
    struct timespec tTimeOut;
    tTimeOut.tv_sec = microseconds / (1000 * 1000);
    tTimeOut.tv_nsec = (microseconds % (1000 * 1000)) * 1000;
    nanosleep(&tTimeOut, NULL);
}

uint64_t CdiOsGetMicroseconds()
{
    struct timespec time;

    if (clock_gettime(preferred_clock, &time) == -1) {
        ERROR_MESSAGE("Cannot get current time. clock_gettime() failed");
    }

    return (uint64_t)time.tv_sec * 1000000L + (time.tv_nsec / 1000L);
}

void CdiOsGetUtcTime(struct timespec* ret_time_ptr)
{
    clock_gettime(CLOCK_REALTIME, ret_time_ptr);
}

void CdiOsGetLocalTime(struct tm* local_time_ret_ptr)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, local_time_ret_ptr);
}

int CdiOsGetLocalTimeString(char* time_str, int max_string_len)
{
    struct timespec ts;
    struct tm local_time;
    uint32_t fractional;

    // Verify the OS has the correct timezone.
    tzset();

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &local_time);

    // The valid range of tv_nsec is [0, 999999999]
    // Get the valid milliseconds and microseconds from the tv_nsec value. For this simple implementation, truncation
    // instead of rounding will be used.

    // Remove the nanoseconds from the nanosecond field, leaving the milliseconds and microseconds.
    fractional = (ts.tv_nsec / 1000);

    // gmtoff is the number of seconds to add to the UTC to get local time. The GNU description of the tm struct can
    // be found here: https://www.gnu.org/software/libc/manual/html_node/Broken_002ddown-Time.html.
    char time_zone_str[MAX_FORMATTED_TIMEZONE_STRING_LENGTH] = {0};
    if (local_time.tm_gmtoff == 0) {
        snprintf(time_zone_str, MAX_FORMATTED_TIMEZONE_STRING_LENGTH, "Z");
    } else {
        int offset = local_time.tm_gmtoff / 3600;
        int mod = local_time.tm_gmtoff % 3600;
        int min_offset = 0;

        // Certain sections of India have a 30min offset.
        if (mod != 0) {
            min_offset = 30;
        }

        snprintf(time_zone_str, MAX_FORMATTED_TIMEZONE_STRING_LENGTH, "%+03d:%02d", offset, min_offset);
    }

    return snprintf(time_str, max_string_len, "[%.4d-%.2d-%.2dT%.2d:%.2d:%.2d.%06d%s] ",
                          (local_time.tm_year + 1900), (local_time.tm_mon + 1), local_time.tm_mday,
                          local_time.tm_hour, local_time.tm_min, local_time.tm_sec, fractional, time_zone_str);
}

// -- Sockets --
bool CdiOsSocketOpen(const char* host_address_str, int port_number, CdiSocket* new_socket_ptr)
{
    bool ret = false;

    // Create an Internet socket which will be used for writing or reading.
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd >= 0) {
        // Start with an address that can be used in either direction.
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port_number),
            .sin_addr = { 0 },
            .sin_zero = { 0 }
        };
        if (host_address_str == NULL) {
            // Bind to the specified port number on any interface.
            addr.sin_addr.s_addr = INADDR_ANY;
            const int rv = bind(fd, (struct sockaddr*)&addr, sizeof addr);
            if (rv == 0) {
                ret = true;
            } else {
                ERROR_MESSAGE("bind() failed[%d]", errno);
            }
        } else {
            // Convert the IP address from a string to a binary representation.
            const in_addr_t ip_addr = inet_addr(host_address_str);
            if (ip_addr != (in_addr_t)-1) {
                // Set the default destination host and port number for writing to the socket.
                addr.sin_addr.s_addr = ip_addr;
                if (connect(fd, (struct sockaddr*)&addr, sizeof addr) == 0) {
                    ret = true;
                }
            }
        }
        if (ret) {
            *new_socket_ptr = fd;
        } else {
            close(fd);
        }
    }

    return ret;
}

bool CdiOsSocketGetPort(CdiSocket s, int* port_number_ptr)
{
    if (port_number_ptr == NULL) {
        return false;
    }

    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(s, (struct sockaddr *)&sin, &len) != 0) {
        return false;
    } else {
        *port_number_ptr = ntohs(sin.sin_port);
        return true;
    }
}

bool CdiOsSocketClose(CdiSocket s)
{
    return close(s) == 0;
}

bool CdiOsSocketRead(CdiSocket s, void* buffer_ptr, int* byte_count_ptr)
{
    bool ret = true;

    // Only one file descriptor will be waited on.
    struct pollfd fdset = {
        .fd = s,
        .events = POLLIN
    };

    // Time out every 10 ms to check for shutdown.
    const int rv = poll(&fdset, 1, 10);
    if (rv > 0) {
        const size_t bytes_read = read(s, buffer_ptr, *byte_count_ptr);
        if (bytes_read <= 0) {
            ret = false;
        } else {
            *byte_count_ptr = bytes_read;
        }
    } else if (rv == 0) {
        // Timed out.
        *byte_count_ptr = 0;
    } else {
        // Error occurred.
        ret = false;
    }

    return ret;
}

bool CdiOsSocketWrite(CdiSocket s, struct iovec* iov, int iovcnt, int* byte_count_ptr)
{
    // Send the packet via the socket.
    const struct msghdr msg = {
        .msg_iov = iov,
        .msg_iovlen = iovcnt
    };
    const ssize_t rv = sendmsg(s, &msg, 0);
    // Partial socket writes cannot occur here because the O_NONBLOCK attribute was never used when opening the
    // socket's descriptor.  So, simply check to see that at least 1 byte was sent and that no error occurred (-1 return
    // code).
    if (rv > 0) {
        *byte_count_ptr = rv;
        return true;
    } else {
        return false;
    }
}

bool CdiOsEnvironmentVariableSet(const char* name_str, const char* value_str)
{
    if (NULL == value_str) {
        // Linux allows NULL for the value but windows uses it to remove the variable. We want both variants to have the
        // same functionality (set a value), so return a failure here.
        return false;
    }
    return 0 == setenv(name_str, value_str, 1); // 1= overwrite existing value
}

void CdiOsShutdown()
{
    // Nothing to do in Linux.
}
