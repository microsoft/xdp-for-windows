//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "offload.h"

NTSTATUS
XdpLwfOffloadQeoSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ const XDP_OFFLOAD_PARAMS_QEO *XdpQeoParams,
    _In_ UINT32 XdpQeoParamsSize,
    _Out_opt_ VOID *OffloadResult,
    _In_ UINT32 OffloadResultSize,
    _Out_opt_ UINT32 *OffloadResultWritten
    );
