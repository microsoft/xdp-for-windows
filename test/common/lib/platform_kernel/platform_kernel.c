//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include "platform.h"

uint64_t CxPlatPerfFreq;
uint32_t CxPlatProcessorCount;

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatInitialize(
    void
    )
{
    PAGED_CODE();

    (VOID)KeQueryPerformanceCounter((LARGE_INTEGER*)&CxPlatPerfFreq);

    CxPlatProcessorCount =
        (uint32_t)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    return XDP_STATUS_SUCCESS;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    )
{
    PAGED_CODE();
}
