// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the Windows definitions for OS functions for creating/deleting threads, semaphores, mutexes, and
 * also for sleeps and string copies.
 * NOTE: This file is excluded from the linux build's Doxygen parsing (see the Makefile), so no need to use Doxygen
 * style commenting in it.
 */

// Windows specific #defs
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_os_api.h"

#include <assert.h>
#include <crtdbg.h>
#include <errno.h>
#include <io.h>
#include <Mmsystem.h>
#include <malloc.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>
#include <sys/uio.h>
#include <windows.h>
#include <ws2def.h>
#include <ws2tcpip.h>

#include "cdi_logger_api.h"
#include "utilities_api.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Maximum number of signals supported when using CdiOsSignalsWait().
#define MAX_WAIT_SIGNALS    (50)

typedef struct CdiThreadInfo CdiThreadInfo;
struct CdiThreadInfo
{
    DWORD thread_id;                        // Thread identifier.
    HANDLE thread_handle;                   // Handle to thread.
    char  thread_name_str[CDI_MAX_THREAD_NAME]; // Name of thread (used for informational purposes only).

    /// Thread function that will be used in ThreadFuncHelper().
    CdiThreadFuncName thread_func;
    /// The argument given to thread_func.
    void*          thread_func_arg_ptr;
    /// Signal used to start the thread. If NULL, thread starts immediately.
    CdiSignalType  start_signal;
    /// If non-zero, CdiOsThreadJoin() has been called to wait for the thread to exit.
    int exit;
};

typedef struct SemInfo SemInfo;
struct SemInfo
{
    HANDLE    sem_id;                     // Semaphore ID.
    int      sem_count;                   // Current semaphore count.
    int      sem_total;                   // Total number of semaphores.
};

typedef struct SignalInfo SignalInfo;
struct SignalInfo
{
    HANDLE  event_handle;       // Windows event handle from CreateEventA().
    volatile bool signal_state; // Variable used to hold current signal state. Allows read access without using any
                                // OS resources.
};

/// @brief Forward declaration to create pointer to socket info when used.
typedef struct SocketInfo SocketInfo;
/**
 * @brief Structure used to hold semaphore state data.
 */
struct SocketInfo
{
    SOCKET s; ///< Socket descriptor
    struct sockaddr_in addr; ///< Socket address info
};

/// @brief Macro used within this file to handle generation of error messages with OS message either to the logger or
/// stderr.
#define LAST_ERROR_MESSAGE(...) LastErrorMessage(__FUNCTION__, __LINE__, __VA_ARGS__)

/// @brief Macro used within this file to handle generation of error messages either to the logger or stderr.
#define ERROR_MESSAGE(...) ErrorMessage(__FUNCTION__, __LINE__, __VA_ARGS__)

/// Maximum length of a single formatted message string.
#define MAX_FORMATTED_MESSAGE_LENGTH    (1024)

/// Max filename length supported by windows _splitpath().
#define MAX_FILENAME_LENGTH             (256)
/// Max file extension length supported by windows _splitpath().
#define MAX_FILE_EXTENSION_LENGTH       (256)
/// Max directory path length supported by windows _splitpath().
#define MAX_DIRECTORY_PATH_LENGTH       (256)
/// Max drive name string supported by windows.
#define MAX_DRIVE_LENGTH                (3)

/// @brief Declare a windows API function that resides within ntdll.lib.
extern int NtQueryTimerResolution(PULONG MinimumResolution, PULONG Maximum_Resolution, PULONG ActualResolution);

/// @brief Statically allocated mutex used to make initialization of profile data thread-safe.
static CdiStaticMutexType mutex_lock = CDI_STATIC_MUTEX_INITIALIZER;

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

// Needed for Winsock2
WSADATA wsaData;

/// Array of data used to hold signal handlers.
static CdiSignalHandlerInfo signal_handler_function_array[CDI_MAX_SIGNAL_HANDLERS];

/// Number of signal handlers in signal_handler_function_array.
static int signal_handler_count = 0;

/// If true, the CDI logger will be used to generate error messages, otherwise output will be sent to stderr.
static bool use_logger = false;

/// State of winsock initialization.
static bool winsock_initialized = false;

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * Generate error message and send to logger or stderr.
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

    vsprintf_s(msg_buffer_str, sizeof(msg_buffer_str), format_str, val);
    if (use_logger) {
        CdiLogger(CdiLoggerThreadLogGet(), kLogComponentGeneric, kLogError, func_name_str, line, msg_buffer_str);
    } else {
        fprintf(stderr, "[%s:%d] ERROR: %s.\r\n", func_name_str, line, msg_buffer_str);
    }

    va_end(val);
}

/**
 * Print error message with last OS error as a string. This original code came from:
 * https://docs.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
 */
