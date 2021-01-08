// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the declarations for OS functions for creating/managing/freeing threads, semaphores, mutexes,
 * critical sections, signals, memory and sockets.  There are also abstractions for atomic operations as well as some
 * time, sleep and string operations. These definitions in this module are here to account for differences between
 * Linux and Windows.
 */

// Page for CDI OS API
/*!
 * @page cdi_os_api_home_page CDI OS API
 *
 * @section os_api_intro_sec Introduction
 * CDI OS API provides a collection of functions for abstracting common OS operations.  The operations available fall
 * into several groups:
 *  - Atomics
 *  - Threads
 *  - Semaphores
 *  - Critical Sections
 *  - Signals
 *  - Memory Allocation
 *  - File I/O
 *  - Sockets
 *  - Other operations such as Sleep, String Copy and Compare, Time, etc.
 *
 * Refer to @ref cdi_os_api.h for API details.
 */

#if !defined OS_API_H__
#define OS_API_H__

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

#if defined _WIN32
    // Windows specific #defs
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0502
    #endif  // _WIN32_WINNT 0x0502

    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif  // WIN32_LEAN_AND_MEAN

    #include <windows.h>
    #include <winsock2.h>
#elif defined (_LINUX)
    #include <pthread.h>
    #include <semaphore.h>
    #include <signal.h>
    #include <string.h>
    #include <strings.h>
#else
#error Either _WIN32 or _LINUX must be defined.
#endif

#include <sys/uio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "cdi_utility_api.h"

#if defined _WIN32
    #define CDI_STDIN GetStdHandle(STD_INPUT_HANDLE)    ///< Definition of OS agnostic standard input stream.
    #define CDI_STDOUT GetStdHandle(STD_OUTPUT_HANDLE)  ///< Definition of OS agnostic standard output stream.
    #define CDI_STDERR GetStdHandle(STD_ERROR_HANDLE)   ///< Definition of OS agnostic standard error output stream.

    #define CDI_STDIN_FILENO _fileno(stdin)   ///< Definition of OS agnostic standard input file number.
    #define CDI_STDOUT_FILENO _fileno(stdout) ///< Definition of OS agnostic standard output file number.
    #define CDI_STDERR_FILENO _fileno(stderr) ///< Definition of OS agnostic standard error file number.

    // Define portable thread Function.
    #define ThreadFuncName LPTHREAD_START_ROUTINE
    #define THREAD DWORD WINAPI
    #define THREAD_PARAM LPVOID
    #define WINDOW_HANDLE HWND

    typedef DWORD CdiThreadData;

    // Define portable semaphore. Don't use void* here, which prevents the compiler from type checking.
    typedef struct CdiSemID_* CdiSemID;

    // Define portable signal type.
    typedef HANDLE CdiSignalType;

    // Define portable critical section.
    typedef CRITICAL_SECTION* CdiCsID;

    // Define portable File ID type.
    typedef HANDLE CdiFileID;

    #define CDI_INFINITE INFINITE        // Infinity used as wait arguments, i.e "wait for infinity".

    // NOTE: These macros operate on 16-bit values.
    #define CdiOsAtomicInc16(x) InterlockedIncrement16(x)
    #define CdiOsAtomicDec16(x) InterlockedDecrement16(x)
    #define CdiOsAtomicRead16(x) InterlockedAdd16((x), 0)
    #define CdiOsAtomicAdd16(x, b) InterlockedAdd16((x), (b))

    // NOTE: These macros operate on 32-bit values.
    #define CdiOsAtomicInc32(x) InterlockedIncrement(x)
    #define CdiOsAtomicDec32(x) InterlockedDecrement(x)
    #define CdiOsAtomicRead32(x) InterlockedAdd((x), 0)
    #define CdiOsAtomicAdd32(x, b) InterlockedAdd((x), (b))

    // NOTE: These macros operate on 64-bit values.
    #define CdiOsAtomicInc64(x) InterlockedIncrement64(x)
    #define CdiOsAtomicDec64(x) InterlockedDecrement64(x)
    #define CdiOsAtomicRead64(x) InterlockedAdd64((x), 0)
    #define CdiOsAtomicAdd64(x, b) InterlockedAdd64((x), (b))

    // MSVC uses volatile to add necessary compiler/memory fence barriers as needed depending on CPU architecture. For
    // x86 platforms, recommend to use "/volatile:iso" for "C/C++"", "Command Line", "Additional Options" in MSVC
    // project configuration properties.
    #define CdiOsAtomicLoad16(x) *(volatile uint16_t*)(x)
    #define CdiOsAtomicStore16(x, v) *volatile (uint16_t*)(x) = (v)
    #define CdiOsAtomicLoad32(x) *(volatile uint32_t*)(x)
    #define CdiOsAtomicStore32(x, v) *(volatile uint32_t*)(x) = (v)
    #define CdiOsAtomicLoad64(x) *(volatile uint64_t*)(x)
    #define CdiOsAtomicStore64(x, v) *(volatile uint64_t*)(x) = (v)
    #define CdiOsAtomicLoadPointer(x) *(volatile void**)(x)
    #define CdiOsAtomicStorePointer(x, v) *(volatile void**)(x) = (v)

    typedef struct siginfo_t siginfo_t;

    struct siginfo_t {
        long si_pid;
        long si_uid;
    };

    typedef HANDLE CdiStaticMutexType;
    #define CDI_STATIC_MUTEX_INITIALIZER    NULL
    #define CdiOsStaticMutexLock(x) StaticMutexLockWin32(&(x))
    // If two threads enter the if-body, then only one succeeds in initializing the lock. When the application exists,
    // the mutex handle held in x will be freed by the windows OS.
    static inline void StaticMutexLockWin32(volatile HANDLE* x) {
        if (*x == NULL) {
            HANDLE tmp = CreateMutex(NULL, FALSE, NULL);
            if (InterlockedCompareExchangePointer((PVOID*)x, (PVOID)tmp, NULL) != NULL) {
                CloseHandle(tmp);
            }
        }
        WaitForSingleObject(*x, INFINITE);
    }
    #define CdiOsStaticMutexUnlock(x) (ReleaseMutex(x) == 0)

    // Huge pages not implemented in windows, so just use 1 byte for size.
    #define HUGE_PAGES_BYTE_SIZE    (1)

