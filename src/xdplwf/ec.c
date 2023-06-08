//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// For rudimentary fairness between components running at dispatch, limit the
// time in each DPC.
//
#define MAX_ITERATIONS_PER_DPC 8

typedef enum _XDP_EC_STATE {
    EcIdle,
    EcCleanedUp,
    EcPassiveWait,
    EcPassiveWake,
    EcPassiveQueue,
    EcPoll,
    EcDpcQueueMigrate,
    EcPassive,
    EcDpcQueue,
    EcDpcArm,
    EcDpcDequeue,
    EcDisarm,
    EcEnterInline,
    EcExitInline,
} XDP_EC_STATE;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpEcNotifyEx(
    _In_ XDP_EC *Ec,
    _In_ BOOLEAN CanInline
    );

static
_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
VOID
XdpEcPassiveWorker(
    _In_ VOID *Context
    )
{
    XDP_EC *Ec = Context;
    GROUP_AFFINITY Affinity = {0};
    GROUP_AFFINITY OldAffinity;
    PROCESSOR_NUMBER ProcessorNumber;
    ULONG CurrentProcessor;

    ASSERT(Ec != NULL);

    CurrentProcessor = ReadULongNoFence(&Ec->OwningProcessor);
    KeGetProcessorNumberFromIndex(CurrentProcessor, &ProcessorNumber);
    Affinity.Group = ProcessorNumber.Group;
    Affinity.Mask = AFFINITY_MASK(ProcessorNumber.Number);
    KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

    //
    // Set the thread priority to the same priority as the DelayedWorkQueue.
    //
    KeSetPriorityThread(KeGetCurrentThread(), 12);

    while (TRUE) {
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcPassiveWait);
        KeWaitForSingleObject(&Ec->PassiveEvent, Executive, KernelMode, FALSE, NULL);

        if (Ec->CleanupPassiveThread) {
            break;
        }

        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcPassiveWake);

        if (CurrentProcessor != Ec->OwningProcessor) {
            CurrentProcessor = Ec->OwningProcessor;
            KeGetProcessorNumberFromIndex(CurrentProcessor, &ProcessorNumber);
            Affinity.Group = ProcessorNumber.Group;
            Affinity.Mask = AFFINITY_MASK(ProcessorNumber.Number);
            KeSetSystemGroupAffinityThread(&Affinity, NULL);
        }

        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcPassiveQueue);
        KeInsertQueueDpc(&Ec->Dpc, NULL, NULL);
    }

    KeRevertToUserGroupAffinityThread(&OldAffinity);
}

