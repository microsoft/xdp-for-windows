//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "offload.h"

UINT16
XdpOffloadChecksumNb(
    _In_ const NET_BUFFER *NetBuffer,
    _In_ UINT32 DataLength,
    _In_ UINT32 DataOffset
    );

_Offload_work_routine_
VOID
XdpLwfOffloadTaskOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    );

_Offload_work_routine_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpOffloadUpdateTaskOffloadConfig(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ const NDIS_OFFLOAD *TaskOffload,
    _In_ UINT32 TaskOffloadSize
    );
