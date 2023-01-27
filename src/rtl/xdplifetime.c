//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "xdplifetime.tmh"

static XDP_WORK_QUEUE *XdpLifetimeQueue;
static KDPC *XdpLifetimeDpcs;

static
_Function_class_(KDEFERRED_ROUTINE)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
XdpLifetimeDpc(
    _In_ struct _KDPC *Dpc,
    _In_opt_ VOID *DeferredContext,
    _In_opt_ VOID *SystemArgument1,
    _In_opt_ VOID *SystemArgument2
    )
{
    UINT32 *ReferenceCount = SystemArgument1;
    KEVENT *Event = SystemArgument2;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    if (InterlockedDecrement((LONG *)ReferenceCount) == 0) {
        KeSetEvent(Event, 0, FALSE);
    }
}

static
VOID
XdpLifetimeWorker(
    _In_ SINGLE_LIST_ENTRY *WorkQueueHead
    )
{
    UINT32 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    UINT32 ReferenceCount = ProcessorCount;
    KEVENT Event;
    UINT32 Index;

    TraceEnter(TRACE_RTL, "WorkQueueHead=%p", WorkQueueHead);

    //
    // Before deleting the entries, sweep across all processors, ensuring all
    // DPCs that might hold an active reference to objects finish executing.
    //
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    for (Index = 0; Index < ProcessorCount; Index++) {
        PROCESSOR_NUMBER Processor;

        FRE_ASSERT(NT_SUCCESS(KeGetProcessorNumberFromIndex(Index, &Processor)));
        KeInitializeDpc(&XdpLifetimeDpcs[Index], XdpLifetimeDpc, NULL);
        KeSetImportanceDpc(&XdpLifetimeDpcs[Index], HighImportance);
        KeSetTargetProcessorDpcEx(&XdpLifetimeDpcs[Index], &Processor);
        KeInsertQueueDpc(&XdpLifetimeDpcs[Index], &ReferenceCount, &Event);
    }

    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    while (WorkQueueHead != NULL) {
        XDP_LIFETIME_ENTRY *Entry = CONTAINING_RECORD(WorkQueueHead, XDP_LIFETIME_ENTRY, Link);
        WorkQueueHead = WorkQueueHead->Next;

        TraceVerbose(TRACE_RTL, "Entry=%p completed", Entry);
        Entry->DeleteRoutine(Entry);
    }

    TraceExitSuccess(TRACE_RTL);
}

VOID
XdpLifetimeDelete(
    _In_ XDP_LIFETIME_DELETE *DeleteRoutine,
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    TraceVerbose(TRACE_RTL, "Entry=%p requested", Entry);
    Entry->DeleteRoutine = DeleteRoutine;
    XdpInsertWorkQueue(XdpLifetimeQueue, &Entry->Link);
}

NTSTATUS
XdpLifetimeStart(
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    )
{
    NTSTATUS Status;

    XdpLifetimeDpcs =
        ExAllocatePoolZero(
            NonPagedPoolNx,
            sizeof(*XdpLifetimeDpcs) * KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS),
            XDP_POOLTAG_LIFETIME);
    if (XdpLifetimeDpcs == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpLifetimeQueue =
        XdpCreateWorkQueue(XdpLifetimeWorker, PASSIVE_LEVEL, DriverObject, DeviceObject);
    if (XdpLifetimeQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Ensure the lifetime worker runs at high priority to avoid the data path
    // starving the cleanup path.
    //
    XdpSetWorkQueuePriority(XdpLifetimeQueue, CriticalWorkQueue);

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpLifetimeStop();
    }

    return Status;
}

VOID
XdpLifetimeStop(
    VOID
    )
{
    if (XdpLifetimeQueue != NULL) {
        XdpShutdownWorkQueue(XdpLifetimeQueue, TRUE);
        XdpLifetimeQueue = NULL;
    }

    if (XdpLifetimeDpcs != NULL) {
        ExFreePoolWithTag(XdpLifetimeDpcs, XDP_POOLTAG_LIFETIME);
        XdpLifetimeDpcs = NULL;
    }
}