static
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpEcInvokePoll(
    _In_ XDP_EC *Ec
    )
{
    BOOLEAN NeedPoll;

    ASSERT(!Ec->InPoll);
    Ec->InPoll = TRUE;

    NeedPoll = Ec->Poll(Ec->PollContext);

    ASSERT(Ec->InPoll);
    Ec->InPoll = FALSE;

    return NeedPoll;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpEcPoll(
    _In_ XDP_EC *Ec
    )
{
    BOOLEAN NeedPoll = FALSE;
    BOOLEAN NeedYieldCheck;
    LARGE_INTEGER CurrentTick;
    UINT32 Iteration = 0;

    EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcPoll);

    ASSERT(Ec->OwningProcessor == KeGetCurrentProcessorIndex());

    NeedYieldCheck = !Ec->SkipYieldCheck;
    Ec->SkipYieldCheck = FALSE;

    if (Ec->OwningProcessor != *Ec->IdealProcessor) {
        PROCESSOR_NUMBER ProcessorNumber;
        //
        // The target processor has changed. Invalidate this processor's claim
        // to the EC. The target processor will update the owning processor.
        //
        Ec->OwningProcessor = ULONG_MAX;
        KeGetProcessorNumberFromIndex(*Ec->IdealProcessor, &ProcessorNumber);
        KeSetTargetProcessorDpcEx(&Ec->Dpc, &ProcessorNumber);
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDpcQueueMigrate);
        KeInsertQueueDpc(&Ec->Dpc, NULL, NULL);
        return;
    }

    if (NeedYieldCheck) {
        KeQueryTickCount(&CurrentTick);
        if (Ec->LastYieldTick.QuadPart < CurrentTick.QuadPart) {
            Ec->LastYieldTick.QuadPart = CurrentTick.QuadPart;
            if (KeShouldYieldProcessor()) {
                //
                // The EC should yield the processor, so queue a passive work
                // item.
                //
                // Allow the DPC to skip the next yield check: on older systems
                // configured with DPC watchdog timeouts disabled,
                // KeShouldYieldProcessor may return true even if the CPU had
                // recently dropped below dispatch level, which causes this EC
                // to yield the processor prematurely, i.e. without ever polling
                // the callback. If there's a lot of DPC activity on the system,
                // starvation can occur in the degenerate case.
                //
                Ec->SkipYieldCheck = TRUE;
                EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcPassive);
                KeSetEvent(&Ec->PassiveEvent, 0, FALSE);
                return;
            }
        }
    }

    do {
        NeedPoll = XdpEcInvokePoll(Ec);
    } while (NeedPoll && ++Iteration < MAX_ITERATIONS_PER_DPC);

    if (NeedPoll) {
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDpcQueue);
        KeInsertQueueDpc(&Ec->Dpc, NULL, NULL);
    } else {
        //
        // No more work. Re-arm the EC and re-check the poll callback.
        //
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDpcArm);
        InterlockedExchange8((CHAR *)&Ec->Armed, TRUE);

        if (XdpEcInvokePoll(Ec)) {
            //
            // There is more work, after all.
            //
            XdpEcNotifyEx(Ec, FALSE);
        } else if (Ec->CleanupComplete != NULL) {
            //
            // Perform a final disarm of the EC to trigger cleanup completion.
            //
            if (InterlockedExchange8((CHAR *)&Ec->Armed, FALSE)) {
                //
                // The EC has successfully been shut down; any further
                // notifications are ignored, and the poll callback will never
                // be invoked.
                //
                KeSetEvent(Ec->CleanupComplete, 0, FALSE);
            } else {
                //
                // A notification disarmed the EC after this routine re-armed
                // the EC. That notification has in turn queued a DPC, so fall
                // through and do nothing here; that queued DPC will continue
                // where this routine left off.
                //
                // Since it is illegal for external notifications to occur or
                // for polling callbacks to request another poll after cleanup
                // is initiated, the notification that won the disarm race must
                // have been triggered by the cleanup routine itself, and so the
                // EC cannot fall through this path more than once.
                //
            }
        }
    }
}

static
_Function_class_(KDEFERRED_ROUTINE)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
XdpEcDpcThunk(
    _In_ struct _KDPC *Dpc,
    _In_opt_ VOID *DeferredContext,
    _In_opt_ VOID *SystemArgument1,
    _In_opt_ VOID *SystemArgument2
    )
{
    XDP_EC *Ec = DeferredContext;
    ULONG CurrentProcessor = KeGetCurrentProcessorIndex();

    EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDpcDequeue);

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(DeferredContext != NULL);

    Ec->OwningProcessor = CurrentProcessor;
    XdpEcPoll(DeferredContext);
}

