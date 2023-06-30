//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "xdpworkqueue.tmh"

#pragma warning(disable:4701) // OldIrql for XdpWorkQueueReleaseLock is initialized.

typedef struct _XDP_WORK_QUEUE {
    KIRQL MaxIrql;
    WORK_QUEUE_TYPE Priority;
    union {
        EX_PUSH_LOCK PushLock;
        KSPIN_LOCK SpinLock;
    };
    XDP_REFERENCE_COUNT ReferenceCount;
    XDP_WORK_QUEUE_ROUTINE *Routine;
    SINGLE_LIST_ENTRY *Head;
    SINGLE_LIST_ENTRY *Tail;
    PIO_WORKITEM IoWorkItem;
    KEVENT *ShutdownEvent;
} XDP_WORK_QUEUE;

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIoWorkItemRoutine(
    _In_opt_ VOID *IoObject,
    _In_opt_ VOID *Context,
    _In_ PIO_WORKITEM IoWorkItem
    );

#define XdpWorkQueueAcquireLock(WorkQueue, OldIrql) \
    if ((WorkQueue)->MaxIrql < DISPATCH_LEVEL) { \
        RtlAcquirePushLockExclusive(&(WorkQueue)->PushLock); \
    } else { \
        KeAcquireSpinLock(&(WorkQueue)->SpinLock, OldIrql); \
    }

#define XdpWorkQueueReleaseLock(WorkQueue, OldIrql) \
    if ((WorkQueue)->MaxIrql < DISPATCH_LEVEL) { \
        RtlReleasePushLockExclusive(&(WorkQueue)->PushLock); \
    } else { \
        KeReleaseSpinLock(&(WorkQueue)->SpinLock, OldIrql); \
    }

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_WORK_QUEUE *
XdpCreateWorkQueue(
    _In_ XDP_WORK_QUEUE_ROUTINE *WorkQueueRoutine,
    _In_ KIRQL MaxIrql,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    )
{
    XDP_WORK_QUEUE *WorkQueue;
    VOID *IoObject;

    //
    // Initializes a work-queue.
    //

    ASSERT(DriverObject != NULL || DeviceObject != NULL);
    IoObject = (DriverObject != NULL) ? (VOID *)DriverObject : DeviceObject;

    if (!ExAcquireRundownProtection(&XdpRtlRundown)) {
        return NULL;
    }

    WorkQueue =
        ExAllocatePoolZero(
            MaxIrql < DISPATCH_LEVEL ? PagedPool : NonPagedPoolNx, sizeof(*WorkQueue),
            XDP_POOLTAG_WORKQUEUE);
    if (WorkQueue == NULL) {
        ExReleaseRundownProtection(&XdpRtlRundown);
        return NULL;
    }

    if (MaxIrql < DISPATCH_LEVEL) {
        ExInitializePushLock(&WorkQueue->PushLock);
    } else {
        KeInitializeSpinLock(&WorkQueue->SpinLock);
    }

    XdpInitializeReferenceCount(&WorkQueue->ReferenceCount);
    WorkQueue->MaxIrql = MaxIrql;
    WorkQueue->Routine = WorkQueueRoutine;
    WorkQueue->Priority = DelayedWorkQueue;
    WorkQueue->IoWorkItem =
        ExAllocatePoolZero(
            NonPagedPoolNx, IoSizeofWorkItem(), XDP_POOLTAG_WORKQUEUE);
    if (WorkQueue->IoWorkItem == NULL) {
        ExFreePoolWithTag(WorkQueue, XDP_POOLTAG_WORKQUEUE);
        ExReleaseRundownProtection(&XdpRtlRundown);
        return NULL;
    }

    IoInitializeWorkItem(IoObject, WorkQueue->IoWorkItem);

    return WorkQueue;
}

static
VOID
XdpReferenceWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue
    )
{
    XdpIncrementReferenceCount(&WorkQueue->ReferenceCount);
}

static
VOID
XdpDereferenceWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue
    )
{
    if (XdpDecrementReferenceCount(&WorkQueue->ReferenceCount)) {
        ASSERT(WorkQueue->Tail == NULL);
        IoUninitializeWorkItem(WorkQueue->IoWorkItem);
        ExFreePoolWithTag(WorkQueue->IoWorkItem, XDP_POOLTAG_WORKQUEUE);
        ExFreePoolWithTag(WorkQueue, XDP_POOLTAG_WORKQUEUE);
        ExReleaseRundownProtection(&XdpRtlRundown);
    }
}

