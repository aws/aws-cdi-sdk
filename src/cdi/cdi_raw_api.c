// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise the CDI Raw Payload SDK's API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_raw_api.h"

#include <stdbool.h>
#include <stddef.h>

#include "internal.h"
#include "internal_tx.h"
#include "internal_rx.h"

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//*********************************************** START OF VARIABLES **************************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF STATIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

//*********************************************************************************************************************
//******************************************* START OF PUBLIC FUNCTIONS ***********************************************
//*********************************************************************************************************************

////////////////////////////////////////////////////////////////////////////////
// Doxygen commenting for these functions is in cdi_raw_api.h.
////////////////////////////////////////////////////////////////////////////////


CdiReturnStatus CdiRawTxCreate(CdiTxConfigData* config_data_ptr, CdiRawTxCallback tx_cb_ptr,
                               CdiConnectionHandle* ret_handle_ptr)
{
    if (!cdi_global_context.sdk_initialized) {
        return kCdiStatusNotInitialized;
    } else {
        return TxCreateInternal(kProtocolTypeRaw, config_data_ptr, (CdiCallback)tx_cb_ptr, ret_handle_ptr);
    }
}

CdiReturnStatus CdiRawRxCreate(CdiRxConfigData* config_data_ptr, CdiRawRxCallback rx_cb_ptr,
                               CdiConnectionHandle* ret_handle_ptr)
{
    if (!cdi_global_context.sdk_initialized) {
        return kCdiStatusNotInitialized;
    } else {
        return RxCreateInternal(kProtocolTypeRaw, config_data_ptr, (CdiCallback)rx_cb_ptr, ret_handle_ptr);
    }
}

CdiReturnStatus CdiRawTxPayload(CdiConnectionHandle con_handle,
                                const CdiCoreTxPayloadConfig* payload_config_ptr,
                                const CdiSgList* sgl_ptr, int max_latency_microsecs)
{
    if (!IsValidTxHandle(con_handle)) {
        return kCdiStatusInvalidHandle;
    }

    CdiEndpointState* endpoint_ptr = con_handle->default_tx_endpoint_ptr;
    if (!IsValidEndpointHandle(endpoint_ptr)) {
        return kCdiStatusInvalidHandle;
    }

    // Raw doesn't use extra data (so last two parameters are 0 and NULL).
    return TxPayloadInternal(con_handle->default_tx_endpoint_ptr, payload_config_ptr, sgl_ptr, max_latency_microsecs,
                             0, NULL);
}
