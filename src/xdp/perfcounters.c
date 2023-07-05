//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements performance counters.
//

#include "precomp.h"
#include "perfcounters.tmh"

static XDP_PCW_PER_PROCESSOR *XdpPerProcessorCounters;

static
PAGED_ROUTINE
NTSTATUS
NTAPI
XdpPcwPerProcessorCallback(
    _In_ PCW_CALLBACK_TYPE Type,
    _In_ PCW_CALLBACK_INFORMATION *Info,
    _In_opt_ VOID *Context
    )

/*++

Routine Description:

    This function is called by PCW.sys when a consumer is collecting data
    from the GeometricWave counterset.

    This function collects the requested information and adds it to the provided
    buffer.

Arguments:

    Type - Request type.

    Info - Buffer for returned data.

    Context - Not used.

Return Value:

    NTSTATUS indicating if the function succeeded.

--*/

{
    NTSTATUS Status;
    DECLARE_UNICODE_STRING_SIZE(Name, ARRAYSIZE("cpu_" MAXUINT32_STR));

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    switch (Type) {
    case PcwCallbackEnumerateInstances:
    case PcwCallbackCollectData:

        for (UINT32 i = 0; i < KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS); i++) {
            Status = RtlUnicodeStringPrintf(&Name, L"cpu_%u", i);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }

            Status =
                XdpPcwAddPerProcessor(
                    Info->EnumerateInstances.Buffer, &Name, 0, &XdpPerProcessorCounters[i]);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }

        break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
XdpPerfCountersStart(
    VOID
    )
{
    NTSTATUS Status;
    SIZE_T AllocSize;

    TraceEnter(TRACE_CORE, "-");

    Status =
        RtlSizeTMult(
            sizeof(*XdpPerProcessorCounters), KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS),
            &AllocSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XdpPerProcessorCounters =
        ExAllocatePoolZero(NonPagedPoolNxCacheAligned, AllocSize, XDP_POOLTAG_PCW);
    if (XdpPerProcessorCounters == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = XdpPcwRegisterPerProcessor(XdpPcwPerProcessorCallback, NULL);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpPerfCountersStop(
    VOID
    )
{
    TraceEnter(TRACE_CORE, "-");

    if (XdpPcwPerProcessor != NULL) {
        PcwUnregister(XdpPcwPerProcessor);
    }

    if (XdpPerProcessorCounters != NULL) {
        ExFreePoolWithTag(XdpPerProcessorCounters, XDP_POOLTAG_PCW);
        XdpPerProcessorCounters = NULL;
    }

    TraceExitSuccess(TRACE_CORE);
}