static void LastErrorMessage(const char* func_name_str, int line, const char* format_str, ...)
{
    char system_msg_buffer_str[MAX_FORMATTED_MESSAGE_LENGTH];
    system_msg_buffer_str[0] = '\0';
    DWORD last_error_code = GetLastError();

    // NOTE: This message will contain the ending CR/LF ('\r\n').
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, last_error_code, 0, system_msg_buffer_str,
                  sizeof(system_msg_buffer_str), 0);

    // Don't want to use the ending CR/LF.
    size_t string_length = strlen(system_msg_buffer_str);
    if (string_length >= 2) {
        string_length -= 2;
    }

    char msg_buffer_str[MAX_FORMATTED_MESSAGE_LENGTH];
    msg_buffer_str[0] = '\0';
    va_list val;
    va_start(val, format_str);

    vsprintf_s(msg_buffer_str, sizeof(msg_buffer_str), format_str, val);
    if (use_logger) {
        CdiLogger(CdiLoggerThreadLogGet(), kLogComponentGeneric, kLogError, func_name_str, line, "%s. LastError[%s].",
            msg_buffer_str, system_msg_buffer_str);
    } else {
        fprintf(stderr, "[%s:%d] ERROR: %s. LastError[%s].\r\n", func_name_str, line, msg_buffer_str,
                system_msg_buffer_str);
    }

    va_end(val);
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
    for (int i = 0; i < CDI_MAX_SIGNAL_HANDLERS; i++) {
        signal(signal_handler_function_array[i].signal_num, (_crt_signal_t)signal_handler_function_array[i].func_ptr);
    }

    if (thread_info_ptr->start_signal) {
        CdiOsSignalWait(thread_info_ptr->start_signal, CDI_INFINITE, NULL);
    }

    // No need to start thread if we are already waiting for it to exit via CdiOsThreadJoin().
    if (0 == CdiOsAtomicRead32(&thread_info_ptr->exit)) {
        (thread_info_ptr->thread_func)(thread_info_ptr->thread_func_arg_ptr);
    }

    return NULL;
}

static bool ThreadDetach(CdiThreadID thread_id)
{
    DWORD thread_flags;
    bool return_val = true;
    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_id;

    assert(thread_id != NULL);

    if (GetHandleInformation(thread_info_ptr->thread_handle, &thread_flags)) {
        CloseHandle(thread_info_ptr->thread_handle);
    }

    CdiOsMemFree(thread_info_ptr);

    return return_val;
}

/**
 *  Converts SYSTEMTIME to time_t.
 *
 *  @param time_sys_ptr The pointer to the SYSTEMTIME struct that will be converted to time_t struct.
 *
 *  return time_t struct.
 */
static time_t ConvertSystemTime(SYSTEMTIME* time_sys_ptr)
{
    struct tm tm;
    tm.tm_sec = time_sys_ptr->wSecond;
    tm.tm_min = time_sys_ptr->wMinute;
    tm.tm_hour = time_sys_ptr->wHour;
    tm.tm_mday = time_sys_ptr->wDay;
    tm.tm_mon = time_sys_ptr->wMonth - 1;
    tm.tm_year = time_sys_ptr->wYear - 1900;
    tm.tm_isdst = -1;

    time_t time_ret = mktime(&tm);

    return time_ret;
}

/**
 * Initializes the Winsock2 library. Call this at the start of any function that creates a new communications socket.
 * The function ensures that the library is only really inialized once.
 */
static bool InitializeWinsock(void)
{
    CdiOsStaticMutexLock(mutex_lock);
    if (!winsock_initialized) {
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            ERROR_MESSAGE("WSAStartup failed. Returned[%d]", iResult);
        }
        winsock_initialized = true;
    }
    CdiOsStaticMutexUnlock(mutex_lock);
    return winsock_initialized;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

void CdiOsUseLogger(void)
{
    use_logger = true;
}

// -- Threads --
bool CdiOsSignalHandlerSet(int signal_num, CdiSignalHandlerFunction func_ptr)
{
    bool ret = true;

    if (signal_handler_count >= CDI_MAX_SIGNAL_HANDLERS) {
        return false;
    }

    signal_handler_function_array[signal_handler_count].signal_num = signal_num;
    signal_handler_function_array[signal_handler_count].func_ptr = func_ptr;

    signal_handler_count++;

    return ret;
}

bool CdiOsThreadCreatePinned(CdiThreadFuncName thread_func, CdiThreadID* thread_id_out_ptr, const char* thread_name_str,
                             void* thread_func_arg_ptr, CdiSignalType start_signal, int cpu_affinity)
{
    bool return_val = true;

    assert(NULL != thread_id_out_ptr);
    assert(NULL != thread_func);

    // create a new thread record
    CdiThreadInfo* thread_info_ptr = CdiOsMemAllocZero(sizeof(CdiThreadInfo));
    if (NULL != thread_info_ptr) {

        thread_info_ptr->thread_func = thread_func;
        thread_info_ptr->thread_func_arg_ptr = thread_func_arg_ptr;
        thread_info_ptr->start_signal = start_signal;

        thread_info_ptr->thread_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadFuncHelper,
                                                      thread_info_ptr, 0, &thread_info_ptr->thread_id);
        if (NULL == thread_info_ptr->thread_handle) {
            CdiOsMemFree(thread_info_ptr);
            LAST_ERROR_MESSAGE("CreateThread failed");
            return_val = false;
        }
    }
    *thread_id_out_ptr = (CdiThreadID)thread_info_ptr;

    if (return_val && cpu_affinity >= 0) {
        // Set the newly created thread's CPU affinity if one was specified.
        PROCESSOR_NUMBER ideal_processor = { 0 };
        const int group_count = GetActiveProcessorGroupCount();
        int accumulator = 0;
        bool found = false;
        for (int i = 0; !found && (i < group_count); i++) {
            const int group_size = GetActiveProcessorCount(i);
            if (accumulator + group_size > cpu_affinity) {
                ideal_processor.Group = i;
                ideal_processor.Number = cpu_affinity - accumulator;
                found = true;
            } else {
                accumulator += group_size;
            }
        }

        if (found) {
            return_val = SetThreadIdealProcessorEx(thread_info_ptr->thread_handle, &ideal_processor, NULL);
            if (!return_val) {
                ERROR_MESSAGE("SetThreadIdealProcessorEx failed. Core[%d] not available?", cpu_affinity);
            }
        } else {
            return_val = false;
        }
    }

    if (return_val) {
        if (NULL != thread_name_str) {
            strcpy_s(thread_info_ptr->thread_name_str, CDI_MAX_THREAD_NAME, thread_name_str);
        }
        else {
            thread_info_ptr->thread_name_str[0] = '\0';
        }
    } else {
        *thread_id_out_ptr = NULL;
    }

    return return_val;
}