#elif defined _LINUX
    #define CDI_STDIN  stdin   ///< Definition of OS agnostic standard input stream.
    #define CDI_STDOUT stdout  ///< Definition of OS agnostic standard output stream.
    #define CDI_STDERR stderr  ///< Definition of OS agnostic standard error output stream.

    #define CDI_STDIN_FILENO STDIN_FILENO   ///< Definition of OS agnostic standard input file number.
    #define CDI_STDOUT_FILENO STDOUT_FILENO ///< Definition of OS agnostic standard output file number.
    #define CDI_STDERR_FILENO STDERR_FILENO ///< Definition of OS agnostic standard error file number.

    #define THREAD_PARAM void*  ///< Define portable thread function parameter type.
    #define THREAD int          ///< Define portable thread function return type.
    typedef THREAD (*ThreadFuncName) (THREAD_PARAM); ///< Define portable thread function.

    /// Define portable thread data type.
    typedef pthread_key_t CdiThreadData;

    /// Define portable window handle.
    #define WINDOW_HANDLE int

    /// Define portable semaphore.
    typedef sem_t* CdiSemID;

    /// @brief Define portable signal type. Don't use void* here, which prevents the compiler from type checking.
    typedef struct CdiSignalType_t* CdiSignalType;

    /// Define portable critical section.
    typedef pthread_mutex_t* CdiCsID;

    /// Define portable File ID type.
    typedef FILE* CdiFileID;

    #define CDI_INFINITE 0xFFFFFFFF  ///< Infinity used as wait arguments, i.e "wait for infinity".

    /// Atomic increment a 16-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicInc16(x) __sync_add_and_fetch((x), 1)
    /// Atomic decrement a 16-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicDec16(x) __sync_sub_and_fetch((x), 1)
    /// Atomic read a 16-bit value (matches windows variant, which uses functions).
    #define CdiOsAtomicRead16(x) __sync_add_and_fetch((x), 0)
    /// Atomic add a 16-bit value by a 16-bit value sent (matches windows variant, which uses functions).
    #define CdiOsAtomicAdd16(x, b) __sync_add_and_fetch((x), (b))

    /// Atomic increment a 32-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicInc32(x) __sync_add_and_fetch((x), 1)
    /// Atomic decrement a 32-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicDec32(x) __sync_sub_and_fetch((x), 1)
    /// Atomic read a 32-bit value (matches windows variant, which uses functions).
    #define CdiOsAtomicRead32(x) __sync_add_and_fetch((x), 0)
    /// Atomic add a 32-bit value by a 32-bit value sent (matches windows variant, which uses functions).
    #define CdiOsAtomicAdd32(x, b) __sync_add_and_fetch((x), (b))

    /// Atomic increment a 64-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicInc64(x) __sync_add_and_fetch((x), 1)
    /// Atomic decrement a 64-bit value by 1 (matches windows variant, which uses functions).
    #define CdiOsAtomicDec64(x) __sync_sub_and_fetch((x), 1)
    /// Atomic read a 64-bit value (matches windows variant, which uses functions).
    #define CdiOsAtomicRead64(x) __sync_add_and_fetch((x), 0)
    /// Atomic add a 64-bit value by a 64-bit value sent (matches windows variant, which uses functions).
    #define CdiOsAtomicAdd64(x, b) __sync_add_and_fetch((x), (b))

    /// @brief Atomic load value. Valid memory models are:
    /*! @code
        __ATOMIC_RELAXED : No barriers or synchronization.
        __ATOMIC_CONSUME : Data dependency only for both barrier and synchronization with another thread.
        __ATOMIC_ACQUIRE : Barrier to hoisting of code and synchronizes with release (or stronger) semantic stores from
                           another thread.
        __ATOMIC_SEQ_CST : Full barrier in both directions and synchronizes with acquire loads and release stores in
                           all threads.
    @endcode */
    #define CdiOsAtomicLoad16(x) __atomic_load_n((x), __ATOMIC_CONSUME)
    /// @brief 32-bit version of CdiOsAtomicLoad16 (matches windows variant, which uses functions).
    #define CdiOsAtomicLoad32(x) __atomic_load_n((x), __ATOMIC_CONSUME)
    /// @brief 64-bit version of CdiOsAtomicLoad16 (matches windows variant, which uses functions).
    #define CdiOsAtomicLoad64(x) __atomic_load_n((x), __ATOMIC_CONSUME)
    /// @brief Pointer version of CdiOsAtomicLoad16 (matches windows variant, which uses functions).
    #define CdiOsAtomicLoadPointer(x) __atomic_load_n((x), __ATOMIC_CONSUME)

    /// Atomic store value. Valid memory models are:
    /*! @code
        __ATOMIC_RELAXED : No barriers or synchronization.
        __ATOMIC_RELEASE : Barrier to sinking of code and synchronizes with acquire (or stronger) semantic loads from
                           another thread.
        __ATOMIC_SEQ_CST : Full barrier in both directions and synchronizes with acquire loads and release stores in
                           all threads.
    @endcode */
    #define CdiOsAtomicStore16(x, v) __atomic_store_n((x), (v), __ATOMIC_RELEASE)
    /// @brief 32-bit version of CdiOsAtomicStore16 (matches windows variant, which uses functions).
    #define CdiOsAtomicStore32(x, v) __atomic_store_n((x), (v), __ATOMIC_RELEASE)
    /// @brief 64-bit version of CdiOsAtomicStore16 (matches windows variant, which uses functions).
    #define CdiOsAtomicStore64(x, v) __atomic_store_n((x), (v), __ATOMIC_RELEASE)
    /// @brief Pointer version of CdiOsAtomicStore16 (matches windows variant, which uses functions).
    #define CdiOsAtomicStorePointer(x, v) __atomic_store_n((x), (v), __ATOMIC_RELEASE)

    /// Define portable invalid handle.
    #define INVALID_HANDLE_VALUE -1

    /// Define portable static mutex type. An example implementation:
    /*! @code
        static CdiStaticMutexType my_lock = CDI_STATIC_MUTEX_INITIALIZER;

        void Foo() {
            CdiOsStaticMutexLock(my_lock);
            // Do something that uses a shared resource.
            CdiOsStaticMutexLock(my_lock);
        }
    @endcode */
    typedef pthread_mutex_t CdiStaticMutexType;
    /// @brief Initialization value used to initialize the value of a static mutex variable.
    #define CDI_STATIC_MUTEX_INITIALIZER    PTHREAD_MUTEX_INITIALIZER
    /// @brief Lock a statically generated mutex.
    #define CdiOsStaticMutexLock(x)         pthread_mutex_lock(&(x))
    /// @brief Unlock a statically generated mutex.
    #define CdiOsStaticMutexUnlock(x)       pthread_mutex_unlock(&(x))

    /// @brief Size of huge pages. Memory must be a multiple of this size when using the CdiOsMemAllocHugePage() and
    /// CdiOsMemFreeHugePage() APIs.
    /// NOTE: Must match the "Hugepagesize" setting in /proc/meminfo.
    #define HUGE_PAGES_BYTE_SIZE    (2 * 1024 * 1024)
