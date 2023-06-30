//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements performance counters.
//

#include "precomp.h"

XDP_PCW_PER_PROCESSOR XdpPerProcessorCounters[4];

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
    DECLARE_UNICODE_STRING_SIZE(Name, ARRAYSIZE("CPU " MAXUINT32_STR));

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    switch (Type) {
    case PcwCallbackEnumerateInstances:
    case PcwCallbackCollectData:

        for (UINT32 i = 0; i < RTL_NUMBER_OF(XdpPerProcessorCounters); i++) {
            Status = RtlUnicodeStringPrintf(&Name, L"CPU %u", i);
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
    return XdpPcwRegisterPerProcessor(XdpPcwPerProcessorCallback, NULL);
}

VOID
XdpPerfCountersStop(
    VOID
    )
{
    if (XdpPcwPerProcessor != NULL) {
        PcwUnregister(XdpPcwPerProcessor);
    }
}
