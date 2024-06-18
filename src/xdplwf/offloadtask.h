//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "offload.h"

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