#endif // _LINUX

#define MAX_THREAD_NAME     (50)         ///< Maximum thread name size.
#define OS_SIG_TIMEOUT      (0xFFFFFFFF) ///< Timeout value returned when waiting on a signal using CdiOsSignalsWait().

/// Maximum number of signals that can be passed to CdiOsSignalsWait().
#define MAX_WAIT_MULTIPLE   (64)

/// The maximum size of iovec array that can be passed in to CdiOsSocketWrite().
#define CDI_OS_SOCKET_MAX_IOVCNT (10)

/// @brief Type used for signal handler.
typedef void (*SignalHandlerFunction)(int sig, siginfo_t* siginfo, void* context);

/// @brief Structure used to hold signal handler data.
typedef struct {
    int signal_num;  ///< Signal number of the signal related to the handler.
    SignalHandlerFunction func_ptr; ///< Pointer to signal handler.
} SignalHandlerInfo;

/// @brief Define portable thread type. Separate name from type, otherwise the typedef that follows it will generate a
/// compile error (duplicate typedef).
typedef struct CdiThreadID_t* CdiThreadID;

/// Define portable socket type.
typedef struct CdiSocket_t* CdiSocket;

/// Maximum number of signal handlers.
#define MAX_SIGNAL_HANDLERS     (10)

