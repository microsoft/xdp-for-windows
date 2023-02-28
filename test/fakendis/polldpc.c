//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#define MAX_ITERATIONS_INLINE   1
#define MAX_ITERATIONS_PER_DPC  16

//
// The NDIS polling API uses a default thread priority of 10 for affinitized
// polling contexts, so use that here, too.
//
#define PASSIVE_THREAD_PRIORITY 10

#if DBG
//
// Use smaller quotas to reduce NBL chain lengths. Since debug builds of the
// NBL queue helpers traverse the complete NBL queue for each operation, we
// need to keep each queue short.
//
#define RX_QUOTA 16
#define TX_QUOTA 16
#else
//
// For release builds, use the same default quotas as the NDIS polling module.
//
#define RX_QUOTA 64
#define TX_QUOTA 64
#endif

typedef struct DECLSPEC_CACHEALIGN _NDIS_POLL_CPU {
    KDPC Dpc;
    BOOLEAN DpcActive;
    BOOLEAN MoreData;
    INT64 LastYieldTick;
    PIO_WORKITEM WorkItem;
    PKEVENT CleanupComplete;
    LIST_ENTRY Queues;
} NDIS_POLL_CPU, *PNDIS_POLL_CPU;

typedef struct _NDIS_POLL_CPU_EC {
    LIST_ENTRY Link;
    LONG Armed;
    BOOLEAN NeedCleanup;
    ULONG OwningCpu;
    PKEVENT CleanupComplete;

    //
    // To keep the critical fast path (poll is requested on current CPU) fast,
    // we schedule a separate DPC to shim cross-CPU poll requests.
    // This DPC is queued onto the target processor, which in turn queues the
    // poll request onto the regular lock-free DPC.
    //
    // There is probably a better way.
    //
    KDPC CrossCpuDpc;
} NDIS_POLL_CPU_EC, *PNDIS_POLL_CPU_EC;
C_ASSERT(sizeof(NDIS_POLL_CPU_EC) <= RTL_FIELD_SIZE(NDIS_POLL_QUEUE, Reserved));

_IRQL_requires_(DISPATCH_LEVEL)
VOID
NdisPollCpu(
    _In_ PNDIS_POLL_CPU PollCpu,
    _In_ UINT32 IterationQuota
    );

static
PNDIS_POLL_CPU PollCpus;

