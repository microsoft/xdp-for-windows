//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "xdptimer.tmh"

typedef struct _EX_TIMER EX_TIMER;
typedef struct _IO_WORKITEM IO_WORKITEM;

typedef struct _XDP_TIMER {
    XDP_REFERENCE_COUNT ReferenceCount;
    EX_PUSH_LOCK PushLock;
    KSPIN_LOCK SpinLock;
    EX_TIMER *ExTimer;
    WORKER_THREAD_ROUTINE *TimerRoutine;
    VOID *TimerContext;
    KEVENT *CancelEvent;
    KEVENT *CleanupEvent;
    struct {
        BOOLEAN ExTimerInserted : 1;
        BOOLEAN WorkItemInserted : 1;
        BOOLEAN Shutdown : 1;
    } Flags;
    ULONG_PTR WorkItem[0];
} XDP_TIMER;

static EXT_CALLBACK XdpTimerTimeout;
static IO_WORKITEM_ROUTINE_EX XdpTimerWorker;

static
VOID
XdpTimerReference(
    _Inout_ XDP_TIMER *Timer
    )
{
    XdpIncrementReferenceCount(&Timer->ReferenceCount);
}

static
VOID
XdpTimerDereference(
    _Inout_ XDP_TIMER *Timer
    )
{
    if (XdpDecrementReferenceCount(&Timer->ReferenceCount)) {
        KEVENT *CleanupEvent = Timer->CleanupEvent;

        if (Timer->ExTimer != NULL) {
            ExDeleteTimer(Timer->ExTimer, FALSE, FALSE, NULL);
        }

        IoUninitializeWorkItem((IO_WORKITEM *)Timer->WorkItem);
        ExFreePoolWithTag(Timer, XDP_POOLTAG_TIMER);

        if (CleanupEvent != NULL) {
            KeSetEvent(CleanupEvent, 0, FALSE);
        }

        ExReleaseRundownProtection(&XdpRtlRundown);
    }
}


_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_TIMER *
XdpTimerCreate(
    _In_ WORKER_THREAD_ROUTINE *TimerRoutine,
    _In_opt_ VOID *TimerContext,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    )
{
    XDP_TIMER *Timer;
    VOID *IoObject;
    NTSTATUS Status;

    ASSERT((DriverObject != NULL) || (DeviceObject != NULL));
    IoObject = (DriverObject != NULL) ? (VOID *)DriverObject : DeviceObject;

    if (!ExAcquireRundownProtection(&XdpRtlRundown)) {
        return NULL;
    }

    Timer =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Timer) + IoSizeofWorkItem(), XDP_POOLTAG_TIMER);
    if (Timer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&Timer->ReferenceCount);
    ExInitializePushLock(&Timer->PushLock);
    KeInitializeSpinLock(&Timer->SpinLock);
    Timer->TimerRoutine = TimerRoutine;
    Timer->TimerContext = TimerContext;
    IoInitializeWorkItem(IoObject, (IO_WORKITEM *)Timer->WorkItem);

    Timer->ExTimer = ExAllocateTimer(XdpTimerTimeout, Timer, 0);
    if (Timer->ExTimer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Timer != NULL) {
            XdpTimerDereference(Timer);
            Timer = NULL;
        }

        ExReleaseRundownProtection(&XdpRtlRundown);
    }

    return Timer;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Timer->PushLock)
