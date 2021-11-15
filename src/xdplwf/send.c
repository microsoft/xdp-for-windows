//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "send.tmh"

#define MAX_TX_BUFFER_LENGTH 65536
#define DEFAULT_TX_BUFFER_COUNT 32
#define MAX_TX_BUFFER_COUNT 8096

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxNotify(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue,
    _In_ ULONG Flags
    );

XDP_INTERFACE_NOTIFY_QUEUE XdpGenericTxNotifyQueue;

// Copy declaration from ntifs.h.
SLIST_ENTRY *
FASTCALL
InterlockedPushListSList(
    _Inout_ SLIST_HEADER *ListHead,
    _Inout_ __drv_aliasesMem SLIST_ENTRY *List,
    _Inout_ SLIST_ENTRY *ListEnd,
    _In_ ULONG Count
    );

typedef struct _NBL_TX_CONTEXT {
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue;
    UINT64 Address;
} NBL_TX_CONTEXT;

static
NBL_TX_CONTEXT *
NblTxContext(
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    //
    // Review: we could use the protocol or miniport reserved space for
    // regular TX and RX-inject respectively.
    //
    return (NBL_TX_CONTEXT *)NET_BUFFER_LIST_CONTEXT_DATA_START(NetBufferList);
}

static
ULONG_PTR
XdpGenericInjectCompleteClassify(
    _In_ VOID *ClassificationContext,
    _In_ NET_BUFFER_LIST *Nbl)
{
    if (Nbl->SourceHandle == ClassificationContext) {
        return (ULONG_PTR)NblTxContext(Nbl)->TxQueue;
    }

    return (ULONG_PTR)NULL;
}

static
VOID
XdpGenericInjectCompleteFlush(
    _In_ VOID *FlushContext,
    _In_ ULONG_PTR ClassificationResult,
    _In_ NBL_COUNTED_QUEUE *Queue)
{
    if (ClassificationResult == (ULONG_PTR)NULL) {
        NBL_QUEUE *PassList = (NBL_QUEUE *)FlushContext;

        NdisAppendNblChainToNblQueueFast(
            PassList, Queue->Queue.First,
            CONTAINING_RECORD(Queue->Queue.Last, NET_BUFFER_LIST, Next));
    } else {
        XDP_LWF_GENERIC_TX_QUEUE *TxQueue = (XDP_LWF_GENERIC_TX_QUEUE *)ClassificationResult;

        InterlockedPushListSList(
            &TxQueue->NblComplete,
            (SLIST_ENTRY *)&Queue->Queue.First->Next,
            (SLIST_ENTRY *)Queue->Queue.Last,
            (ULONG)Queue->NblCount);

        XdpGenericTxNotify(TxQueue, XDP_NOTIFY_QUEUE_FLAG_TX);
    }
}

_IRQL_requires_(DISPATCH_LEVEL)
NET_BUFFER_LIST *
XdpGenericInjectNetBufferListsComplete(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists
    )
{
    NBL_QUEUE PassList;

    NdisInitializeNblQueue(&PassList);

    NdisClassifyNblChainByValueLookaheadWithCount(
        NetBufferLists, XdpGenericInjectCompleteClassify, Generic->NdisFilterHandle,
        XdpGenericInjectCompleteFlush, &PassList);

    return NdisGetNblChainFromNblQueue(&PassList);
}

