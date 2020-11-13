// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains the definitions of the functions that comprise the CDI-AVM SDK's API.
 */

// Include headers in the following order: Related header, C system headers, other libraries' headers, your project's
// headers.

#include "cdi_avm_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "cdi_baseline_profile_api.h"
#include "internal.h"
#include "internal_tx.h"
#include "internal_rx.h"
#include "cdi_utility_api.h"

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
// Doxygen commenting for these functions is in cdi_avm_api.h.
////////////////////////////////////////////////////////////////////////////////

CdiReturnStatus CdiAvmTxCreate(CdiTxConfigData* config_data_ptr, CdiAvmTxCallback tx_cb_ptr,
                               CdiConnectionHandle* ret_handle_ptr)
{
    if (!cdi_global_context.sdk_initialized) {
        return kCdiStatusNotInitialized;
    } else {
        return TxCreateInternal(kProtocolTypeAvm, config_data_ptr, (CdiCallback)tx_cb_ptr, ret_handle_ptr);
    }
}

CdiReturnStatus CdiAvmStreamEndpointDestroy(CdiEndpointHandle handle)
{
    EndpointDestroyInternal(handle);
    return kCdiStatusOk;
}

CdiReturnStatus CdiAvmRxCreate(CdiRxConfigData* config_data_ptr, CdiAvmRxCallback rx_cb_ptr,
                               CdiConnectionHandle* ret_handle_ptr)
{
    if (!cdi_global_context.sdk_initialized) {
        return kCdiStatusNotInitialized;
    } else {
        return RxCreateInternal(kProtocolTypeAvm, config_data_ptr, (CdiCallback)rx_cb_ptr, ret_handle_ptr);
    }
}

CdiReturnStatus CdiAvmTxPayload(CdiConnectionHandle con_handle, const CdiAvmTxPayloadConfig* payload_config_ptr,
                                const CdiAvmConfig* avm_config_ptr, const CdiSgList* sgl_ptr, int max_latency_microsecs)
{
    if (!IsValidTxHandle(con_handle)) {
        return kCdiStatusInvalidHandle;
    }

    CDIPacketAvmUnion packet_avm_data;
    memset((void*)&packet_avm_data, 0, sizeof(packet_avm_data));

    packet_avm_data.common_header.avm_extra_data = payload_config_ptr->avm_extra_data;

    if (NULL != avm_config_ptr) {
        packet_avm_data.with_config.config = *avm_config_ptr;
    }

    int avm_data_size =
        (NULL == avm_config_ptr) ? sizeof(packet_avm_data.no_config) : sizeof(packet_avm_data.with_config);

    return TxPayloadInternal(con_handle, &payload_config_ptr->core_config_data, sgl_ptr, max_latency_microsecs,
                             avm_data_size, (uint8_t*)&packet_avm_data);
}