bool CdiOsThreadAllocData(CdiThreadData* handle_out_ptr)
{
    CdiThreadData handle = TlsAlloc();
    if (TLS_OUT_OF_INDEXES == handle)
        return false;

    *handle_out_ptr = handle;
    return true;
}

bool CdiOsThreadFreeData(CdiThreadData handle)
{
    if (TlsFree(handle)) {
        return true;
    }
    return false;
}

bool CdiOsThreadSetData(CdiThreadData handle, void* content_ptr)
{
    if (TlsSetValue(handle, content_ptr)) {
        return true;
    }
    return false;
}

bool CdiOsThreadGetData(CdiThreadData handle, void** content_out_ptr)
{
    *content_out_ptr = TlsGetValue(handle);

    if (ERROR_SUCCESS != GetLastError()) {
        return false;
    }

    return true;
}

const char* CdiOsThreadGetName(CdiThreadID thread_id)
{
    assert(thread_id != NULL);

    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_id;
    return thread_info_ptr->thread_name_str;
}

bool CdiOsThreadJoin(CdiThreadID thread_id, uint32_t timeout_in_ms, bool* timed_out_ptr)
{
    DWORD wait_rc;
    bool return_val = true;
    CdiThreadInfo* thread_info_ptr = (CdiThreadInfo*)thread_id;

    assert(thread_id != NULL);

    if (timed_out_ptr) {
        *timed_out_ptr = false;
    }

    CdiOsAtomicInc32(&thread_info_ptr->exit); // Ensure value is non-zero.
    if (thread_info_ptr->start_signal) {
        CdiOsSignalSet(thread_info_ptr->start_signal);
    }
    wait_rc = WaitForSingleObject(thread_info_ptr->thread_handle, timeout_in_ms);

    if (WAIT_ABANDONED == wait_rc) {
        //should never happen with thread handles, but just in case...
        LAST_ERROR_MESSAGE("Thread join[%s] exited with WAIT_ABANDONED", thread_info_ptr->thread_name_str);
        return_val = false;
    } else if (WAIT_TIMEOUT == wait_rc) {
        LAST_ERROR_MESSAGE("Thread join[%s] exited with WAIT_TIMEOUT", thread_info_ptr->thread_name_str);
        if (timed_out_ptr) {
            *timed_out_ptr = true;
        }
        return_val = false;
    } else if (WAIT_FAILED == wait_rc) {
        LAST_ERROR_MESSAGE("Wait failed");
        return_val = false;
    }

    if (return_val) {
        return_val = ThreadDetach(thread_id);
    }

    return return_val;
}

// -- Semaphores --
bool CdiOsSemaphoreCreate(CdiSemID* sem_info_out_ptr, int sem_count)
{
    bool return_val = true;
    SemInfo* sem_info_ptr;
    int i;

    assert(sem_info_out_ptr != NULL);
    assert(sem_count >= 0);

    *sem_info_out_ptr = NULL;

    // create a new thread record
    sem_info_ptr = CdiOsMemAllocZero(sizeof(SemInfo));
    if (return_val) {
        sem_info_ptr->sem_id = CreateSemaphore(NULL, sem_count, sem_count, NULL);
        if (NULL == sem_info_ptr->sem_id) {
            CdiOsMemFree(sem_info_ptr);
            return_val = false;
            LAST_ERROR_MESSAGE("CreateSemaphore failed");
        } else {
            sem_info_ptr->sem_total = sem_count;
            sem_info_ptr->sem_count = sem_count;

            *sem_info_out_ptr = (CdiSemID)sem_info_ptr;
        }
    }

    return return_val;
}

bool CdiOsSemaphoreDelete(CdiSemID sem_ptr)
{
    bool return_val = true;

    if (sem_ptr) {
        SemInfo* sem_info_ptr = (SemInfo*)sem_ptr;

        if (CloseHandle(sem_info_ptr->sem_id)) {
            // release semaphore info memory
            CdiOsMemFree(sem_info_ptr);
        } else {
            LAST_ERROR_MESSAGE("CloseHandle failed");
            return_val = false;
        }
    }

    return return_val;
}

bool CdiOsSemaphoreRelease(CdiSemID sem_ptr)
{
    bool return_val = true;
    SemInfo* sem_info_ptr = (SemInfo*)sem_ptr;
    assert(sem_ptr != NULL);

    return_val = ReleaseSemaphore(sem_info_ptr->sem_id, 1, NULL);
    if (!return_val) {
        // Allowed to release more than reserve.
        if (ERROR_TOO_MANY_POSTS != GetLastError()) {
            LAST_ERROR_MESSAGE("ReleaseSemphore failed");
        }
    }

    return return_val;
}

bool CdiOsSemaphoreReserve(CdiSemID sem_ptr, int timeout_in_ms)
{
    DWORD wait_rc = WAIT_OBJECT_0;
    SemInfo* sem_info_ptr = (SemInfo*)sem_ptr;
    assert(sem_ptr != NULL);

    wait_rc = WaitForSingleObject(sem_info_ptr->sem_id, (DWORD)timeout_in_ms);
    if (WAIT_OBJECT_0 != wait_rc) {
        LAST_ERROR_MESSAGE("WaitForSingleObject failed");
    }
    return (WAIT_OBJECT_0 == wait_rc) ? true : false;
}

/// CdiOsSemaphoreValueGet not available in windows; always returns 0.
int CdiOsSemaphoreValueGet(CdiSemID sem_ptr)
{
    return 0;
}