BOOLEAN
XdpTimerCancelUnderPushLock(
    _In_ XDP_TIMER *Timer
    )
{
    KEVENT CancelEvent;
    KIRQL OldIrql;
    BOOLEAN Canceled = FALSE;

    //
    // Cancels any outstanding timer. Returns TRUE if and only if a timer had
    // previously been started and was successfully canceled.
    //

    KeInitializeEvent(&CancelEvent, NotificationEvent, TRUE);

    KeAcquireSpinLock(&Timer->SpinLock, &OldIrql);

    if (Timer->Flags.ExTimerInserted) {
        if (ExCancelTimer(Timer->ExTimer, NULL)) {
            //
            // The ExTimer was successfully cancelled.
            //
            Timer->Flags.ExTimerInserted = FALSE;
            XdpTimerDereference(Timer);
        } else {
            KeClearEvent(&CancelEvent);
            FRE_ASSERT(Timer->CancelEvent == NULL);
            Timer->CancelEvent = &CancelEvent;
        }

        Canceled = TRUE;
    } else if (Timer->Flags.WorkItemInserted) {
        //
        // The ExTimer has fired, but the work item is still enqueued. Wait for
        // the timer to be canceled on the work queue.
        //
        Canceled = TRUE;
        KeClearEvent(&CancelEvent);
        FRE_ASSERT(Timer->CancelEvent == NULL);
        Timer->CancelEvent = &CancelEvent;
    }

    KeReleaseSpinLock(&Timer->SpinLock, OldIrql);

    KeWaitForSingleObject(&CancelEvent, Executive, KernelMode, FALSE, NULL);

    return Canceled;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerCancel(
    _In_ XDP_TIMER *Timer
    )
{
    BOOLEAN Canceled;

    RtlAcquirePushLockExclusive(&Timer->PushLock);

    Canceled = XdpTimerCancelUnderPushLock(Timer);

    RtlReleasePushLockExclusive(&Timer->PushLock);

    return Canceled;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerStart(
    _In_ XDP_TIMER *Timer,
    _In_ UINT32 DueTimeInMs,
    _Out_opt_ BOOLEAN *Started
    )
{
    KIRQL OldIrql;
    BOOLEAN Canceled;

    //
    // Cancels any outstanding timer and starts a new timer. Starting a timer
    // fails if the timer shutdown has begun. Returns TRUE if and only if a
    // timer had previously been started and was successfully canceled.
    //

    //
    // Acquire a pushlock to serialize multiple timer starts.
    //
    RtlAcquirePushLockExclusive(&Timer->PushLock);

    //
    // First, cancel any outstanding timer. This ensures the work item is
    // never double-enqueued.
    //
    Canceled = XdpTimerCancelUnderPushLock(Timer);

    KeAcquireSpinLock(&Timer->SpinLock, &OldIrql);

    //
    // If the timer isn't disabled for shutdown, set the next timer.
    //
    if (!Timer->Flags.Shutdown) {
        LONGLONG DueTime = -(LONGLONG)RTL_MILLISEC_TO_100NANOSEC(DueTimeInMs);

        XdpTimerReference(Timer);
        Timer->Flags.ExTimerInserted = TRUE;
        ExSetTimer(Timer->ExTimer, DueTime, 0, NULL);

        if (Started != NULL) {
            *Started = TRUE;
        }
    } else {
        if (Started != NULL) {
            *Started = FALSE;
        }
    }

    KeReleaseSpinLock(&Timer->SpinLock, OldIrql);

    RtlReleasePushLockExclusive(&Timer->PushLock);

    return Canceled;
}

_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerShutdown(
    _In_ XDP_TIMER *Timer,
    _In_ BOOLEAN Cancel,
    _In_ BOOLEAN Wait
    )
{
    KIRQL OldIrql;
    KEVENT CleanupEvent;
    BOOLEAN Canceled = FALSE;

    //
    // Runs down a timer. Returns TRUE if and only if a timer had previously
    // been started and was successfully canceled.
    //

    KeInitializeEvent(&CleanupEvent, NotificationEvent, TRUE);

    KeAcquireSpinLock(&Timer->SpinLock, &OldIrql);

    //
    // Prevent further timers being started.
    //
    Timer->Flags.Shutdown = TRUE;

    KeReleaseSpinLock(&Timer->SpinLock, OldIrql);

    //
    // If the caller asked to cancel timers, do so after the timer has been
    // disabled.
    //
    if (Cancel) {
        Canceled = XdpTimerCancel(Timer);
    }

    if (Wait) {
        KeClearEvent(&CleanupEvent);
        Timer->CleanupEvent = &CleanupEvent;
    }

    //
    // Release the original caller reference.
    //
    XdpTimerDereference(Timer);

    KeWaitForSingleObject(&CleanupEvent, Executive, KernelMode, FALSE, NULL);

    return Canceled;
}

static
_Function_class_(EXT_CALLBACK)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
XdpTimerTimeout(
    _In_ EX_TIMER *ExTimer,
    _In_opt_ VOID *Context
    )
{
    XDP_TIMER *Timer = Context;
    KEVENT *CancelEvent = NULL;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ExTimer);
    ASSERT(Timer);

    KeAcquireSpinLock(&Timer->SpinLock, &OldIrql);

    Timer->Flags.ExTimerInserted = FALSE;

    if (Timer->CancelEvent != NULL) {
        CancelEvent = Timer->CancelEvent;
        Timer->CancelEvent = NULL;
        KeSetEvent(CancelEvent, 0, FALSE);
    } else {
        IoQueueWorkItemEx((IO_WORKITEM *)Timer->WorkItem, XdpTimerWorker, NormalWorkQueue, Timer);
        Timer->Flags.WorkItemInserted = TRUE;
    }

    KeReleaseSpinLock(&Timer->SpinLock, OldIrql);

    if (CancelEvent != NULL) {
        XdpTimerDereference(Timer);
    }
}

static
_Function_class_(IO_WORKITEM_ROUTINE_EX)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
XdpTimerWorker(
    _In_ VOID *IoObject,
    _In_opt_ VOID *Context,
    _In_ IO_WORKITEM *IoWorkItem
    )
{
    XDP_TIMER *Timer = Context;
    KIRQL OldIrql;
    KEVENT *CancelEvent = NULL;

    TraceEnter(TRACE_RTL, "Timer=%p IoObject=%p", Timer, IoObject);

    UNREFERENCED_PARAMETER(IoWorkItem);
    ASSERT(Timer);

    KeAcquireSpinLock(&Timer->SpinLock, &OldIrql);

    Timer->Flags.WorkItemInserted = FALSE;

    //
    // If a cancel request is pending, do not invoke the callback.
    //
    if (Timer->CancelEvent != NULL) {
        CancelEvent = Timer->CancelEvent;
        Timer->CancelEvent = NULL;
        KeSetEvent(CancelEvent, 0, FALSE);
    }

    KeReleaseSpinLock(&Timer->SpinLock, OldIrql);

    if (CancelEvent == NULL) {
        Timer->TimerRoutine(Timer->TimerContext);
    }

    //
    // The work queue holds an indirect reference on the ETW tracing provider,
    // so trace exit prematurely to ensure the log isn't dropped.
    //
    TraceExitSuccess(TRACE_RTL);

    XdpTimerDereference(Timer);
}