/// Maximum length of a single formatted time string.
#define MAX_FORMATTED_TIMEZONE_STRING_LENGTH   (128)

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable use of the logger when generating error messages. This function is normally used internally as part of
 * initialization of the CDI SDK. If it is not used, then output will be directed to stderr.
 */
CDI_INTERFACE void CdiOsUseLogger(void);

// -- Threads --

/**
 * Set the address to the default signal handler function shared by all threads.
 *
 * @param signal_num            Number of the signal to set the new handler.
 * @param func_ptr              Address of signal handler function to set for the signal.
 *
 * @return true on success, false if there isn't enough storage to hold the signal or if there was an error.
 */
CDI_INTERFACE bool CdiOsSignalHandlerSet(int signal_num, SignalHandlerFunction func_ptr);

/**
 * Creates a thread which can optionally be pinned to a specific CPU.
 *
 * @param thread_func         Pointer to a function for the thread.
 * @param thread_id_out_ptr   Pointer to CdiThreadID to return.
 * @param thread_name_str     Optional Thread Name for debugging and logging purposes (NULL if don't care).
 * @param thread_func_arg_ptr Optional pointer to user data passed to the thread delegate.
 * @param start_signal        Optional signal used to start the thread. If NULL, thread starts running immediately.
 * @param cpu_affinity        Zero-based CPU number to pin this thread to, -1 to not pin.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadCreatePinned(ThreadFuncName thread_func, CdiThreadID* thread_id_out_ptr,
                                           const char* thread_name_str, void* thread_func_arg_ptr,
                                           CdiSignalType start_signal, int cpu_affinity);

/**
 * Creates a thread. Note that thread pinning is inherited, so the launched thread will inherit the affinity of its
 * parent if not explicitly set.
 *
 * @param thread_func         Pointer to a function for the thread.
 * @param thread_id_out_ptr   Pointer to CdiThreadID to return.
 * @param thread_name_str     Optional Thread Name for debugging and logging purposes (NULL if don't care).
 * @param thread_func_arg_ptr Optional pointer to user data passed to the thread delegate.
 * @param start_signal        Optional signal used to start the thread. If NULL, thread starts running immediately.
 *
 * @return true if successful, otherwise false.
 */
static inline bool CdiOsThreadCreate(ThreadFuncName thread_func, CdiThreadID* thread_id_out_ptr,
                                     const char* thread_name_str, void* thread_func_arg_ptr, CdiSignalType start_signal)
{
    return CdiOsThreadCreatePinned(thread_func, thread_id_out_ptr, thread_name_str, thread_func_arg_ptr, start_signal,
                                   -1);
}

/**
 * Allocates a slot of thread-local storage. The slot is allocated once for the whole program, after which each thread
 * can store and read private data from the slot.
 *
 * @param handle_out_ptr Returned handle for a thread data slot.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadAllocData(CdiThreadData* handle_out_ptr);

/**
 * Frees a slot of thread-local storage.  Should be called before program exit but after all threads are done using the
 * slot.
 *
 * @param handle Handle of thread data to free.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadFreeData(CdiThreadData handle);

/**
 * Stores a value in this thread's copy of a thread-local storage slot. Calls to osThreadGetData from the same thread
 * will get this value back. The slot must have been allocated by CdiOsThreadAllocData.
 *
 * @param handle      Handle to thread data slot.
 * @param content_ptr Pointer to be stored in the slot.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadSetData(CdiThreadData handle, void* content_ptr);

/**
 * Get the value of this thread's copy of a thread-local storage slot.
 *
 * @param handle          Handle to thread data slot.
 * @param content_out_ptr Pointer to a variable which receives the data.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadGetData(CdiThreadData handle, void** content_out_ptr);

/**
 * Get the name of the thread that was created using CdiOsThreadCreatePinned().
 *
 * @param thread_id Data structure for thread to get name.
 *
 * @return Pointer to name of the thread.
 */