bool CdiOsCritSectionCreate(CdiCsID* cs_handle_ptr)
{
    assert(NULL != cs_handle_ptr);
    bool return_val = true;

    *cs_handle_ptr = (CdiCsID)CdiOsMemAllocZero(sizeof(CRITICAL_SECTION));
    if (NULL == *cs_handle_ptr) {
        LAST_ERROR_MESSAGE("Failed to allocate memory for critical section");
        return_val = false;
    } else {
        if (!InitializeCriticalSectionAndSpinCount(*cs_handle_ptr, 0x100)) {
            return_val = false;
            LAST_ERROR_MESSAGE("InitializeCriticalSectionAndSpinCount failed");
        }
    }

    return return_val;
}

void CdiOsCritSectionReserve(CdiCsID cs_handle)
{
    assert(NULL != cs_handle);
    EnterCriticalSection(cs_handle);
}

void CdiOsCritSectionRelease(CdiCsID cs_handle)
{
    assert(NULL != cs_handle);
    LeaveCriticalSection(cs_handle);
}

bool CdiOsCritSectionDelete(CdiCsID cs_handle)
{
    if (NULL != cs_handle) {
        DeleteCriticalSection(cs_handle);
        CdiOsMemFree(cs_handle);
    }
    return true;
}

// -- Signals --
/**
 *  This function creates a signal.  The initial value is not signaled. Will return the handle to any existing signal
 *  with this name. Special note:  this function used to be exposed in the API, but was found difficult to implement on
 *  LINUX.  Use CdiosNamedSemaphoreCreate instead.
 *
 *  @param   signal_handle_ptr  pointer to a signal handle to return
 *  @param   signal_name_str    Name in the global namespace of the signal.
 *
 *  @return  standard return value
 */
bool CdiOsSignalCreateNamed(CdiSignalType* signal_handle_ptr, char* signal_name_str)
{
    bool return_val = true;
    char max_len_signal_name_str[ MAX_PATH ] = { 0 };
    assert(NULL != signal_handle_ptr);

    if (signal_name_str) {
        _snprintf_s(max_len_signal_name_str, MAX_PATH-1, MAX_PATH-1, "cdiNamedSig_%s", signal_name_str);
        max_len_signal_name_str[ MAX_PATH-1 ] = 0;
        signal_name_str = max_len_signal_name_str;
    }

    SignalInfo* signal_info_ptr = CdiOsMemAllocZero(sizeof(SignalInfo));
    if (NULL == signal_info_ptr) {
        return_val = false;
        ERROR_MESSAGE("Failed to allocate memory");
    } else {
        signal_info_ptr->event_handle = CreateEventA(NULL, true, false, signal_name_str);
        if (NULL == signal_info_ptr->event_handle) {
            if (signal_name_str) {
                LAST_ERROR_MESSAGE("CreateEventA failed. Name[%s]", signal_name_str);
            }
            else {
                LAST_ERROR_MESSAGE("CreateEventA failed");
            }
            CdiOsMemFree(signal_info_ptr);
            signal_info_ptr = NULL;
            return_val = false;
        }
    }

    *signal_handle_ptr = (CdiSignalType)signal_info_ptr;

    return return_val;
}

bool CdiOsSignalCreate(CdiSignalType* signal_handle_ptr)
{
    return(CdiOsSignalCreateNamed(signal_handle_ptr, NULL));
}

bool CdiOsSignalDelete(CdiSignalType signal_handle)
{
    bool return_val = true;
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    if (signal_info_ptr) {
        if (signal_info_ptr->event_handle) {
            return_val = CloseHandle(signal_info_ptr->event_handle);
            if (!return_val) {
                LAST_ERROR_MESSAGE("CloseHandle failed");
            }
        }
        CdiOsMemFree(signal_info_ptr);
    }

    return return_val;
}

bool CdiOsSignalClear(CdiSignalType signal_handle)
{
    bool return_val;
    assert(NULL != signal_handle);
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    return_val = ResetEvent(signal_info_ptr->event_handle);
    if (!return_val) {
        LAST_ERROR_MESSAGE("ResetEvent failed");
    } else {
        CdiOsAtomicStore32(&signal_info_ptr->signal_state, false);
    }
    return return_val;
}

bool CdiOsSignalSet(CdiSignalType signal_handle)
{
    bool return_val;
    assert(NULL != signal_handle);
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    return_val = SetEvent(signal_info_ptr->event_handle);
    if (!return_val) {
        LAST_ERROR_MESSAGE("SetEvent failed");
    } else {
        CdiOsAtomicStore32(&signal_info_ptr->signal_state, true);
    }
    return return_val;
}

bool CdiOsSignalGet(CdiSignalType signal_handle)
{
    DWORD temp_rc;
    bool return_val = true;
    assert(NULL != signal_handle);
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    temp_rc = WaitForSingleObject(signal_info_ptr->event_handle, 0);
    switch (temp_rc)
    {
        case WAIT_OBJECT_0:
            return_val = true;
            break;

        case WAIT_TIMEOUT:
            return_val = false;
            break;

        // This case is not supported yet.
        case WAIT_ABANDONED_0:
        // An error occurred. Signal is probably not initialized
        default:
            LAST_ERROR_MESSAGE("");
            return_val = false;
    }

    return return_val;
}

bool CdiOsSignalReadState(CdiSignalType signal_handle)
{
    assert(NULL != signal_handle);
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;
    return CdiOsAtomicLoad32(&signal_info_ptr->signal_state);
}