static
PNDIS_POLL_CPU_EC
NdisPollCpuGetEc(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    PNDIS_POLL_CPU_EC Ec = (PNDIS_POLL_CPU_EC)&Q->Reserved;

    ASSERT(Q->Ec == Ec);
    return Ec;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
VOID
NdisPollCpuEnqueue(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ ULONG Processor
    )
{
    PNDIS_POLL_CPU PollCpu = &PollCpus[Processor];
    PNDIS_POLL_CPU_EC Ec = NdisPollCpuGetEc(Q);
    ULONG IdealProcessor = ReadULongNoFence(&Q->IdealProcessor);

    // Enqueue the poll request onto the current CPU.

    ASSERT(Processor == KeGetCurrentProcessorIndex());
    ASSERT(Processor == Ec->OwningCpu);

    if (IdealProcessor != Ec->OwningCpu && IdealProcessor != NDIS_DATAPATH_POLL_ANY_PROCESSOR) {
        PROCESSOR_NUMBER ProcessorNumber;

        // The poll affinity has changed; update our target CPU.
        Ec->OwningCpu = IdealProcessor;
        KeGetProcessorNumberFromIndex(IdealProcessor, &ProcessorNumber);
        KeSetTargetProcessorDpcEx(&Ec->CrossCpuDpc, &ProcessorNumber);
        NT_VERIFY(KeInsertQueueDpc(&Ec->CrossCpuDpc, NULL, NULL));
        return;
    }

    InsertHeadList(&PollCpu->Queues, &Ec->Link);
    PollCpu->MoreData = TRUE;

    if (!PollCpu->DpcActive) {
        PollCpu->DpcActive = TRUE;
        NdisPollCpu(PollCpu, MAX_ITERATIONS_INLINE);
    }
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
NdisPollCpuNotify(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    PNDIS_POLL_CPU_EC Ec = NdisPollCpuGetEc(Q);

    // A poll has been requested. Ensure the poll is enqueued only once.

    if (InterlockedExchange(&Ec->Armed, FALSE)) {
        ULONG CurrentProcessor = KeGetCurrentProcessorIndex();

        if (Ec->OwningCpu == CurrentProcessor) {
            // Enqueue directly onto the current CPU's poll queue.
            NdisPollCpuEnqueue(Q, CurrentProcessor);
        } else {
            // Insert a DPC onto the target processor.
            NT_VERIFY(KeInsertQueueDpc(&Ec->CrossCpuDpc, NULL, NULL));
        }
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
static
VOID
NdisPollCpuPassiveWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context
    )
{
    PNDIS_POLL_CPU PollCpu = (PNDIS_POLL_CPU)Context;
    UINT32 ProcessorIndex = (UINT32)(PollCpu - PollCpus);
    GROUP_AFFINITY Affinity = {0};
    GROUP_AFFINITY OldAffinity;
    PROCESSOR_NUMBER ProcessorNumber;

    ASSERT(PollCpu);
    UNREFERENCED_PARAMETER(DeviceObject);

    KeGetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);
    Affinity.Group = ProcessorNumber.Group;
    Affinity.Mask = AFFINITY_MASK(ProcessorNumber.Number);
    KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

    KeInsertQueueDpc(&PollCpu->Dpc, NULL, NULL);

    KeRevertToUserGroupAffinityThread(&OldAffinity);
}

_Function_class_(KDEFERRED_ROUTINE)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
NdisPollCpuDpc(
    _In_ struct _KDPC *Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PNDIS_POLL_CPU PollCpu = (PNDIS_POLL_CPU)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    NdisPollCpu(PollCpu, MAX_ITERATIONS_PER_DPC);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
NdisPollCpu(
    _In_ PNDIS_POLL_CPU PollCpu,
    _In_ UINT32 IterationQuota
    )
{
    PLIST_ENTRY Entry;
    LARGE_INTEGER CurrentTick;
    ULONG Iteration = 0;

    //
    // The main poll loop.
    //

    KeQueryTickCount(&CurrentTick);
    if (PollCpu->LastYieldTick < CurrentTick.QuadPart) {
        PollCpu->LastYieldTick = CurrentTick.QuadPart;
        if (KeShouldYieldProcessor()) {
            IoQueueWorkItem(
                PollCpu->WorkItem, NdisPollCpuPassiveWorker,
                CustomPriorityWorkQueue + PASSIVE_THREAD_PRIORITY, PollCpu);
            return;
        }
    }

    do {
        PollCpu->MoreData = FALSE;
        Entry = PollCpu->Queues.Flink;

        while (Entry != &PollCpu->Queues) {
            PNDIS_POLL_CPU_EC Ec = CONTAINING_RECORD(Entry, NDIS_POLL_CPU_EC, Link);
            PNDIS_POLL_QUEUE Q = CONTAINING_RECORD(Ec, NDIS_POLL_QUEUE, Reserved);
            Entry = Entry->Flink;

            ASSERT(InterlockedOr(&Ec->Armed, 0) == FALSE);

            if (ReadBooleanAcquire(&Ec->NeedCleanup)) {
                RemoveEntryList(&Ec->Link);
                KeSetEvent(Ec->CleanupComplete, 0, FALSE);
                continue;
            }

            if (NdisPollInvokePoll(Q, RX_QUOTA, TX_QUOTA) || Q->BusyReferences > 0) {
                PollCpu->MoreData = TRUE;
                continue;
            }

            //
            // The NIC reported no more data, so remove it from our queue.
            //
            // We need to double-check each state after re-arming notifications.
            //

            RemoveEntryList(&Ec->Link);
            NT_VERIFY(InterlockedExchange(&Ec->Armed, TRUE) == FALSE);
            NdisPollEnableInterrupt(Q);

            if (Ec->NeedCleanup) {
                NdisPollCpuNotify(Q);
                continue;
            }

            if (NdisPollInvokePoll(Q, RX_QUOTA, TX_QUOTA)) {
                NdisPollCpuNotify(Q);
                continue;
            }
        }
    } while (PollCpu->MoreData && ++Iteration < IterationQuota);

    if (PollCpu->MoreData) {
        KeInsertQueueDpc(&PollCpu->Dpc, NULL, NULL);
        return;
    }

    PollCpu->DpcActive = FALSE;

    if (PollCpu->CleanupComplete != NULL) {
        KeSetEvent(PollCpu->CleanupComplete, 0, FALSE);
    }
}

_Function_class_(KDEFERRED_ROUTINE)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
NdisPollCrossCpuDpc(
    _In_ struct _KDPC *Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PNDIS_POLL_QUEUE Q = (PNDIS_POLL_QUEUE)DeferredContext;

    // A shim to enqueue the poll request onto the current CPU from another CPU.

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    NdisPollCpuEnqueue(Q, KeGetCurrentProcessorIndex());
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuInsert(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    PNDIS_POLL_CPU_EC Ec = (PNDIS_POLL_CPU_EC)&Q->Reserved;
    PROCESSOR_NUMBER ProcessorNumber;
    KIRQL OldIrql;

    // Take ownership of the poll queue.

    Q->Notify = NdisPollCpuNotify;

    RtlZeroMemory(Ec, sizeof(*Ec));
    KeInitializeDpc(&Ec->CrossCpuDpc, NdisPollCrossCpuDpc, Q);
    KeSetImportanceDpc(&Ec->CrossCpuDpc, MediumHighImportance);
    KeGetProcessorNumberFromIndex(Ec->OwningCpu, &ProcessorNumber);
    KeSetTargetProcessorDpcEx(&Ec->CrossCpuDpc, &ProcessorNumber);

    InterlockedExchange(&Ec->Armed, TRUE);

    if (Q->MiniportPollContext != NULL) {
        // This is a re-insert, so we must restart our poll loop.
        OldIrql = KeRaiseIrqlToDpcLevel();
        NdisPollCpuNotify(Q);
        KeLowerIrql(OldIrql);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuUpdate(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    UNREFERENCED_PARAMETER(Q);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuCleanup(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    PNDIS_POLL_CPU_EC Ec = NdisPollCpuGetEc(Q);
    KIRQL OldIrql;
    KEVENT CleanupComplete;

    // Remove the specified poll context. Either the NIC is deleting the queue
    // or a poll extension is acquring the queue.

    KeInitializeEvent(&CleanupComplete, NotificationEvent, FALSE);
    Ec->CleanupComplete = &CleanupComplete;
    WriteBooleanRelease(&Ec->NeedCleanup, TRUE);

    // Request a final notification to disarm and remove the poll queue.
    OldIrql = KeRaiseIrqlToDpcLevel();
    NdisPollCpuNotify(Q);
    KeLowerIrql(OldIrql);

    KeWaitForSingleObject(&CleanupComplete, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
NdisPollCpuStart(
    VOID
    )
{
    NTSTATUS Status;
    ULONG Count = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    SIZE_T AllocationSize = Count * sizeof(*PollCpus);

    PollCpus = ExAllocatePoolZero(NonPagedPoolNxCacheAligned, AllocationSize, POOLTAG_PERCPU);
    if (PollCpus == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    for (ULONG Index = 0; Index < Count; Index++) {
        PNDIS_POLL_CPU PollCpu = &PollCpus[Index];
        PROCESSOR_NUMBER ProcessorNumber;

        InitializeListHead(&PollCpu->Queues);
        KeGetProcessorNumberFromIndex(Index, &ProcessorNumber);
        KeInitializeDpc(&PollCpu->Dpc, NdisPollCpuDpc, PollCpu);
        KeSetTargetProcessorDpcEx(&PollCpu->Dpc, &ProcessorNumber);

        PollCpu->WorkItem = IoAllocateWorkItem(FndisDeviceObject);
        if (PollCpu->WorkItem == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

VOID
NdisPollCpuStop(
    VOID
    )
{
    ULONG Count = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ULONG ActiveCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    if (PollCpus != NULL) {
        for (ULONG Index = 0; Index < Count; Index++) {
            PNDIS_POLL_CPU PollCpu = &PollCpus[Index];
            KEVENT CleanupEvent;
            KIRQL OldIrql;

            if (PollCpu->WorkItem != NULL) {

                if (Index < ActiveCount) {
                    PROCESSOR_NUMBER Number;
                    GROUP_AFFINITY Affinity = {0};
                    GROUP_AFFINITY OldAffinity = {0};

                    KeInitializeEvent(&CleanupEvent, NotificationEvent, FALSE);
                    WritePointerRelease(&PollCpu->CleanupComplete, &CleanupEvent);

                    KeGetProcessorNumberFromIndex(Index, &Number);
                    Affinity.Group = Number.Group;
                    Affinity.Mask = 1ui64 << Number.Number;
                    KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

                    OldIrql = KeRaiseIrqlToDpcLevel();
                    if (!PollCpu->DpcActive) {
                        PollCpu->DpcActive = TRUE;
                        KeInsertQueueDpc(&PollCpu->Dpc, NULL, NULL);
                    }
                    KeLowerIrql(OldIrql);

                    KeRevertToUserGroupAffinityThread(&OldAffinity);

                    KeWaitForSingleObject(&CleanupEvent, Executive, KernelMode, FALSE, NULL);
                    PollCpu->CleanupComplete = NULL;
                }

#pragma warning(push)
#pragma warning(disable:6001) // WorkItem should be initialized.
                IoFreeWorkItem(PollCpu->WorkItem);
#pragma warning(pop)
            }
        }

        ExFreePoolWithTag(PollCpus, POOLTAG_PERCPU);
        PollCpus = NULL;
    }
}