CDI_INTERFACE const char* CdiOsThreadGetName(CdiThreadID thread_id);

/**
 * Waits/blocks until the given thread has finished.
 *
 * @param thread_id      Data structure for thread to wait for.
 * @param timeout_in_ms  How long to wait for join before timing out.
 * @param timed_out_ptr  Pointer to a boolean that indicates a timeout has occurred when true.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsThreadJoin(CdiThreadID thread_id, uint32_t timeout_in_ms, bool* timed_out_ptr);

// -- Semaphores --

/**
 * Creates a semaphore.
 *
 * @param ret_sem_handle_ptr Pointer to semaphore ID to return.
 * @param sem_count          Initial semaphore count.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSemaphoreCreate(CdiSemID* ret_sem_handle_ptr, int sem_count);

/**
 * Deletes a semaphore.
 *
 * @param sem_handle Pointer to semaphore ID to delete.
 *
 * @return returns true if successful, otherwise false is returned.
 */
CDI_INTERFACE bool CdiOsSemaphoreDelete(CdiSemID sem_handle);

/**
 * Releases a semaphore.
 *
 * @param sem_handle Semaphore ID to release.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSemaphoreRelease(CdiSemID sem_handle);

/**
 * Reserves a semaphore and blocks if the current semaphore count is 0. If the semaphore is already reserved by the
 * calling thread, then this call simply returns success.
 *
 * @param sem_handle    Semaphore ID to reserve.
 * @param timeout_in_ms Amount of miliseconds to wait for the semaphore, CDI_INFINITE to wait indefinitely.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSemaphoreReserve(CdiSemID sem_handle, int timeout_in_ms);

/**
 * Returns the value of the given semaphore (ie. how many semaphore resources are available).
 *
 * @param sem_handle Semaphore ID to check.
 *
 * @return Value of semaphore.
 */
CDI_INTERFACE int CdiOsSemaphoreValueGet(CdiSemID sem_handle);

// -- Critical Sections --

/**
 * Creates and initializes a critical section.
 *
 * @param cs_handle_ptr Pointer to critical section ID to return.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsCritSectionCreate(CdiCsID* cs_handle_ptr);

/**
 * Reserves the specified critical section.
 *
 * @param cs_handle Critical section ID to reserve.
 */
CDI_INTERFACE void CdiOsCritSectionReserve(CdiCsID cs_handle);

/**
 * Releases the specified critical section.
 *
 * @param cs_handle Critical section ID to release.
 */
CDI_INTERFACE void CdiOsCritSectionRelease(CdiCsID cs_handle);

/**
 *  Deletes a critical section.
 *
 * @param cs_handle Pointer to critical section ID to delete.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsCritSectionDelete(CdiCsID cs_handle);

// -- String Functions ---

#if defined _WIN32
///< get a string token
#define CdiOsStrTokR strtok_s
#else
///< get a string token
#define CdiOsStrTokR strtok_r
#endif

// -- Signals --

/**
 * This function creates a signal. The initial value is not signaled.
 *
 * @param signal_handle_ptr Address where to write the returned signal handle.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalCreate(CdiSignalType* signal_handle_ptr);

/**
 * This function deletes a signal.
 *
 * @param signal_handle A signal handle to delete.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalDelete(CdiSignalType signal_handle);

/**
 * This function clears a signal.
 *
 * @param signal_handle A signal handle to clear the value of.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalClear(CdiSignalType signal_handle);

/**
 * This function sets a signal and its related state variable.
 *
 * @param signal_handle A signal handle to set the value of.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalSet(CdiSignalType signal_handle);

/**
 * This function returns the value of the signal passed in.
 *
 * @param signal_handle A signal handle to get the value of.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalGet(CdiSignalType signal_handle);

/**
 * This function returns the value of the signal passed in without using any OS resources. It only accesses state data.
 *
 * @param signal_handle A signal handle to read the state value of.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalReadState(CdiSignalType signal_handle);

/**
 * This function waits for a signal.
 *
 * @param signal_handle  A signal handle to wait on.
 * @param timeout_in_ms  Timeout in mSec can be CDI_INFINITE to wait indefinitely.
 * @param timed_out_ptr  Pointer to boolean - set to true if timed out.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalWait(CdiSignalType signal_handle, uint32_t timeout_in_ms, bool* timed_out_ptr);

/**
 * This function waits for an array of signals.
 *
 * @param signal_array  Pointer to an array of signal handles to wait on.
 * @param num_signals   Number of signals in the array.
 * @param wait_all      Use true to wait for all signals, false to block on any signal.
 * @param timeout_in_ms Timeout in mSec can be CDI_INFINITE to wait indefinitely.
 * @param ret_signal_index_ptr Pointer to the returned signal index that caused the thread to be signaled. if wait_all
 *        is true, then this is set to 1 when all signals are signaled.  If a timeout occurred, OS_SIG_TIMEOUT is
 *        returned. This is an optional parameter, you can pass NULL if you don't care.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSignalsWait(CdiSignalType* signal_array, uint8_t num_signals, bool wait_all,
                                    uint32_t timeout_in_ms, uint32_t* ret_signal_index_ptr);

// -- Memory --

/**
 * Allocates a block of memory and returns a pointer to the start of the block.
 *
 * @param mem_size Number of bytes to allocate.
 *
 * @return Pointer to the allocated memory block. If unable to allocate the memory block, NULL is returned.
 */