bool CdiOsSignalWait(CdiSignalType signal_handle, uint32_t timeout_in_ms, bool* timed_out_ptr)
{
    bool return_val = true;
    DWORD wait_rc;
    assert(NULL != signal_handle);
    SignalInfo* signal_info_ptr = (SignalInfo*)signal_handle;

    wait_rc = WaitForSingleObject(signal_info_ptr->event_handle, timeout_in_ms);

    if (WAIT_FAILED == wait_rc) {
        LAST_ERROR_MESSAGE("WaitForSingleObject failed");
        return_val = false;
    } else if (timed_out_ptr) {
        *timed_out_ptr = (wait_rc == WAIT_TIMEOUT);
    }
    return return_val;
}

bool CdiOsSignalsWait(CdiSignalType *signal_array, uint8_t num_signals, bool wait_all, uint32_t timeout_in_ms,
                      uint32_t *ret_signal_index_ptr) {
    assert(NULL != signal_array);

    bool return_val = true;
    DWORD dwWaitResult = 0;
    SignalInfo** signal_info_ptr = (SignalInfo**)signal_array;
    HANDLE signal_handle_array[MAX_WAIT_SIGNALS];

    for (int i = 0; i < num_signals; i++) {
        signal_handle_array[i] = signal_info_ptr[i]->event_handle;
    }

    dwWaitResult = WaitForMultipleObjects(num_signals, signal_handle_array, wait_all, timeout_in_ms);

    if (dwWaitResult >= WAIT_OBJECT_0 && dwWaitResult <= (WAIT_OBJECT_0 + num_signals - 1)) {
        if (ret_signal_index_ptr) {
            if (wait_all) {
                *ret_signal_index_ptr = 1;
            } else {
                *ret_signal_index_ptr = dwWaitResult - WAIT_OBJECT_0;
            }
        }
    } else {
        switch (dwWaitResult) {
        case WAIT_TIMEOUT:
            if (ret_signal_index_ptr) {
                *ret_signal_index_ptr = CDI_OS_SIG_TIMEOUT;
            }
            break;

            // This case is not supported yet.
        case WAIT_ABANDONED_0:
            // An error occurred. Usually means that one of the signals is not initialized.
        default:
            LAST_ERROR_MESSAGE("While waiting for multiple signals");
            return_val = false;
        }
    }

    return return_val;
}