_Use_decl_annotations_
VOID
XdpGenericSendNetBufferListsComplete(
    NDIS_HANDLE FilterModuleContext,
    NET_BUFFER_LIST *NetBufferLists,
    ULONG SendCompleteFlags
    )
{
    XDP_LWF_GENERIC *Generic = XdpGenericFromFilterContext(FilterModuleContext);
    KIRQL OldIrql = DISPATCH_LEVEL;

    if (!NDIS_TEST_SEND_COMPLETE_AT_DISPATCH_LEVEL(SendCompleteFlags)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    NetBufferLists = XdpGenericInjectNetBufferListsComplete(Generic, NetBufferLists);

    if (OldIrql != DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }

    if (NetBufferLists != NULL) {
        NdisFSendNetBufferListsComplete(
            Generic->NdisFilterHandle, NetBufferLists, SendCompleteFlags);
    }
}

_Use_decl_annotations_
VOID
XdpGenericSendNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    NET_BUFFER_LIST *NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG SendFlags
    )
{
    XDP_LWF_GENERIC *Generic = XdpGenericFromFilterContext(FilterModuleContext);
    NBL_COUNTED_QUEUE PassList;
    NBL_QUEUE DropList;

    ASSERT(PortNumber == NDIS_DEFAULT_PORT_NUMBER);

    XdpGenericReceive(
        Generic, NetBufferLists, PortNumber, &PassList, &DropList,
        (SendFlags & XDP_LWF_GENERIC_INSPECT_NDIS_TX_MASK) | XDP_LWF_GENERIC_INSPECT_FLAG_TX);

    if (!NdisIsNblCountedQueueEmpty(&PassList)) {
        NdisFSendNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&PassList),
            PortNumber, SendFlags);
    }

    if (!NdisIsNblQueueEmpty(&DropList)) {
        NdisFSendNetBufferListsComplete(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&DropList),
            NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ?
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

ULONG
XdpGenericTxGetNbls(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    return TxQueue->BufferCount - TxQueue->OutstandingCount;
}

VOID
XdpGenericBuildTxNbl(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue,
    _In_ XDP_BUFFER *Buffer,
    _In_ XDP_BUFFER_MDL *BufferMdl,
    _Inout_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    MDL *Mdl = NET_BUFFER_FIRST_MDL(Nb);
    IoBuildPartialMdl(
        BufferMdl->Mdl, Mdl,
        (UCHAR *)MmGetMdlVirtualAddress(BufferMdl->Mdl)
            + BufferMdl->MdlOffset
            + Buffer->DataOffset,
        Buffer->DataLength);
    // work around KDNIC bug: it touches the user StartVa in a system context.
    Mdl->StartVa = (UCHAR *)Mdl->MappedSystemVa - Mdl->ByteOffset;
    NET_BUFFER_DATA_LENGTH(Nb) = Buffer->DataLength;
    NET_BUFFER_DATA_OFFSET(Nb) = 0;
    NET_BUFFER_CURRENT_MDL_OFFSET(Nb) = 0;
    NET_BUFFER_LIST_SET_HASH_VALUE(Nbl, TxQueue->RssQueue->RssHash);
    NblTxContext(Nbl)->TxQueue = TxQueue;
    NblTxContext(Nbl)->Address = BufferMdl->MdlOffset;
}

VOID
XdpGenericCompleteTx(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    NET_BUFFER_LIST *CompleteList;
    XDP_RING *Ring;

    if (ReadPointerAcquire(&TxQueue->XdpTxQueue) == NULL) {
        return;
    }

    Ring = TxQueue->CompletionRing;

    // TODO: replace s-list with MPSC queue.
    CompleteList = (NET_BUFFER_LIST *)InterlockedFlushSList(&TxQueue->NblComplete);

    while (CompleteList != NULL) {
        NET_BUFFER_LIST *Nbl;
        UINT64 *Completion;

        Nbl = CompleteList;
        CompleteList = CompleteList->Next;

        Completion = XdpRingGetElement(Ring, Ring->ProducerIndex++ & Ring->Mask);

        ASSERT(TxQueue == NblTxContext(Nbl)->TxQueue);
        *Completion = NblTxContext(Nbl)->Address;

        // Return the NBL to the TX free list.
        NT_VERIFY(TxQueue->OutstandingCount-- > 0);
        Nbl->Next = TxQueue->FreeNbls;
        TxQueue->FreeNbls = Nbl;

        if (XdpRingFree(Ring) == 0) {
            XdpFlushTransmit(TxQueue->XdpTxQueue);
        }
    }

    if (XdpRingCount(Ring) > 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }
}

BOOLEAN
XdpGenericInitiateTx(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    NBL_COUNTED_QUEUE Nbls;
    XDP_RING *FrameRing;
    ULONG NblsAvailable;

    if (ReadPointerAcquire(&TxQueue->XdpTxQueue) == NULL) {
        return FALSE;
    }

    FrameRing = TxQueue->FrameRing;

    NblsAvailable = XdpGenericTxGetNbls(TxQueue);
    if (NblsAvailable == 0) {
        return FALSE;
    }

    if (XdpRingCount(FrameRing) == 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);

        if (XdpRingCount(FrameRing) == 0) {
            return FALSE;
        }
    }

    NdisInitializeNblCountedQueue(&Nbls);

    while (Nbls.NblCount < NblsAvailable && XdpRingCount(FrameRing) > 0) {
        NET_BUFFER_LIST *Nbl;
        XDP_FRAME *Frame;
        XDP_BUFFER *Buffer;
        XDP_BUFFER_MDL *BufferMdl;

        Frame = XdpRingGetElement(FrameRing, FrameRing->ConsumerIndex++ & FrameRing->Mask);
        Buffer = &Frame->Buffer;
        BufferMdl = XdpGetMdlExtension(Buffer, &TxQueue->BufferMdlExtension);

        Nbl = TxQueue->FreeNbls;
        TxQueue->FreeNbls = TxQueue->FreeNbls->Next;
        XdpGenericBuildTxNbl(TxQueue, Buffer, BufferMdl, Nbl);

        NdisAppendSingleNblToNblCountedQueue(&Nbls, Nbl);
    }

    TxQueue->OutstandingCount += (ULONG)Nbls.NblCount;

    if (!TxQueue->Flags.RxInject) {
        NdisFSendNetBufferLists(
            TxQueue->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
            NDIS_DEFAULT_PORT_NUMBER, NDIS_SEND_FLAGS_DISPATCH_LEVEL);
    } else {
        NdisFIndicateReceiveNetBufferLists(
            TxQueue->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
            NDIS_DEFAULT_PORT_NUMBER, (ULONG)Nbls.NblCount, NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    }

    return XdpGenericTxGetNbls(TxQueue) > 0 && XdpRingCount(FrameRing) > 0;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpGenericDropTx(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    CONST UINT32 MaxDrops = 1024;

    if (ReadPointerAcquire(&TxQueue->XdpTxQueue) == NULL) {
        return FALSE;
    }

    FrameRing = TxQueue->FrameRing;
    CompletionRing = TxQueue->CompletionRing;

    if (XdpRingCount(FrameRing) == 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }

    for (UINT32 Drop = 0; XdpRingCount(FrameRing) > 0 && Drop < MaxDrops; Drop++) {
        XDP_FRAME *Frame;
        XDP_BUFFER *Buffer;
        XDP_BUFFER_MDL *BufferMdl;
        UINT64 *Completion;

        Frame = XdpRingGetElement(FrameRing, FrameRing->ConsumerIndex++ & FrameRing->Mask);
        Buffer = &Frame->Buffer;
        BufferMdl = XdpGetMdlExtension(Buffer, &TxQueue->BufferMdlExtension);

        ASSERT(XdpRingFree(CompletionRing) > 0);
        Completion =
            XdpRingGetElement(
                CompletionRing, CompletionRing->ProducerIndex++ & CompletionRing->Mask);
        *Completion = BufferMdl->MdlOffset;

        if (XdpRingFree(CompletionRing) == 0) {
            XdpFlushTransmit(TxQueue->XdpTxQueue);
        }
    }

    if (XdpRingCount(CompletionRing) > 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }

    return XdpRingCount(FrameRing) > 0;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpGenericTxPoll(
    _In_ VOID *PollContext
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = PollContext;

    XdpGenericCompleteTx(TxQueue);

    //
    // If the data path is pausing/paused, do not initiate new TX.
    //
    if (TxQueue->Flags.Pause) {
        if (TxQueue->OutstandingCount == 0) {
            if (TxQueue->PauseComplete != NULL) {
                KEVENT *PauseComplete = TxQueue->PauseComplete;
                TxQueue->PauseComplete = NULL;
                KeSetEvent(PauseComplete, 0, FALSE);
            }
        }

        //
        // Drop TX frames and complete them while pausing/paused so XDP doesn't
        // wait on outstanding TX forever during interface detach.
        //
        return XdpGenericDropTx(TxQueue);
    }

    return XdpGenericInitiateTx(TxQueue);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpGenericTxFlushRss(
    _In_ XDP_LWF_GENERIC_RSS_QUEUE *Queue,
    _In_ ULONG CurrentProcessor
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = ReadPointerNoFence(&Queue->TxQueue);
    XDP_LWF_GENERIC_TX_QUEUE *RxInjectQueue = ReadPointerNoFence(&Queue->RxInjectQueue);

    if (TxQueue != NULL && XdpEcEnterInline(&TxQueue->Ec, CurrentProcessor)) {
        //
        // Steal some RX cycles for TX.
        //
        (VOID)XdpGenericTxPoll(TxQueue);
        XdpEcExitInline(&TxQueue->Ec);
    }

    if (RxInjectQueue != NULL && XdpEcEnterInline(&RxInjectQueue->Ec, CurrentProcessor)) {
        //
        // Steal some RX cycles for RX-injection.
        //
        (VOID)XdpGenericTxPoll(RxInjectQueue);
        XdpEcExitInline(&RxInjectQueue->Ec);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxPauseQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    KEVENT TxPauseComplete;

    TraceEnter(
        TRACE_GENERIC, "TxQueue=%p IfIndex=%u QueueId=%u RxInject=%!BOOLEAN!",
        TxQueue, Generic->IfIndex, TxQueue->QueueId, TxQueue->Flags.RxInject);

    UNREFERENCED_PARAMETER(Generic);

    KeInitializeEvent(&TxPauseComplete, NotificationEvent, FALSE);
    TxQueue->PauseComplete = &TxPauseComplete;
    TxQueue->Flags.Pause = TRUE;
    XdpGenericTxNotify(TxQueue, XDP_NOTIFY_QUEUE_FLAG_TX);
    KeWaitForSingleObject(&TxPauseComplete, Executive, KernelMode, FALSE, NULL);
    FRE_ASSERT(TxQueue->PauseComplete == NULL);

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxRestartQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    UNREFERENCED_PARAMETER(Generic);

    TraceEnter(
        TRACE_GENERIC, "TxQueue=%p IfIndex=%u QueueId=%u RxInject=%!BOOLEAN!",
        TxQueue, Generic->IfIndex, TxQueue->QueueId, TxQueue->Flags.RxInject);

    TxQueue->Flags.Pause = FALSE;
    XdpGenericTxNotify(TxQueue, XDP_NOTIFY_QUEUE_FLAG_TX);

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxPause(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    TraceEnter(TRACE_GENERIC, "IfIndex=%u", Generic->IfIndex);

    LIST_ENTRY *Entry = Generic->Tx.Queues.Flink;
    while (Entry != &Generic->Tx.Queues) {
        XDP_LWF_GENERIC_TX_QUEUE *TxQueue =
            CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_TX_QUEUE, Link);
        Entry = Entry->Flink;

        XdpGenericTxPauseQueue(Generic, TxQueue);
    }

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 NewMtu
    )
{
    LIST_ENTRY *Entry = Generic->Tx.Queues.Flink;

    TraceEnter(TRACE_GENERIC, "IfIndex=%u NewMtu=%u", Generic->IfIndex, NewMtu);

    while (Entry != &Generic->Tx.Queues) {
        XDP_LWF_GENERIC_TX_QUEUE *TxQueue =
            CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_TX_QUEUE, Link);
        Entry = Entry->Flink;

        if (NewMtu != 0 && Generic->Tx.Mtu != NewMtu) {
            //
            // Notify XDP the MTU has changed; the queue will be deleted. Do not
            // restart the queue, since the MTU enforced by XDP could be greater
            // than originally reported by the NIC.
            //
            // If another NDIS component is not compatible with dynamic MTU
            // changes, NDIS will fully detach this LWF instead of a mere
            // pause/restart.
            //
            XdpTxQueueNotify(
                TxQueue->XdpNotifyHandle, XDP_TX_QUEUE_NOTIFY_MAX_FRAME_SIZE,
                &NewMtu, sizeof(NewMtu));
        } else {
            XdpGenericTxRestartQueue(Generic, TxQueue);
        }
    }

    Generic->Tx.Mtu = NewMtu;

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxNotify(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue,
    _In_ ULONG Flags
    )
{
    if (Flags & XDP_NOTIFY_QUEUE_FLAG_TX) {
        XdpEcNotify(&TxQueue->Ec);
    }
}

_Use_decl_annotations_
VOID
XdpGenericTxNotifyQueue(
    XDP_INTERFACE_HANDLE InterfaceQueue,
    XDP_NOTIFY_QUEUE_FLAGS Flags
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = (XDP_LWF_GENERIC_TX_QUEUE *)InterfaceQueue;

    XdpGenericTxNotify(TxQueue, Flags);
}

static CONST XDP_INTERFACE_TX_QUEUE_DISPATCH TxDispatch = {
    .InterfaceNotifyQueue = XdpGenericTxNotifyQueue,
};

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericTxCreateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    XDP_LWF_GENERIC *Generic = (XDP_LWF_GENERIC *)InterfaceContext;
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = NULL;
    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue;
    CONST XDP_QUEUE_INFO *QueueInfo;
    CONST XDP_HOOK_ID *QueueHookId;
    NTSTATUS Status;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams = {0};
    SIZE_T MdlSize;
    XDP_TX_CAPABILITIES TxCapabilities;
    XDP_EXTENSION_INFO ExtensionInfo;
    XDP_LWF_DATAPATH_BYPASS *Datapath = NULL;
    BOOLEAN NeedRestart = FALSE;
    XDP_HOOK_ID HookId = {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_TX,
        .SubLayer   = XDP_HOOK_INJECT,
    };

    ExAcquirePushLockExclusive(&Generic->Lock);

    QueueInfo = XdpTxQueueGetTargetQueueInfo(Config);

    QueueHookId = XdpTxQueueGetHookId(Config);
    if (QueueHookId != NULL) {
        HookId = *QueueHookId;
    }

    TraceEnter(
        TRACE_GENERIC,
        "IfIndex=%u QueueId=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
        Generic->IfIndex, QueueInfo->QueueId, HookId.Layer, HookId.Direction, HookId.SubLayer);

    if (QueueInfo->QueueType != XDP_QUEUE_TYPE_DEFAULT_RSS) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    RssQueue = XdpGenericRssGetQueueById(Generic, QueueInfo->QueueId);
    if (RssQueue == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (HookId.Layer != XDP_HOOK_L2 ||
        HookId.SubLayer != XDP_HOOK_INJECT ||
        (HookId.Direction != XDP_HOOK_TX && HookId.Direction != XDP_HOOK_RX)) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (Generic->Tx.Mtu == 0) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    MdlSize = MmSizeOfMdl((VOID *)(PAGE_SIZE - 1), MAX_TX_BUFFER_LENGTH);
    if (MdlSize > MAXUSHORT - sizeof(NBL_TX_CONTEXT)) {
        Status = STATUS_INVALID_BUFFER_SIZE;
        goto Exit;
    }

    TxQueue = ExAllocatePoolZero(NonPagedPoolNxCacheAligned, sizeof(*TxQueue), POOLTAG_SEND);
    if (TxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_TCP_IP;
    PoolParams.PoolTag = POOLTAG_BUFFER;
    PoolParams.fAllocateNetBuffer = TRUE;

    TxQueue->XdpNotifyHandle = XdpTxQueueGetNotifyHandle(Config);
    if (TxQueue->XdpNotifyHandle == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    // NBL context only aligns at void*. Ensure our packed structs are aligned.
    C_ASSERT(__alignof(NBL_TX_CONTEXT) <= __alignof(VOID *));
    C_ASSERT(__alignof(MDL) <= __alignof(NBL_TX_CONTEXT));
    PoolParams.ContextSize = (USHORT)(MdlSize + sizeof(NBL_TX_CONTEXT));

    TxQueue->NblPool = NdisAllocateNetBufferListPool(Generic->NdisFilterHandle, &PoolParams);
    if (TxQueue->NblPool == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status =
        XdpRegQueryDwordValue(
            XDP_LWF_PARAMETERS_KEY, L"GenericTxBufferCount", &TxQueue->BufferCount);
    if (!NT_SUCCESS(Status)) {
        TxQueue->BufferCount = DEFAULT_TX_BUFFER_COUNT;
        Status = STATUS_SUCCESS;
    } else if (TxQueue->BufferCount == 0 || TxQueue->BufferCount > MAX_TX_BUFFER_COUNT) {
        TraceWarn(
            TRACE_GENERIC, "IfIndex=%u QueueId=%u Invalid TX buffer count. Using default instead.",
            Generic->IfIndex, QueueInfo->QueueId);
        TxQueue->BufferCount = DEFAULT_TX_BUFFER_COUNT;
    }

    for (ULONG Index = 0; Index < TxQueue->BufferCount; Index++) {
        NET_BUFFER_LIST *Nbl;
        NET_BUFFER *Nb;
        MDL *Mdl;

        Nbl = NdisAllocateNetBufferList(TxQueue->NblPool, (USHORT)MdlSize, 0);
        if (Nbl == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
        Nbl->SourceHandle = Generic->NdisFilterHandle;
        Mdl = (MDL *)(NET_BUFFER_LIST_CONTEXT_DATA_START(Nbl) + sizeof(NBL_TX_CONTEXT));
        MmInitializeMdl(Mdl, NULL, MAX_TX_BUFFER_LENGTH);
        Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NET_BUFFER_FIRST_MDL(Nb) = Mdl;
        NET_BUFFER_CURRENT_MDL(Nb) = Mdl;
        Nbl->Next = TxQueue->FreeNbls;
        TxQueue->FreeNbls = Nbl;
    }

    InitializeSListHead(&TxQueue->NblComplete);
    TxQueue->Generic = Generic;
    TxQueue->QueueId = QueueInfo->QueueId;
    TxQueue->NdisFilterHandle = Generic->NdisFilterHandle;
    TxQueue->RssQueue = RssQueue;

    TxQueue->Flags.RxInject = (HookId.Direction == XDP_HOOK_RX);

    Status =
        XdpEcInitialize(
            &TxQueue->Ec, XdpGenericTxPoll, TxQueue, &TxQueue->RssQueue->IdealProcessor);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Datapath = TxQueue->Flags.RxInject ? &Generic->Rx.Datapath : &Generic->Tx.Datapath;
    XdpGenericReferenceDatapath(Generic, Datapath, &NeedRestart);
    if (NeedRestart) {
        TxQueue->Flags.Pause = TRUE;
    }

    InsertTailList(&Generic->Tx.Queues, &TxQueue->Link);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
        XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpTxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeTxCapabilitiesSystemMdl(&TxCapabilities);
    TxCapabilities.OutOfOrderCompletionEnabled = TRUE;
    TxCapabilities.MaximumBufferSize = MAX_TX_BUFFER_LENGTH;
    TxCapabilities.MaximumFrameSize = Generic->Tx.Mtu;
    XdpTxQueueSetCapabilities(Config, &TxCapabilities);

    *InterfaceTxQueue = (XDP_INTERFACE_HANDLE)TxQueue;
    *InterfaceTxQueueDispatch = &TxDispatch;

    Status = STATUS_SUCCESS;

Exit:

    ExReleasePushLockExclusive(&Generic->Lock);

    if (NT_SUCCESS(Status)) {
        LARGE_INTEGER Timeout;

        if (NeedRestart) {
            TraceVerbose(TRACE_GENERIC, "IfIndex=%u Requesting TX datapath attach", Generic->IfIndex);
            XdpGenericRequestRestart(Generic);
        }

        Timeout.QuadPart =
            -1 * RTL_MILLISEC_TO_100NANOSEC(GENERIC_DATAPATH_RESTART_TIMEOUT_MS);
        KeWaitForSingleObject(
            &Datapath->ReadyEvent, Executive, KernelMode, FALSE, &Timeout);

        NT_VERIFY(InterlockedExchangePointer(
            TxQueue->Flags.RxInject ?
                &TxQueue->RssQueue->RxInjectQueue : &TxQueue->RssQueue->TxQueue,
            TxQueue) == NULL);
    } else {
        if (TxQueue != NULL) {
            XdpEcCleanup(&TxQueue->Ec);
            if (TxQueue->NblPool != NULL) {
                while (TxQueue->FreeNbls != NULL) {
                    NET_BUFFER_LIST *Nbl = TxQueue->FreeNbls;
                    TxQueue->FreeNbls = Nbl->Next;
                    NdisFreeNetBufferList(Nbl);
                }
                NdisFreeNetBufferListPool(TxQueue->NblPool);
                TxQueue->NblPool = NULL;
            }
            ExFreePoolWithTag(TxQueue, POOLTAG_SEND);
        }
    }

    TraceExitStatus(TRACE_GENERIC);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxActivateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = (XDP_LWF_GENERIC_TX_QUEUE *)InterfaceTxQueue;
    XDP_LWF_GENERIC *Generic = TxQueue->Generic;
    XDP_EXTENSION_INFO ExtensionInfo;

    ExAcquirePushLockExclusive(&Generic->Lock);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
        XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpTxQueueGetExtension(Config, &ExtensionInfo, &TxQueue->BufferMdlExtension);

    ASSERT(XdpTxQueueIsOutOfOrderCompletionEnabled(Config));
    ASSERT(!XdpTxQueueIsFragmentationEnabled(Config));

    TxQueue->FrameRing = XdpTxQueueGetFrameRing(Config);
    TxQueue->CompletionRing = XdpTxQueueGetCompletionRing(Config);

    WritePointerRelease(&TxQueue->XdpTxQueue, XdpTxQueue);

    ExReleasePushLockExclusive(&Generic->Lock);
}

VOID
XdpGenericFreeTxQueue(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue =
        CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_TX_QUEUE, DeleteEntry);

    XdpEcCleanup(&TxQueue->Ec);
    KeSetEvent(TxQueue->DeleteComplete, 0, FALSE);
    ExFreePoolWithTag(TxQueue, POOLTAG_SEND);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxDeleteQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = (XDP_LWF_GENERIC_TX_QUEUE *)InterfaceTxQueue;
    XDP_LWF_GENERIC *Generic = TxQueue->Generic;
    XDP_LWF_DATAPATH_BYPASS *Datapath;
    BOOLEAN NeedRestart;
    KEVENT DeleteComplete;

    TraceEnter(TRACE_GENERIC, "IfIndex=%u QueueId=%u", Generic->IfIndex, TxQueue->QueueId);

    ASSERT(TxQueue->OutstandingCount == 0);

    ExAcquirePushLockExclusive(&Generic->Lock);

    NT_VERIFY(InterlockedExchangePointer(
        TxQueue->Flags.RxInject ?
            &TxQueue->RssQueue->RxInjectQueue : &TxQueue->RssQueue->TxQueue,
        NULL) == TxQueue);

    if (!TxQueue->Flags.Pause) {
        XdpGenericTxPauseQueue(Generic, TxQueue);
    }

    while (TxQueue->FreeNbls != NULL) {
        NET_BUFFER_LIST *Nbl;
        Nbl = TxQueue->FreeNbls;
        TxQueue->FreeNbls = Nbl->Next;
        NdisFreeNetBufferList(Nbl);
    }
    NdisFreeNetBufferListPool(TxQueue->NblPool);
    TxQueue->NblPool = NULL;

    RemoveEntryList(&TxQueue->Link);

    Datapath =
        TxQueue->Flags.RxInject ?
            &Generic->Rx.Datapath : &Generic->Tx.Datapath;
    XdpGenericDereferenceDatapath(Generic, Datapath, &NeedRestart);

    ExReleasePushLockExclusive(&Generic->Lock);

    if (NeedRestart) {
        TraceVerbose(TRACE_GENERIC, "IfIndex=%u Requesting TX datapath detach", Generic->IfIndex);
        XdpGenericRequestRestart(Generic);
    }

    KeInitializeEvent(&DeleteComplete, NotificationEvent, FALSE);
    TxQueue->DeleteComplete = &DeleteComplete;
    XdpLifetimeDelete(XdpGenericFreeTxQueue, &TxQueue->DeleteEntry);
    KeWaitForSingleObject(&DeleteComplete, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_GENERIC);
}