CDI_INTERFACE void* CdiOsMemAlloc(int32_t mem_size);

/**
 * Allocates a block of memory, writes zero across its entirety, and returns a pointer to the start of the block.
 *
 * @param mem_size Number of bytes to allocate.
 *
 * @return Pointer to the allocated memory block. If unable to allocate the memory block, NULL is returned.
 */
CDI_INTERFACE void* CdiOsMemAllocZero(int32_t mem_size);

/**
 * Releases a previously allocated block of memory.
 *
 * @param mem_ptr Pointer to start address of memory block.
 */
CDI_INTERFACE void CdiOsMemFree(void* mem_ptr);

/**
 * Allocates a block of huge page memory and returns a pointer to the start of the block.
 *
 * @param mem_size Number of bytes to allocate. Size must be a multiple of HUGE_PAGES_BYTE_SIZE. If not, NULL is
 * returned.
 *
 * @return Pointer to the allocated memory block. If unable to allocate the memory block, NULL is returned.
 */
CDI_INTERFACE void* CdiOsMemAllocHugePage(int32_t mem_size);

/**
 * Releases a previously allocated block of huge page memory.
 *
 * @param mem_ptr Pointer to start address of memory block.
 * @param mem_size Number of bytes that were allocated.
 */
CDI_INTERFACE void CdiOsMemFreeHugePage(void* mem_ptr, int mem_size);

// -- File --

/**
 * Opens a file (file_name_str) for writing and returns a file handle.
 *
 * @param file_name_str   Pointer to filename to open.
 * @param file_handle_ptr Address where to write returned file handle.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsOpenForWrite(const char* file_name_str, CdiFileID* file_handle_ptr);

/**
 * Opens a file (file_name_str) for read and returns a file handle.
 *
 * @param file_name_str   Pointer to filename to open.
 * @param file_handle_ptr Address where to write returned file handle.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsOpenForRead(const char* file_name_str, CdiFileID* file_handle_ptr);

/**
 *  Closes a file.
 *
 * @param file_handle Identifier of file to close.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsClose(CdiFileID file_handle);

/**
 * Reads data from a file.
 *
 *  @param file_handle    Identifier of file to read from.
 *  @param buffer_ptr     Pointer to buffer to read in to.
 *  @param byte_count     Number of bytes to read into the buffer pointed to by buffer_ptr.
 *  @param bytes_read_ptr Returns the number of bytes read (NULL if you don't care).
 *
 * @return true if successful, otherwise false. Check EOF using OS's EOF API function.
 */
CDI_INTERFACE bool CdiOsRead(CdiFileID file_handle, void* buffer_ptr, uint32_t byte_count, uint32_t* bytes_read_ptr);

/**
 * Writes a file.
 *
 * @param file_handle Identifier of file to write to.
 * @param data_ptr    Pointer to data to write.
 * @param byte_count  Number of bytes to write.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsWrite(CdiFileID file_handle, const void* data_ptr, uint32_t byte_count);

/**
 * Flushes write buffers for the specified file.
 *
 * @param file_handle Identifier of the file to flush.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsFlush(CdiFileID file_handle);

/**
 * Retrieves the current file position for the specified file.
 *
 * @param file_handle          Identifier of the file.
 * @param current_position_ptr Pointer to current position of the file.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsFTell(CdiFileID file_handle, uint64_t* current_position_ptr);

/**
 * Retrieves the current file position for the specified file.
 *
 * @param file_handle Identifier of the file.
 * @param offset      Number of bytes to offset from position.
 * @param position    The position from where offset is added.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsFSeek(CdiFileID file_handle, int64_t offset, int position);

/**
 * Takes in a filepath and breaks it into its component directory and filename.
 *
 * @param filepath_str       String containing a path name and filename.
 * @param filename_str       String containing the filename without the path. Pass NULL for this parameter if the
 *                           filename is not needed.
 * @param filename_buf_size  Size of the buffer sent for storing the filename string.
 * @param directory_str      String containing the directory path, including any end '/' without the filename. Pass NULL
 *                           for this parameter if the directory path is not needed.
 * @param directory_buf_size Size of the buffer sent for storing the directory path string.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsSplitPath(const char* filepath_str, char* filename_str, int filename_buf_size,
                                  char* directory_str, int directory_buf_size);

/**
 * Takes in a directory string and verifies that the directory exists and is writeable.
 *
 * @param directory_str      String containing the directory path, including any end '/' without the filename.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsIsPathWriteable(const char* directory_str);

// -- Utilities - Strings, Sleep --

/**
 * A portable version of strcpy with range checking to replace Microsoft's strcpy_s.
 *
 * @param dest_str    Buffer to receive copied string.
 * @param max_str_len Maximum number of characters to copy. This includes the terminating '\0'.
 * @param src_str     Source string to copy.
 *
 * @return Number of characters copied.
 */