// -- Memory --
void* CdiOsMemAlloc(int32_t mem_size)
{
    void* mem_ptr = _aligned_malloc(mem_size, 16); // always align to 16 bytes

    if (NULL == mem_ptr) {
        LAST_ERROR_MESSAGE("_aligned_malloc failed");
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
    _aligned_free(mem_ptr);
}

void* CdiOsMemAllocHugePage(int32_t mem_size)
{
    return NULL; // Not implemented on windows.
}

void CdiOsMemFreeHugePage(void* mem_ptr, int mem_size)
{
    // Nothing needed, unless logic in alloc is implemented.
}

// -- File --
bool CdiOsOpenForWrite(const char *file_name_str, CdiFileID *file_handle_ptr)
{
    const uint32_t path_len = MAX_PATH * sizeof(char) ;
    bool return_val = true;
    CdiFileID file_handle;

    file_handle = CreateFile(file_name_str, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);

    if (INVALID_HANDLE_VALUE == file_handle) {
        ERROR_MESSAGE("Cannot open file[%s] for writing", file_name_str);
        return_val = false;
    } else {
        *file_handle_ptr = file_handle;
    }

    return return_val;
}

bool CdiOsOpenForRead(const char *file_name_str, CdiFileID *file_handle_ptr)
{
    bool return_val = true;

    // Using FILE_SHARE_WRITE here allows a file that is already opened for writing to be opened here for reading.
    *file_handle_ptr = CreateFile(file_name_str, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (INVALID_HANDLE_VALUE == *file_handle_ptr) {
        ERROR_MESSAGE("Cannot open file[%s] for reading", file_name_str);
        return_val = false;
    }
    return return_val;
}

bool CdiOsClose(CdiFileID file_handle)
{
    bool return_val = true;
    if (CDI_STDOUT == file_handle || CDI_STDIN == file_handle || CDI_STDERR == file_handle) {
        // oops caller error, nothing to do, just continue
    } else if (!CloseHandle(file_handle)) {
        LAST_ERROR_MESSAGE("CloseHandle failed");
        return_val = false;
    }
    return return_val;
}

/* Microsoft win32 documentation for WriteFile
BOOL WriteFile(
    HANDLE       hFile,                  // A handle to the file or I/O device (for example, a file, file stream,
                                         // physical disk, volume, console buffer, tape drive, socket, communications
                                         // resource, mailslot, or pipe).
    LPCVOID      lpBuffer,               // A pointer to the buffer containing the data to be written to the file or
                                         // device.
    DWORD        nNumberOfBytesToWrite,  // The number of bytes to be written to the file or device.
    LPDWORD      lpNumberOfBytesWritten, // A pointer to the variable that receives the number of bytes written when
                                         // using a synchronous hFile parameter.
    LPOVERLAPPED lpOverlapped            // A pointer to an OVERLAPPED structure is required if the hFile parameter was
                                         // opened with FILE_FLAG_OVERLAPPED, otherwise this parameter can be NULL.
);
*/
bool CdiOsWrite(CdiFileID file_handle, const void* data_ptr, uint32_t byte_count)
{
    bool return_val = true;
    bool status;
    DWORD bytes_written = 0; // temporary holding area to satisfy WriteFile

    if (byte_count != 0) {
        status = WriteFile(file_handle, data_ptr, byte_count, &bytes_written, NULL);
        if (!status || (bytes_written != byte_count))
        {
            ERROR_MESSAGE("WriteFile failed. Byte Count[%d]. Bytes Written[%d]", byte_count, bytes_written);
            return_val = false;
        }
    }
    return return_val;
}

bool CdiOsFlush(CdiFileID file_handle)
{
    return FlushFileBuffers(file_handle);
}

/* Microsoft win32 documentation for ReadFile
BOOL ReadFIle(
    HANDLE       hFile,                // A handle to the file or I/O device (for example, a file, file stream,
                                       // physical disk, volume, console buffer, tape drive, socket, communications
                                       // resource, mailslot, or pipe).
    LPCVOID      lpBuffer,             // A pointer to the buffer containing the data to be written to the file or
                                       // device.
    DWORD        nNumberOfBytesToRead, // The number of bytes to read from the file or device.
    LPDWORD      lpNumberOfBytesRead,  // A pointer to the variable that receives the number of bytes read when using a
                                       // synchronous hFile parameter.
    LPOVERLAPPED lpOverlapped          // A pointer to an OVERLAPPED structure is required if the hFile parameter was
                                       // opened with FILE_FLAG_OVERLAPPED, otherwise this parameter can be NULL.
*/
bool CdiOsRead(CdiFileID file_handle, void* buffer_ptr, uint32_t byte_count, uint32_t* bytes_read_ptr)
{
    bool return_val = true;
    bool status;
    DWORD bytes_read = 0; // temporary holding area to satisfy ReadFile

    if (file_handle && buffer_ptr) {
        status = ReadFile(file_handle, buffer_ptr, byte_count, &bytes_read, NULL);

        if (!status || (bytes_read != byte_count)) {
            ERROR_MESSAGE("ReadFile failed. Byte Count[%d]. Bytes Read[%d]", byte_count, bytes_read);
            return_val = false;
        }
    } else {
        return_val = false;

        if (NULL == buffer_ptr) {
            ERROR_MESSAGE("NULL buffer used for ReadFile()");
        }

        if (NULL == file_handle) {
            ERROR_MESSAGE("No file handle provided to ReadFile()");
        }
    }

    // Check to see if user sent in a valid pointer to the number of bytes read if they did, then they must want to know
    // what happened so we will update it.
    if (bytes_read_ptr) {
        *bytes_read_ptr = bytes_read;
    }

    return return_val;
}

bool CdiOsFTell(CdiFileID file_handle, uint64_t* current_position_ptr)
{
    bool ret = true;
    LARGE_INTEGER   li;
    assert(NULL != current_position_ptr);

    li.QuadPart = 0;
    li.LowPart = SetFilePointer(file_handle, 0, &li.HighPart, FILE_CURRENT);

    if (INVALID_SET_FILE_POINTER == li.LowPart) {
        ERROR_MESSAGE("SetFilePointer() failed");
        ret = false;
    }

    if (current_position_ptr) {
        *current_position_ptr = li.QuadPart;
    }

    return ret;
}

bool CdiOsFSeek(CdiFileID file_handle, int64_t offset, int position)
{
    bool ret = true;
    LONG lOffsetHi = (LONG)(offset >> 32);

    if (INVALID_SET_FILE_POINTER == SetFilePointer(file_handle, (long)offset, &lOffsetHi, position)) {
        ERROR_MESSAGE("SetFilePointer() failed");
        ret = false;
    }

    return ret;
}

bool CdiOsSplitPath(const char* filepath_str, char* filename_str, int filename_buf_size, char* directory_str,
                    int directory_buf_size) {
    char dir[MAX_DIRECTORY_PATH_LENGTH];
    char drive[MAX_DRIVE_LENGTH];
    char ext[MAX_FILE_EXTENSION_LENGTH];
    bool ret_val = 0 == _splitpath_s(filepath_str, drive, MAX_DRIVE_LENGTH, dir, MAX_DIRECTORY_PATH_LENGTH,
                                     filename_str, filename_buf_size, ext, MAX_FILE_EXTENSION_LENGTH);

    // Appending file extension to filename string.
    if ((filename_str != NULL) && ret_val) {
        ret_val = 0 == strncat_s(filename_str, filename_buf_size, ext, MAX_FILE_EXTENSION_LENGTH);
    }

    // Copy drive string to directory_str and then append directory path string to the drive string.
    if ((directory_str != NULL) && ret_val) {
        CdiOsStrCpy(directory_str, directory_buf_size, drive);
        ret_val = 0 == strncat_s(directory_str, directory_buf_size, dir, MAX_DIRECTORY_PATH_LENGTH);
    }

    return ret_val;
}

bool CdiOsIsPathWriteable(const char* directory_str)
{
    bool ret = true;
    DWORD dwAttrib = GetFileAttributes(directory_str);

    // First, verify directory exists.
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        ERROR_MESSAGE("Directory [%s] does not exist.", directory_str);
        ret = false;
    }

    // Next, verify the directory is writeable by attempting to create a temp file in user-provided directory.
    if (ret) {
        CdiFileID file_handle = 0;
        char temp_file_string[CDI_MAX_LOG_FILENAME_LENGTH] = { 0 };
        snprintf(temp_file_string, sizeof(temp_file_string), "%s\\%s", directory_str, "_tmp_");

        file_handle = CreateFile(temp_file_string, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE == file_handle) {
            ERROR_MESSAGE("Directory [%s] does not have write permissions.", directory_str);
            ret = false;
        } else {
            CloseHandle(file_handle);
            DeleteFile(temp_file_string);
        }
    }

    return ret;
}


// -- Utilities - Strings, Sleep --
int CdiOsStrCpy(char* dest_str, uint32_t max_str_len, const char* src_str)
{
    uint32_t i;
    for (i=0; i<max_str_len; i++) {
        dest_str[i] = src_str[i];
        if (src_str[i] == 0)
            return i;
    }
    dest_str[max_str_len-1] = 0;
    return max_str_len-1;
}

void CdiOsSleep(uint32_t milliseconds)
{
    CdiOsSleepMicroseconds(milliseconds*1000);
}

void CdiOsSleepMicroseconds(uint32_t microseconds)
{
    uint64_t start_time = CdiOsGetMicroseconds();
    uint64_t end_time = start_time + microseconds;

    // Values are returned in 100 nanoseconds units.
    LONG min, max, resolution;
    NtQueryTimerResolution(&min, &max, &resolution);

    resolution *= 2;    // Adjust timer resolution to ensure wait doesn't run over.
    microseconds *= 10; // Convert to 100 nanosecond units.
    if (microseconds > resolution) {
        // Use a waitable timer to wait for the portion of the desired sleep time that is longer than
        // the system resolution.
        HANDLE timer_handle = CreateWaitableTimer(NULL, TRUE, NULL);
        if (NULL == timer_handle) {
            LAST_ERROR_MESSAGE("CreateWaitableTimer failed");
        } else {
            LARGE_INTEGER due_time;
            due_time.QuadPart = -1LL * (microseconds / resolution) * resolution;
            // Use a single period timer and then wait for it to expire.
            SetWaitableTimer(timer_handle, &due_time, 0, NULL, NULL, 0);
            WaitForSingleObject(timer_handle, INFINITE);
            CloseHandle(timer_handle);
        }
    }

    // Now, do busy wait on remaining amount of time that was less than the system resolution.
    while (CdiOsGetMicroseconds() < end_time) {
        Sleep(0); // Allow other threads to run, if any.
    }
}

uint64_t CdiOsGetMicroseconds(void)
{
    static LARGE_INTEGER frequency = {
        .QuadPart = 0LL
    };
    LARGE_INTEGER count;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }

    QueryPerformanceCounter(&count);

    return (count.QuadPart * 1000000LL) / frequency.QuadPart;
}

