//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offloadtask.tmh"

static
VOID
XdpLwfFreeTaskOffloadSetting(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *OldOffload =
        CONTAINING_RECORD(Entry, XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD, DeleteEntry);

    ExFreePoolWithTag(OldOffload, POOLTAG_OFFLOAD);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpOffloadUpdateTaskOffloadConfig(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ const NDIS_OFFLOAD *TaskOffload,
    _In_ UINT32 TaskOffloadSize
    )
{
    NTSTATUS Status;

    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *NewOffload = NULL;
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *OldOffload = NULL;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    if (TaskOffloadSize < sizeof(TaskOffload->Header) ||
        TaskOffload->Header.Size > TaskOffloadSize) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    NewOffload = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*NewOffload), POOLTAG_OFFLOAD);
    if (NewOffload == NULL) {
        TraceError(
            TRACE_LWF, "Filter=%p Failed to allocate XDP task offload setting", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Since XDP does not have configuration APIs for task offload, simply copy
    // the current NDIS settings.
    //
    RtlCopyMemory(
        &NewOffload->Params, TaskOffload, min(sizeof(NewOffload->Params), TaskOffloadSize));

    OldOffload = Filter->Offload.LowerEdge.TaskOffload;
    Filter->Offload.LowerEdge.TaskOffload = NewOffload;
    NewOffload = NULL;
    Status = STATUS_SUCCESS;

    TraceInfo(TRACE_LWF, "Filter=%p updated task offload setting", Filter);

Exit:

    if (OldOffload != NULL) {
        XdpLifetimeDelete(XdpLwfFreeTaskOffloadSetting, &OldOffload->DeleteEntry);
    }

    if (NewOffload != NULL) {
        ExFreePoolWithTag(NewOffload, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);
}