CDI_INTERFACE int CdiOsStrCpy(char* dest_str, uint32_t max_str_len, const char* src_str);

/**
 * Block the current thread for the specified number of milliseconds.
 *
 * @param milliseconds Block thread for this much time.
 */
CDI_INTERFACE void CdiOsSleep(uint32_t milliseconds);

/**
 * Block the current thread for microseconds.
 *
 * @param microseconds Block thread for this much time.
 */
CDI_INTERFACE void CdiOsSleepMicroseconds(uint32_t microseconds);

#if defined _WIN32
#define CdiOsStrCaseCmp _stricmp
#define CdiOsStrNCaseCmp _strnicmp
#else
/// Used to compare two strings specifying the number of characters to compare, ignoring the case of the characters.
#define CdiOsStrNCaseCmp strncasecmp
/// Used to compare two strings, ignoring the case of the characters.
#define CdiOsStrCaseCmp strcasecmp
#endif

/**
 * @brief Used to compare two strings.
 *
 * @param string1 C string to be compared with string2.
 * @param string2 C string to be compared with string1.
 * @param num     Maximum number of characters to compare. NOTE: num is size_t.
 *
 * @return Returns int = 0 both substrings are identical, <0 string1 first non-matching character has a lower value than
 *         string2, >0 string1 first non-matching character has a higher value than string2.
 */
#define CdiOsStrCmp strcmp

/**
 * @brief Used to compare two strings specifying the number of characters to compare.
 *
 * @param string1 C string to be compared with string2.
 * @param string2 C string to be compared with string1.
 * @param num     Maximum number of characters to compare. NOTE: num is size_t.
 *
 * @return Returns int =0 both substrings are identical, <0 string1 first non-matching character has a lower value than
 *         string2, >0 string1 first non-matching character has a higher value than string2.
 */
#define CdiOsStrNCmp strncmp

/**
 * @brief Timers get a microsecond timestamp from CLOCK_MONOTONIC on linux or from the performance counter on Windows.
 *
 * @return Microsecond timestamp.
 */
CDI_INTERFACE uint64_t CdiOsGetMicroseconds(void);

/**
 * @brief Macro to get OS time in milliseconds that uses CdiOsGetMicroseconds().
 *
 * @return Millisecond timestamp.
 */
#define CdiOsGetMilliseconds() (CdiOsGetMicroseconds()/1000)

/**
 * @brief This is an OS call to get the current synced AWS network time in UTC format.
 *
 * This function will be kept up to date with the best practices for getting high accuracy time from Amazon Time Sync
 * Service as improved accuracy time is available. All EC2 instances that call this function should be using the Amazon
 * Time Sync Service. Amazon Time Sync Service setup directions for Linux can be found at:
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/set-time.html#configure-amazon-time-service

 * For Windows follow the directions at: https://docs.aws.amazon.com/AWSEC2/latest/WindowsGuide/windows-set-time.html
 *
 * @param ret_time_ptr a pointer returned to a UTC timestamp in the format of a timespec structure as defined by time.h.
 */
CDI_INTERFACE void CdiOsGetUtcTime(struct timespec* ret_time_ptr);

/**
 * Get current local time as "tm" structure.
 *
 * @param local_time_ret_ptr Pointer to returned local time.
 */
CDI_INTERFACE void CdiOsGetLocalTime(struct tm* local_time_ret_ptr);

/**
 * Get current local time as a formatted as ISO 8601.
 *
 * @param time_str Formatted string to represent ISO 8601 time format.
 * @param max_string_len Maximum allowable characters in the time string.
 *
 * @return char_count Returns the number of characters of the formatted string.
 */
CDI_INTERFACE int CdiOsGetLocalTimeString(char* time_str, int max_string_len);