void CdiOsGetUtcTime(struct timespec* ret_time_ptr)
{
    timespec_get(ret_time_ptr, TIME_UTC);
}

void CdiOsGetLocalTime(struct tm* local_time_ret_ptr)
{
    time_t now = time(NULL);
    localtime_s(local_time_ret_ptr, &now);
}


int CdiOsGetLocalTimeString(char* time_str, int max_string_len)
{
    // If more precision is needed (milli, micro and nanoseconds), a possible solution is to call this function once
    // to initialize and statically allocate local_time along with the QueryPerformanceCounter. Every ensuing
    // call to this function would allow a developer to calculate the time difference and add the enhanced resolution.
    struct tm local_time;

    time_t now = time(NULL);
    localtime_s(&local_time, &now);
    SYSTEMTIME time_utc;
    GetSystemTime(&time_utc);              // Retrieves UTC.

    int hour_diff = 0;
    int min_offset = local_time.tm_min - time_utc.wMinute;

    // Get the hourly difference between local time and UTC.
    time_t time_utc_t = ConvertSystemTime(&time_utc);
    hour_diff = (int)((difftime(now, time_utc_t)) / 3600);

    // Determine the timezone offset to append to the date/time string. 'Z' is designated for UTC.
    char time_zone_str[CDI_MAX_FORMATTED_TIMEZONE_STRING_LENGTH] = {0};

    if (hour_diff == 0) {
        snprintf(time_zone_str, CDI_MAX_FORMATTED_TIMEZONE_STRING_LENGTH, "Z");
    } else {

        // Certain sections of the globe have a 30min offset.
        if (min_offset != 0) {
            min_offset = 30;
        }

        snprintf(time_zone_str, CDI_MAX_FORMATTED_TIMEZONE_STRING_LENGTH, "%+03d:%02d", hour_diff, min_offset);
    }

    return snprintf(time_str, max_string_len, "[%.4d-%.2d-%.2dT%.2d:%.2d:%.2d%s] ",
                          (local_time.tm_year + 1900), (local_time.tm_mon + 1), local_time.tm_mday,
                          local_time.tm_hour, local_time.tm_min, local_time.tm_sec, time_zone_str);
}

// -- Sockets --
bool CdiOsSocketOpen(const char* host_address_str, int port_number, const char* bind_address_str, CdiSocket* new_socket_ptr)
{
    bool ret = false;

    if (InitializeWinsock()) {
        SocketInfo* info_ptr = CdiOsMemAllocZero(sizeof(SocketInfo));
        if (NULL == info_ptr) {
            ERROR_MESSAGE("Failed to allocate memory");
        } else {
            info_ptr->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (info_ptr->s != INVALID_SOCKET) {
                char port_str[20];
                snprintf(port_str, sizeof(port_str), "%d", port_number);

                struct addrinfo hints = {
                    .ai_family = AF_INET,
                    .ai_socktype = SOCK_DGRAM,
                    .ai_protocol = IPPROTO_UDP,
                    .ai_flags = AI_PASSIVE
                };

                struct addrinfo* result_ptr = NULL;
                const int iResult = getaddrinfo(host_address_str, port_str, &hints, &result_ptr);
                if (iResult == 0) {
                    memcpy(&info_ptr->addr, result_ptr->ai_addr, result_ptr->ai_addrlen);
                    if (host_address_str == NULL) {
                        bool ok_to_bind = true;
                        if (NULL != bind_address_str) {
                            info_ptr->addr.sin_addr.s_addr = inet_addr(bind_address_str);
                            if (info_ptr->addr.sin_addr.s_addr == -1) {
                                // inet_addr does not set errno,
                                ERROR_MESSAGE("inet_addr() failed with bind address[%s]", bind_address_str);
                                ok_to_bind = false;
                            }
                        } else {
                            // Bind to the specified port number on any interface.
                            info_ptr->addr.sin_addr.s_addr = INADDR_ANY;
                        }
                        if (ok_to_bind) {
                            if (bind(info_ptr->s, result_ptr->ai_addr, result_ptr->ai_addrlen) == 0) {
                                ret = true;
                            } else {
                                int code = WSAGetLastError();
                                ERROR_MESSAGE("bind failed. Port[%d] might be in use by another application. Code[%d]",
                                    port_number, code);
                            }
                        }
                    } else {
                        ret = true;
                    }
                    freeaddrinfo(result_ptr);
                }
            }

            if (ret) {
                *new_socket_ptr = (CdiSocket)info_ptr;
            } else {
                if (info_ptr->s != INVALID_SOCKET) {
                    closesocket(info_ptr->s);
                }
                CdiOsMemFree(info_ptr);
                info_ptr = NULL;
            }
        }
    }

    return ret;
}

