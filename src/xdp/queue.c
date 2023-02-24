//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"


//
// Data path routines.
//

__declspec(noinline)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpQueueDatapathSyncSlow(
    _In_ XDP_QUEUE_SYNC *Sync
    )
{
    LIST_ENTRY SyncList;
    LIST_ENTRY *Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Sync->Lock, &OldIrql);

    InitializeListHead(&SyncList);
    AppendTailList(&SyncList, &Sync->PendingList);
    RemoveEntryList(&Sync->PendingList);
    InitializeListHead(&Sync->PendingList);

    KeReleaseSpinLock(&Sync->Lock, OldIrql);

    while ((Entry = RemoveHeadList(&SyncList)) != &SyncList) {
        XDP_QUEUE_SYNC_ENTRY *SyncEntry = CONTAINING_RECORD(Entry, XDP_QUEUE_SYNC_ENTRY, Link);

        SyncEntry->Callback(SyncEntry->CallbackContext);
    }
}

VOID
XdpQueueSyncInitialize(
    _Out_ XDP_QUEUE_SYNC *Sync
    )
{
    KeInitializeSpinLock(&Sync->Lock);
    InitializeListHead(&Sync->PendingList);
}

VOID
XdpQueueSyncInsert(
    _In_ XDP_QUEUE_SYNC *Sync,
    _In_ XDP_QUEUE_SYNC_ENTRY *Entry,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    )
{
    KIRQL OldIrql;

    Entry->Callback = Callback;
    Entry->CallbackContext = CallbackContext;

    KeAcquireSpinLock(&Sync->Lock, &OldIrql);
    InsertTailList(&Sync->PendingList, &Entry->Link);
    KeReleaseSpinLock(&Sync->Lock, OldIrql);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpQueueBlockingSyncCallback(
    _In_opt_ VOID *CallbackContext
    )
{
    XDP_QUEUE_BLOCKING_SYNC_CONTEXT *SyncContext = CallbackContext;

    ASSERT(SyncContext);
    SyncContext->Callback(SyncContext->CallbackContext);

    KeSetEvent(&SyncContext->Event, 0, FALSE);
}

VOID
XdpQueueBlockingSyncInsert(
    _In_ XDP_QUEUE_SYNC *Sync,
    _In_ XDP_QUEUE_BLOCKING_SYNC_CONTEXT *Entry,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    )
{
    Entry->Callback = Callback;
    Entry->CallbackContext = CallbackContext;
    KeInitializeEvent(&Entry->Event, SynchronizationEvent, FALSE);

    XdpQueueSyncInsert(Sync, &Entry->SyncEntry, XdpQueueBlockingSyncCallback, Entry);
}

VOID
XdpInitializeQueueInfo(
    _Out_ XDP_QUEUE_INFO *QueueInfo,
    _In_ XDP_QUEUE_TYPE QueueType,
    _In_ UINT32 QueueId
    )
{
    RtlZeroMemory(QueueInfo, sizeof(*QueueInfo));
    QueueInfo->Header.Revision = XDP_QUEUE_INFO_REVISION_1;
    QueueInfo->Header.Size = XDP_SIZEOF_QUEUE_INFO_REVISION_1;

    QueueInfo->QueueType = QueueType;
    QueueInfo->QueueId = QueueId;
}

#if DBG

VOID
XdpDbgInitializeQueueEc(
    _Out_ XDP_DBG_QUEUE_EC *Ec
    )
{
    RtlZeroMemory(Ec, sizeof(*Ec));
    KeInitializeSpinLock(&Ec->Lock);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgEnterQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec
    )
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ec->Lock, &OldIrql);

    ASSERT(!Ec->Active);
    ASSERT(!Ec->Flushed);
    Ec->Active = TRUE;

    KeReleaseSpinLock(&Ec->Lock, OldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgFlushQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec
    )
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ec->Lock, &OldIrql);

    ASSERT(Ec->Active);

    Ec->NotifyFlags = 0;
    Ec->FlushQpc = KeQueryPerformanceCounter(NULL);
    Ec->FlushCpu = KeGetCurrentProcessorIndex();
    Ec->Flushed = TRUE;

    KeReleaseSpinLock(&Ec->Lock, OldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgExitQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec
    )
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ec->Lock, &OldIrql);

    ASSERT(Ec->Active);
    ASSERT(Ec->Flushed);
    Ec->Active = FALSE;
    Ec->Flushed = FALSE;

    KeReleaseSpinLock(&Ec->Lock, OldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgNotifyQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec,
    _In_ UINT32 NotifyFlags
    )
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ec->Lock, &OldIrql);

    Ec->NotifyFlags |= NotifyFlags;
    Ec->NotifyQpc = KeQueryPerformanceCounter(NULL);
    Ec->NotifyCpu = KeGetCurrentProcessorIndex();

    KeReleaseSpinLock(&Ec->Lock, OldIrql);
}

#endif
