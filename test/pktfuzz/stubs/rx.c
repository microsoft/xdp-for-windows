//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

XDP_PCW_RX_QUEUE *
XdpRxQueueGetStatsFromInspectionContext(
    _In_ const XDP_INSPECTION_CONTEXT *Context
    )
{
    static XDP_PCW_RX_QUEUE PcwStats;

    UNREFERENCED_PARAMETER(Context);

    return &PcwStats;
}
