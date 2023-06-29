//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements performance counters.
//

#include "precomp.h"

static
PAGED_ROUTINE
NTSTATUS
XdpPcwAddMyCounterSet1Instance(
    _In_ PPCW_BUFFER Buffer,
    _In_ PCWSTR Name
    )

/*++

Routine Description:

    This utility function computes the value of a synthetic counter
    instance and adds the value to the PCW buffer.

Arguments:

    Buffer - Data will be returned in this buffer.

    Name - Name of instances to be added.

    MinimalValue - Minimum value of the wave.

    Amplitude - Amplitude of the wave.

Return Value:

    NTSTATUS indicating if the function succeeded.

--*/

{
    LARGE_INTEGER Timestamp;
    UNICODE_STRING UnicodeName;
    MY_COUNTER_SET1_VALUES Values;

    PAGED_CODE();

    KeQuerySystemTime(&Timestamp);

    Values.MyCounter1 = (UINT32)Timestamp.QuadPart;

    RtlInitUnicodeString(&UnicodeName, Name);

    return XdpPcwAddMyCounterSet1(Buffer, &UnicodeName, 0, &Values);
}

static
PAGED_ROUTINE
NTSTATUS
NTAPI
XdpMyCounterSet1Callback(
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
    UNICODE_STRING UnicodeName;

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    switch (Type) {
    case PcwCallbackEnumerateInstances:

        //
        // Instances are being enumerated, do not provide values.
        //

        RtlInitUnicodeString(&UnicodeName, L"IfIndex1");
        Status = XdpPcwAddMyCounterSet1(Info->EnumerateInstances.Buffer, &UnicodeName, 0, NULL);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        RtlInitUnicodeString(&UnicodeName, L"IfIndex2");
        Status = XdpPcwAddMyCounterSet1(Info->EnumerateInstances.Buffer, &UnicodeName, 0, NULL);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        break;

    case PcwCallbackCollectData:

        Status = XdpPcwAddMyCounterSet1Instance(Info->CollectData.Buffer, L"IfIndex1");
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        Status = XdpPcwAddMyCounterSet1Instance(Info->CollectData.Buffer, L"IfIndex2");
        if (!NT_SUCCESS(Status)) {
            return Status;
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
    return XdpPcwRegisterMyCounterSet1(XdpMyCounterSet1Callback, NULL);
}

VOID
XdpPerfCountersStop(
    VOID
    )
{
    if (XdpPcwMyCounterSet1 != NULL) {
        PcwUnregister(XdpPcwMyCounterSet1);
    }
}
