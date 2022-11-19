// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
* @file
* @brief
* This file contains definitions and functions for accessing libfabric 1.9 and mainline through a generic V-table
* interface. It is used to generate two shared libraries, each linking to a unique version of libfabric as shown below:
*
* libfabric_api.so -> links to libfabric 1.9
* libfabric_api_new.so -> links to libfabric new
*/

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "adapter_efa.h"
#include "libfabric_api.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#define LOAD_LIBRARY(filename_str) (LoadLibrary(filename_str))
#define GET_PROC_ADDRESS(handle, name_str) ((void*)GetProcAddress(handle, name_str))
#define FREE_LIBRARY(handle) (FreeLibrary(handle))
#else
#include <dlfcn.h> // For dlopen(), RTLD_LAZY.

/// Macro used to dynamically load a library.
#define LOAD_LIBRARY(filename_str) (dlopen(filename_str, RTLD_LAZY))

/// Macro used to get an address to function in a dynamically loaded library using its name.
#define GET_PROC_ADDRESS(handle, name_str) (dlsym(handle, name_str))

/// Macro used to free resources used by a dynmically loaded library.
#define FREE_LIBRARY(handle) (dlclose(handle))
#endif

#ifdef LIBFABRIC_NEW
    #pragma message("Building libfabric_api.c using libfabric_new.")
    /// @brief The CDI-SDK has been validated using the version of libfabric specified below.
    CDI_STATIC_ASSERT(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 14, "Incorrect libfabric version.");
#else
    #pragma message("Building libfabric_api.c using libfabric (v1.9).")
    /// @brief To provide backwards compatibility with previous versions of the SDK, libfabric version 1.9.x must be
    /// used here.
    CDI_STATIC_ASSERT(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 9, "Incorrect libfabric version.");
#endif

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/// Forward reference.
static struct fi_info *fi_allocinfo_internal(void);

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

/// API V-table for libfabric API.
static LibfabricApi api_vtable = {
    .version_major = FI_MAJOR_VERSION,
    .version_minor = FI_MINOR_VERSION,
    .fi_version = NULL, // Libfabric non-static function.
    .fi_allocinfo = fi_allocinfo_internal,
    .fi_av_insert = fi_av_insert,
    .fi_av_open = fi_av_open,
    .fi_av_remove = fi_av_remove,
    .fi_close = fi_close,
    .fi_cq_open = fi_cq_open,
    .fi_cq_read = fi_cq_read,
    .fi_cq_readerr = fi_cq_readerr,
    .fi_domain = fi_domain,
    .fi_enable = fi_enable,
    .fi_endpoint = fi_endpoint,
    .fi_ep_bind = fi_ep_bind,
    .fi_fabric = NULL, // Libfabric non-static function.
    .fi_freeinfo = NULL, // Libfabric non-static function.
    .fi_getinfo = NULL, // Libfabric non-static function.
    .fi_getname = fi_getname,
    .fi_mr_desc = fi_mr_desc,
    .fi_mr_reg = fi_mr_reg,
    .fi_recvmsg = fi_recvmsg,
    .fi_sendmsg = fi_sendmsg,
    .fi_strerror = NULL, // Libfabric non-static function.
};

/// Handle for libfabric library.
static void* lib_handle = NULL;

/// Pointer to fi_dupinfo, which is dynamically loaded.
static struct fi_info* (*internal_fi_dupinfo_ptr)(const struct fi_info *info);

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

/**
 * @brief Wrapper function for fi_allocinfo(), which is a static line function that simply calls the non-static function
 * fi_dupinfo(). Wrapper required here since we want to dynamically load and use libfabric's non-static functions.
 *
 * @return Pointer to allocated fi_info structure.
 */
static struct fi_info *fi_allocinfo_internal(void)
{
	return (*internal_fi_dupinfo_ptr)(NULL); // Call fi_dupinfo() directly instead of using fi_allocinfo().
}

/**
 * @brief Dynamically load a version of the libfabric library.
 *
 * @param libfabric_filename_str Pointer to name of libfabric library to load.
 * @param ret_api_ptr Address where to write pointer to the V-table API.
 *
 * @return CdiReturnStatus kCdiStausOk if successful, otherwise a value indicating the nature of failure.
 */
static CdiReturnStatus DynamicallyLoadLibrary(const char* libfabric_filename_str, LibfabricApi** ret_api_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    lib_handle = LOAD_LIBRARY(libfabric_filename_str);

    if (lib_handle) {
        if (NULL == (api_vtable.fi_version = GET_PROC_ADDRESS(lib_handle, "fi_version")) ||
            NULL == (api_vtable.fi_fabric = GET_PROC_ADDRESS(lib_handle, "fi_fabric")) ||
            NULL == (api_vtable.fi_freeinfo = GET_PROC_ADDRESS(lib_handle, "fi_freeinfo")) ||
            NULL == (api_vtable.fi_getinfo = GET_PROC_ADDRESS(lib_handle, "fi_getinfo")) ||
            NULL == (api_vtable.fi_strerror = GET_PROC_ADDRESS(lib_handle, "fi_strerror")) ||
            NULL == (internal_fi_dupinfo_ptr = GET_PROC_ADDRESS(lib_handle, "fi_dupinfo"))) {
            rs = kCdiStatusLibrarySymbolNotFound;
        } else {
            uint32_t version = (api_vtable.fi_version)();
            if (FI_MAJOR(version) != api_vtable.version_major || FI_MINOR(version) != api_vtable.version_minor) {
                rs = kCdiStatusLibraryWrongVersion;
            } else {
                *ret_api_ptr = &api_vtable;
            }
        }
    } else {
        rs = kCdiStatusLibraryLoadFailed;
    }

    if (kCdiStatusOk != rs && lib_handle) {
        FREE_LIBRARY(lib_handle);
        lib_handle = NULL;
    }

    return rs;
}

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

#ifdef LIBFABRIC_NEW
CdiReturnStatus LoadLibfabricMainline(LibfabricApi** ret_api_ptr)
{
    return DynamicallyLoadLibrary(LIBFABRIC_NEW_FILENAME_STRING, ret_api_ptr);
}
#else
CdiReturnStatus LoadLibfabric1_9(LibfabricApi** ret_api_ptr)
{
    return DynamicallyLoadLibrary(LIBFABRIC_1_9_FILENAME_STRING, ret_api_ptr);
}
#endif