_When_(Wait == FALSE, _IRQL_requires_max_(DISPATCH_LEVEL))
_When_(Wait != FALSE, _IRQL_requires_(PASSIVE_LEVEL))
VOID
XdpShutdownWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ BOOLEAN Wait
    )
{
    if (Wait) {
        KIRQL OldIrql;

        XdpWorkQueueAcquireLock(WorkQueue, &OldIrql);
        if (WorkQueue->Tail == NULL) {
            //
            // The work-item is not executing.
            //

            XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
        } else {
            //
            // The work-item is executing. Synchronize with it before proceeding.
            //

            KEVENT ShutdownEvent;
            KeInitializeEvent(&ShutdownEvent, NotificationEvent, FALSE);
            WorkQueue->ShutdownEvent = &ShutdownEvent;
            XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
            KeWaitForSingleObject(
                &ShutdownEvent, Executive, KernelMode, FALSE, NULL);
        }
    }

    XdpDereferenceWorkQueue(WorkQueue);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpSetWorkQueuePriority(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ WORK_QUEUE_TYPE Priority
    )
{
    KIRQL OldIrql;

    XdpWorkQueueAcquireLock(WorkQueue, &OldIrql);
    WorkQueue->Priority = Priority;
    XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpInsertWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ SINGLE_LIST_ENTRY *WorkQueueEntry
    )
{
    KIRQL OldIrql;

    //
    // Inserts an item into a work-queue.
    //
    // Lock the work-queue and determine whether its work-item is scheduled.
    // If so, enqueue the item and return. Otherwise, schedule the work-item.
    //

    WorkQueueEntry->Next = NULL;

    XdpWorkQueueAcquireLock(WorkQueue, &OldIrql);
    if (WorkQueue->Tail == NULL) {

        //
        // The work-item is not scheduled. Schedule it now.
        //

        WorkQueue->Head = WorkQueueEntry;
        WorkQueue->Tail = WorkQueueEntry;
        XdpReferenceWorkQueue(WorkQueue);
        IoQueueWorkItemEx(
            WorkQueue->IoWorkItem, XdpIoWorkItemRoutine, WorkQueue->Priority,
            WorkQueue);
    } else {

        //
        // The work-item is already scheduled.
        // See if the queue has been flushed for processing.
        //

        if (WorkQueue->Head == NULL) {

            //
            // The queue has been flushed, so this is now the first item.
            //

            WorkQueue->Head = WorkQueueEntry;
            WorkQueue->Tail = WorkQueueEntry;
        } else {

            //
            // The queue hasn't been flushed, so this is now the last item.
            //

            WorkQueue->Tail->Next = WorkQueueEntry;
            WorkQueue->Tail = WorkQueueEntry;
        }
    }

    XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIoWorkItemRoutine(
    _In_opt_ VOID *IoObject,
    _In_opt_ VOID *Context,
    _In_ PIO_WORKITEM IoWorkItem
    )
{
    XDP_WORK_QUEUE *WorkQueue = (XDP_WORK_QUEUE *)Context;
    KIRQL OldIrql;

    TraceEnter(TRACE_RTL, "WorkQueue=%p IoObject=%p", WorkQueue, IoObject);

    UNREFERENCED_PARAMETER(IoWorkItem);
    ASSERT(WorkQueue);

    //
    // Process the contents of a work-queue.
    //
    XdpWorkQueueAcquireLock(WorkQueue, &OldIrql);

    ASSERT(WorkQueue->Head != NULL);
    do {
        SINGLE_LIST_ENTRY *Head;

        //
        // Flush the entire list by capturing its head, leaving the tail set
        // to indicate we're still working.
        //

        Head = WorkQueue->Head;
        WorkQueue->Head = NULL;
        XdpWorkQueueReleaseLock(WorkQueue, OldIrql);

        //
        // Process the list that we captured.
        //

        WorkQueue->Routine(Head);

        //
        // Go back to look for more.
        //

        XdpWorkQueueAcquireLock(WorkQueue, &OldIrql);
    } while(WorkQueue->Head != NULL);

    //
    // There's no more left, so set the tail to indicate we're not at work.
    // If the work-queue is being shut down, signal that we're done.
    //

    WorkQueue->Tail = NULL;
    if (WorkQueue->ShutdownEvent == NULL) {
        XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
    } else {
        XdpWorkQueueReleaseLock(WorkQueue, OldIrql);
        KeSetEvent(WorkQueue->ShutdownEvent, 0, FALSE);
    }

    //
    // The work queue holds an indirect reference on the ETW tracing provider,
    // so trace exit prematurely to ensure the log isn't dropped.
    //
    TraceExitSuccess(TRACE_RTL);

    XdpDereferenceWorkQueue(WorkQueue);
}