bool CdiOsSocketGetPort(CdiSocket socket_handle, int* port_number_ptr)
{
    assert(port_number_ptr != NULL);
    SocketInfo* info_ptr = (SocketInfo*)socket_handle;

    struct sockaddr_in sin;
    int len = sizeof(sin);
    if (getsockname(info_ptr->s, (struct sockaddr*)&sin, &len) != 0) {
        return false;
    } else {
        *port_number_ptr = ntohs(sin.sin_port);
        return true;
    }
}

bool CdiOsSocketClose(CdiSocket socket_handle)
{
    SocketInfo* info_ptr = (SocketInfo*)socket_handle;
    bool ret = closesocket(info_ptr->s) == 0;
    CdiOsMemFree(info_ptr);
    return ret;
}

bool CdiOsSocketRead(CdiSocket socket_handle, void* buffer_ptr, int* byte_count_ptr)
{
    return (CdiOsSocketReadFrom(socket_handle, buffer_ptr, byte_count_ptr, NULL));
}

bool CdiOsSocketReadFrom(CdiSocket socket_handle, void* buffer_ptr, int* byte_count_ptr,
                         struct sockaddr_in* source_address_ptr)
{
    assert(NULL != byte_count_ptr);

    bool ret = false;
    SocketInfo* info_ptr = (SocketInfo*)socket_handle;

    WSAPOLLFD pollfd = {
        .fd = info_ptr->s,
        .events = POLLIN
    };

    int rv = WSAPoll(&pollfd, 1, 10);
    if (rv > 0) {
        WSABUF wsabuf = {
            .len = *byte_count_ptr,
            .buf = buffer_ptr
        };
        DWORD flags = 0;
        socklen_t addrlen = (source_address_ptr) ? sizeof(*source_address_ptr) : 0;
        rv = WSARecvFrom(info_ptr->s, &wsabuf, 1, byte_count_ptr, &flags, (struct sockaddr *)source_address_ptr, &addrlen, NULL, NULL);
        if (rv == 0) {
            ret = true;
        } else {
            int code = WSAGetLastError();
            // Bidirectional sockets will cause a WSAECONNRESET error whenever SendTo issued without a connected
            // destination, even for UDP sockets. So, just ignore it and retry according to MSDN. Zero bytes received
            // will be returned.
            if (WSAECONNRESET == code) {
                ret = true;
            } else {
                ERROR_MESSAGE("WSARecvFrom failed. Code[%d]", code);
            }
        }
    } else if (rv == 0) {
        *byte_count_ptr = 0;  // indicates a timeout condition
        ret = true;
    } else {
        int code = WSAGetLastError();
        ERROR_MESSAGE("WSAPoll failed. Code[%d]", code);
    }

    return ret;
}

bool CdiOsSocketWrite(CdiSocket socket_handle, struct iovec* iov, int iovcnt, int* byte_count_ptr)
{
    SocketInfo* info_ptr = (SocketInfo*)socket_handle;

    return CdiOsSocketWriteTo(socket_handle, iov, iovcnt, &info_ptr->addr, byte_count_ptr);
}

bool CdiOsSocketWriteTo(CdiSocket socket_handle, struct iovec* iov, int iovcnt,
                        const struct sockaddr_in* destination_address_ptr, int* byte_count_ptr)
{
    WSABUF wsabufs[10];
    SocketInfo* info_ptr = (SocketInfo*)socket_handle;

    if (iovcnt > CDI_ARRAY_ELEMENT_COUNT(wsabufs)) {
        return false;
    } else {
        for (int i = 0; i < iovcnt; i++) {
            wsabufs[i].buf = iov[i].iov_base;
            wsabufs[i].len = iov[i].iov_len;
        }

        const struct sockaddr_in* addr_ptr = (destination_address_ptr) ? destination_address_ptr : &info_ptr->addr;
        return WSASendTo(info_ptr->s, wsabufs, iovcnt, byte_count_ptr, 0, (const struct sockaddr *)addr_ptr, sizeof(*addr_ptr),
                         NULL, NULL) == 0;
    }
}

bool CdiOsEnvironmentVariableSet(const char* name_str, const char* value_str)
{
    if (NULL == value_str) {
        // Linux allows NULL for the value but windows uses it to remove the variable. We want both variants to have the
        // same functionality (set a value), so return a failure here.
        ERROR_MESSAGE("Environment variable[%s] value cannot be NULL", name_str);
        return false;
    }
    return 0 != SetEnvironmentVariable(name_str, value_str);
}

void CdiOsShutdown()
{
    CdiOsStaticMutexLock(mutex_lock);
    if (winsock_initialized) {
        WSACleanup();
        winsock_initialized = false;
    }
    CdiOsStaticMutexUnlock(mutex_lock);
}
