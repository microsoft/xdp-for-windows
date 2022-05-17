//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

EX_PUSH_LOCK PollQueueLock;
LIST_ENTRY PollQueueList;

static
VOID
NdisPollReferenceQueue(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    InterlockedIncrement(&Q->ReferenceCount);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollDereferenceQueue(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    if (InterlockedDecrement(&Q->ReferenceCount) == 0) {
        ExFreePoolWithTag(Q, POOLTAG_POLL);
    }
}

static
_IRQL_requires_(DISPATCH_LEVEL)
VOID
NdisRequestPollAtDpc(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    NDIS_POLL_NOTIFY *NotifyRoutine;

    //
    // Check if the queue is running. The queue is paused during EC migrations.
    //

#pragma warning(push)
#pragma warning(disable:4152) // function/data pointer conversion
    NotifyRoutine = ReadPointerNoFence((PVOID *)&Q->Notify);
#pragma warning(pop)
    if (NotifyRoutine != NULL) {
        NotifyRoutine(Q);
    }
}

_Function_class_(KDEFERRED_ROUTINE)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
NdisRequestPollDpc(
    _In_ struct _KDPC *Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PNDIS_POLL_QUEUE Q = (PNDIS_POLL_QUEUE)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    NdisRequestPollAtDpc(Q);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
NdisPollInvokePoll(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ ULONG RxQuota,
    _In_ ULONG TxQuota
    )
{
    NDIS_POLL_DATA Poll = {0};

    Poll.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Poll.Header.Revision = NDIS_POLL_DATA_REVISION_1;
    Poll.Header.Size = sizeof(Poll);

    Poll.Receive.MaxNblsToIndicate = RxQuota;
    Poll.Transmit.MaxNblsToComplete = TxQuota;

    Q->Poll(Q->MiniportPollContext, &Poll);

    if (Poll.Receive.IndicatedNblChain != NULL) {
        NdisMIndicateReceiveNetBufferLists(
            Q->MiniportAdapterHandle, Poll.Receive.IndicatedNblChain,
            NDIS_DEFAULT_PORT_NUMBER, Poll.Receive.NumberOfIndicatedNbls, 0);
    }
    if (Poll.Transmit.CompletedNblChain != NULL) {
        NdisMSendNetBufferListsComplete(
            Q->MiniportAdapterHandle, Poll.Transmit.CompletedNblChain, 0);
    }

    return
        Poll.Receive.IndicatedNblChain != NULL ||
        Poll.Receive.Reserved1[RxXdpFramesAbsorbed] > 0 ||
        Poll.Transmit.CompletedNblChain ||
        Poll.Transmit.Reserved1[TxXdpFramesCompleted] > 0 ||
        Poll.Transmit.Reserved1[TxXdpFramesTransmitted] > 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NdisPollEnableInterrupt(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    NDIS_POLL_NOTIFICATION InterruptParams = {0};

    InterruptParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    InterruptParams.Header.Revision = NDIS_POLL_NOTIFICATION_REVISION_1;
    InterruptParams.Header.Size = sizeof(InterruptParams);

    InterruptParams.Enabled = TRUE;
    Q->Interrupt(Q->MiniportPollContext, &InterruptParams);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollMigrateQueue(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ PVOID Ec,
    _In_ NDIS_POLL_INSERT *Insert,
    _In_ NDIS_POLL_UPDATE *Update,
    _In_ NDIS_POLL_CLEANUP *Cleanup
    )
{
    NTSTATUS Status;

    RtlAcquirePushLockExclusive(&Q->Lock);

    if (Ec == NULL) {
        RtlFailFast(FAST_FAIL_INVALID_ARG);
    }

    if (Q->Deleted) {
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    if (Insert != NdisPollCpuInsert && (Q->Ec != Q->Reserved || Q->BusyReferences > 0)) {
        // Do not allow poll extensions to steal queues from each other.
        Status = STATUS_SHARING_VIOLATION;
        goto Exit;
    }

    if (Q->Ec != NULL) {
        //
        // Remove the old EC.
        //
        Q->Cleanup(Q);
        Q->Notify = NULL;
        KeRemoveQueueDpcEx(&Q->Dpc, TRUE);
    }

#if DBG
    Q->Cleanup = NULL;
    Q->Update = NULL;
    Q->Ec = NULL;
#endif

    //
    // Transfer the queue to the new EC.
    //
    Q->Ec = Ec;
    Q->Update = Update;
    Q->Cleanup = Cleanup;
    Insert(Q);

    Status = STATUS_SUCCESS;

Exit:

    RtlReleasePushLockExclusive(&Q->Lock);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollAcquireQueue(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ PVOID Ec,
    _In_ NDIS_POLL_INSERT *Insert,
    _In_ NDIS_POLL_UPDATE *Update,
    _In_ NDIS_POLL_CLEANUP *Cleanup
    )
{
    return NdisPollMigrateQueue(Q, Ec, Insert, Update, Cleanup);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollReleaseQueue(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    //
    // Restore the default NDIS poll.
    //
    (VOID)NdisPollMigrateQueue(
        Q, &Q->Reserved, NdisPollCpuInsert, NdisPollCpuUpdate,
        NdisPollCpuCleanup);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollAddBusyReference(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    NTSTATUS Status = STATUS_INVALID_DEVICE_STATE;

    RtlAcquirePushLockExclusive(&Q->Lock);

    //
    // Only the default EC supports busy references.
    //
    if (Q->Ec == Q->Reserved) {
        if (Q->BusyReferences++ == 0) {
            FNdisRequestPoll((NDIS_HANDLE)Q, 0);
        }
        Status = STATUS_SUCCESS;
    }

    RtlReleasePushLockExclusive(&Q->Lock);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollReleaseBusyReference(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    RtlAcquirePushLockExclusive(&Q->Lock);
    ASSERT(Q->Ec == Q->Reserved);
    NT_VERIFY(Q->BusyReferences-- > 0);
    RtlReleasePushLockExclusive(&Q->Lock);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollFindAndReferenceQueueByHandle(
    _In_ NDIS_HANDLE PollHandle,
    _Out_ PNDIS_POLL_QUEUE *FoundQueue
    )
{
    NTSTATUS Status;
    PNDIS_POLL_QUEUE Q = NULL;
    PLIST_ENTRY Entry;

    RtlAcquirePushLockShared(&PollQueueLock);
    Entry = PollQueueList.Flink;
    while (Entry != &PollQueueList) {
        PNDIS_POLL_QUEUE Candidate =
            CONTAINING_RECORD(Entry, NDIS_POLL_QUEUE, QueueLink);
        Entry = Entry->Flink;

        if (Candidate == (PNDIS_POLL_QUEUE)PollHandle) {
            Q = Candidate;
            NdisPollReferenceQueue(Q);
            break;
        }
    }
    RtlReleasePushLockShared(&PollQueueLock);

    if (Q == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    *FoundQueue = Q;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}


_IRQL_requires_(PASSIVE_LEVEL)
NDIS_STATUS
FNdisRegisterPoll(
    _In_ NDIS_HANDLE NdisHandle,
    _In_opt_ VOID *Context,
    _In_ NDIS_POLL_CHARACTERISTICS CONST *Characteristics,
    _Out_ NDIS_POLL_HANDLE *PollHandle
    )
{
    NTSTATUS Status;
    PNDIS_POLL_QUEUE Q;

    Q = ExAllocatePoolZero(NonPagedPoolNxCacheAligned, sizeof(*Q), POOLTAG_POLL);
    if (Q == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ExInitializePushLock(&Q->Lock);
    Q->ReferenceCount = 1;
    Q->MiniportAdapterHandle = NdisHandle;
    Q->IdealProcessor = NDIS_DATAPATH_POLL_ANY_PROCESSOR;
    KeInitializeDpc(&Q->Dpc, NdisRequestPollDpc, Q);

    // Initialize to the default NDIS poll.
    Status =
        NdisPollMigrateQueue(
            Q, &Q->Reserved, NdisPollCpuInsert, NdisPollCpuUpdate,
            NdisPollCpuCleanup);
    ASSERT(NT_SUCCESS(Status));

    // Register the miniport callbacks after initializing the EC: if callbacks
    // are non-NULL, the EC is permitted to call back into the NIC immediately.
    Q->MiniportPollContext = Context;
    Q->Poll = Characteristics->PollHandler;
    Q->Interrupt = Characteristics->SetPollNotificationHandler;

    RtlAcquirePushLockExclusive(&PollQueueLock);
    InsertTailList(&PollQueueList, &Q->QueueLink);
    RtlReleasePushLockExclusive(&PollQueueLock);

    *PollHandle = (NDIS_POLL_HANDLE)Q;
    Status = STATUS_SUCCESS;

    // Start the polling state machine on behalf of the caller.
    FNdisRequestPoll(*PollHandle, NULL);
Exit:

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
FNdisDeregisterPoll(
    _In_ NDIS_POLL_HANDLE PollHandle
    )
{
    PNDIS_POLL_QUEUE Q = (PNDIS_POLL_QUEUE)PollHandle;

    RtlAcquirePushLockExclusive(&PollQueueLock);
    RemoveEntryList(&Q->QueueLink);
    RtlReleasePushLockExclusive(&PollQueueLock);

    RtlAcquirePushLockExclusive(&Q->Lock);
    Q->Deleted = TRUE;
    // Ensure any DPCs queued by the ISR have completed.
    KeRemoveQueueDpcEx(&Q->Dpc, TRUE);
    Q->Cleanup(Q);
    RtlReleasePushLockExclusive(&Q->Lock);
    NdisPollDereferenceQueue(Q);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FNdisSetPollAffinity(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _In_ PROCESSOR_NUMBER CONST *PollAffinity
    )
{
    PNDIS_POLL_QUEUE Q = (PNDIS_POLL_QUEUE)PollHandle;
    ULONG Processor = KeGetProcessorIndexFromNumber((PROCESSOR_NUMBER *)PollAffinity);

    RtlAcquirePushLockExclusive(&Q->Lock);
    Q->IdealProcessor = Processor;
    Q->Update(Q);
    RtlReleasePushLockExclusive(&Q->Lock);
}

_IRQL_requires_max_(HIGH_LEVEL)
VOID
FNdisRequestPoll(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _Reserved_ VOID *Reserved
    )
{
    PNDIS_POLL_QUEUE Q = (PNDIS_POLL_QUEUE)PollHandle;

    UNREFERENCED_PARAMETER(Reserved);

    if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
        NT_VERIFY(KeInsertQueueDpc(&Q->Dpc, NULL, NULL));
    } else {
        KIRQL OldIrql;
        // Uncommon case. We need to raise IRQL to synchronize with migration.
        OldIrql = KeRaiseIrqlToDpcLevel();
        NdisRequestPollAtDpc(Q);
        KeLowerIrql(OldIrql);
    }
}

NTSTATUS
NdisPollStart(
    VOID
    )
{
    ExInitializePushLock(&PollQueueLock);
    InitializeListHead(&PollQueueList);

    return STATUS_SUCCESS;
}

VOID
NdisPollStop(
    VOID
    )
{
}