/**
 * Opens a unidirectional Internet Protocol User Datagram Protocol (IP/UDP) socket for communications. For a socket to
 * send on, specify the host address of the remote host. To create a socket for receiving, specify NULL as
 * host_address_str.
 *
 * @param host_address_str The address of the remote host (dotted IPv4 address) to which to send datagrams or NULL if
 *                         the socket is to be used for receiving datagrams.
 * @param port_number The numeric port number on the remote host (for a send-only socket) or the local port number to
 *                    listen for incoming datagrams (for a receive-only socket). Receive-only sockets can specify zero
 *                    for this value so that a randomly selected port number is used. Call @see CdiOsSocketGetPort to
 *                    determine the port number that was assigned.
 * @param new_socket_ptr A pointer to the location which will get the new socket handle written to it.
 *
 * @return true if the socket was successfully opened and is ready for communications, otherwise false.
 */
CDI_INTERFACE bool CdiOsSocketOpen(const char* host_address_str, int port_number, CdiSocket* new_socket_ptr);

/**
 * Gets the number of the port to which the specified socket is bound. This is useful for receive sockets opened with
 * their port number specified as 0, which makes the operating system assign a random port number. It also works on
 * transmit sockets which are also randomly assigned a port number.
 *
 * @param s               The socket whose port number is of interest.
 * @param port_number_ptr Address of where the port number will be written.
 *
 * @return true if the port number could be determined or false if a problem was encountered.
 */
CDI_INTERFACE bool CdiOsSocketGetPort(CdiSocket s, int* port_number_ptr);

/**
 * Close a previously opened communications socket, freeing resources that were allocated for it including the local
 * port if the socket was opened for receiving.
 *
 * @param socket_handle The handle of the socket which is to be closed.
 *
 * @return true if the socket was closed cleanly, false if a problem was encountered trying to close it.
 */
CDI_INTERFACE bool CdiOsSocketClose(CdiSocket socket_handle);

/**
 * Synchronously reads the next available datagram from the specified socket which must have been opened for receiving.
 * If no datagram is available after a short timeout, true is returned but the value written to byte_count_ptr will be
 * zero. This timeout is so that the caller can periodically check whether to shut down its polling loop.
 *
 * @param socket_handle  The handle for the socket for which incoming datagrams are to be received.
 * @param buffer_ptr     The address in memory where the bytes of the datagram will be written.
 * @param byte_count_ptr On entry, the value at the location pointed to must be the size of the buffer at buffer_ptr
 *                       available for the datagram to be written. At exit, the address will be overwritten with the
 *                       number of bytes that are actually contained in the datagram and thus written to the buffer. A
 *                       value of 0 indicates that the read timed out waiting for a datagram and that the read should be
 *                       retried unless the polling process should be shut down.
 *
 * @return true if the function succeeded, false if it failed. Timing out is considered to be success but zero will have
 *         been written to byte_count_ptr to disambiguate a timeout condition from data being written into the buffer.
 */
CDI_INTERFACE bool CdiOsSocketRead(CdiSocket socket_handle, void* buffer_ptr, int* byte_count_ptr);

/**
 * Synchronously write a datagram to a communications socket which must have been opened for sending. The data is
 * represented as an array of address pointers and sizes. This data is copied inside of the function so once it returns
 * the buffer(s) are available for reuse.
 *
 * @param socket_handle  The handle for the socket through which the datagram will be written.
 * @param iov            The address of an array of iovec structures which specify the data to be sent.
 * @param iovcnt         The number of iovec structures contained in the iov array. This value is limited to
 *                       CDI_OS_SOCKET_MAX_IOVCNT.
 * @param byte_count_ptr The address of a location into which the number of bytes written to the socket will be placed
 *                       if the datagram was successfully sent.
 *
 * @return true if the datagram was successfully sent, false if not. Note that there is no guarantee that the datagram
 *         was actually received by the destination host.
 */
CDI_INTERFACE bool CdiOsSocketWrite(CdiSocket socket_handle, struct iovec* iov, int iovcnt, int* byte_count_ptr);

/**
 * Set an environment variable for the currently running process. NOTE: Does not set the process's shell environment.
 *
 * @param name_str  Pointer to string name of variable to set. Assumed to be a non-NULL value.
 * @param value_str Pointer to string value to set. NOTE: Cannot be NULL.
 *
 * @return true if successful, otherwise false.
 */
CDI_INTERFACE bool CdiOsEnvironmentVariableSet(const char* name_str, const char* value_str);

/**
 * Shuts down OS specific resources used by the SDK.
 */
CDI_INTERFACE void CdiOsShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // OS_API_H__