static
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpEcIsSerializable(
    _In_ XDP_EC *Ec,
    _In_ ULONG CurrentProcessor
    )
{
    return CurrentProcessor == Ec->OwningProcessor && !Ec->InPoll;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpEcNotifyEx(
    _In_ XDP_EC *Ec,
    _In_ BOOLEAN CanInline
    )
{
    if (InterlockedExchange8((CHAR *)&Ec->Armed, FALSE)) {
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDisarm);

        KIRQL OldIrql = KeRaiseIrqlToDpcLevel();
        ULONG CurrentProcessor = KeGetCurrentProcessorIndex();

        if (CanInline && XdpEcIsSerializable(Ec, CurrentProcessor)) {
            XdpEcPoll(Ec);
        } else {
            EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcDpcQueue);
            KeInsertQueueDpc(&Ec->Dpc, NULL, NULL);
        }

        if (OldIrql != DISPATCH_LEVEL) {
            KeLowerIrql(OldIrql);
        }
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpEcNotify(
    _In_ XDP_EC *Ec
    )
{
    XdpEcNotifyEx(Ec, TRUE);
}

_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpEcEnterInline(
    _In_ XDP_EC *Ec,
    _In_ ULONG CurrentProcessor
    )
{
    if (XdpEcIsSerializable(Ec, CurrentProcessor)) {
        Ec->InPoll = TRUE;
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcEnterInline);
        return TRUE;
    }

    return FALSE;
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpEcExitInline(
    _In_ XDP_EC *Ec
    )
{
    ASSERT(KeGetCurrentProcessorIndex() == Ec->OwningProcessor);
    ASSERT(Ec->InPoll);
    Ec->InPoll = FALSE;
    EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcExitInline);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpEcCleanup(
    _In_ XDP_EC *Ec
    )
{
    if (Ec->PassiveThread != NULL) {
        KEVENT CleanupEvent;

        KeInitializeEvent(&CleanupEvent, NotificationEvent, FALSE);
        WritePointerRelease(&Ec->CleanupComplete, &CleanupEvent);
        XdpEcNotify(Ec);
        KeWaitForSingleObject(&CleanupEvent, Executive, KernelMode, FALSE, NULL);
        Ec->CleanupComplete = NULL;

        Ec->CleanupPassiveThread = TRUE;
        //
        // Rather than setting the priority boost while setting the event, which
        // is effective only if the thread was not already READY, permanently
        // boost the thread priority to expedite cleanup.
        //
        KeSetPriorityThread(Ec->PassiveThread, LOW_REALTIME_PRIORITY);
        KeSetEvent(&Ec->PassiveEvent, 0, FALSE);
        KeWaitForSingleObject(Ec->PassiveThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Ec->PassiveThread);
        EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcCleanedUp);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpEcInitialize(
    _Inout_ XDP_EC *Ec,
    _In_ XDP_EC_POLL_ROUTINE *Poll,
    _In_ VOID *PollContext,
    _In_ ULONG *IdealProcessor
    )
{
    NTSTATUS Status;
    PROCESSOR_NUMBER ProcessorNumber;
    HANDLE ThreadHandle = NULL;

    RtlZeroMemory(Ec, sizeof(*Ec));
    Ec->Poll = Poll;
    Ec->PollContext = PollContext;
    Ec->IdealProcessor = IdealProcessor;
    Ec->OwningProcessor = ReadULongNoFence(IdealProcessor);
    Ec->Armed = TRUE;

    KeInitializeDpc(&Ec->Dpc, XdpEcDpcThunk, Ec);
    KeGetProcessorNumberFromIndex(Ec->OwningProcessor, &ProcessorNumber);
    KeSetTargetProcessorDpcEx(&Ec->Dpc, &ProcessorNumber);
    KeInitializeEvent(&Ec->PassiveEvent, SynchronizationEvent, FALSE);

    //
    // Use MediumHigh importance to append the DPC to the tail of the DPC queue
    // (the same as the default) and force the DPC queue to be flushed. The
    // default DPC flushing behavior depends on whether the DPC is queued from
    // the target processor; if queued from another processor, the system is
    // allowed to defer the DPC.
    //
    // Depending on the workload, either Medium or MediumHigh importance could
    // be the best policy; TODO: allow DPC importance to be configured.
    //
    KeSetImportanceDpc(&Ec->Dpc, MediumHighImportance);

    Status =
        PsCreateSystemThread(
            &ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, XdpEcPassiveWorker, Ec);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        ObReferenceObjectByHandle(
            ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Ec->PassiveThread, NULL);
    FRE_ASSERT(NT_SUCCESS(Status));

    EventWriteEcStateChange(&MICROSOFT_XDP_PROVIDER, Ec, EcIdle);
    Status = STATUS_SUCCESS;

Exit:

    if (ThreadHandle != NULL) {
        ZwClose(ThreadHandle);
    }

    return Status;
}
