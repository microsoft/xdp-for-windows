//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "send.tmh"

#define MAX_TX_BUFFER_LENGTH 65536
#define DEFAULT_TX_FRAME_COUNT 32
#define MAX_TX_FRAME_COUNT 8096

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
    XDP_LWF_GENERIC_INJECTION_TYPE InjectionType;
    UINT64 BufferAddress;
    XDP_TX_FRAME_COMPLETION_CONTEXT CompletionContext;
} NBL_TX_CONTEXT;

C_ASSERT(
    FIELD_OFFSET(NBL_TX_CONTEXT, TxQueue) ==
    FIELD_OFFSET(XDP_LWF_GENERIC_INJECTION_CONTEXT, InjectionCompletionContext));
C_ASSERT(
    FIELD_OFFSET(NBL_TX_CONTEXT, InjectionType) ==
    FIELD_OFFSET(XDP_LWF_GENERIC_INJECTION_CONTEXT, InjectionType));

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

VOID
XdpGenericSendInjectComplete(
    _In_ VOID *ClassificationResult,
    _In_ NBL_COUNTED_QUEUE *Queue
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = ClassificationResult;

    EventWriteGenericTxCompleteBatch(&MICROSOFT_XDP_PROVIDER, TxQueue, Queue->NblCount);

    InterlockedPushListSList(
        &TxQueue->NblComplete,
        (SLIST_ENTRY *)&Queue->Queue.First->Next,
        (SLIST_ENTRY *)Queue->Queue.Last,
        (ULONG)Queue->NblCount);

    XdpGenericTxNotify(TxQueue, XDP_NOTIFY_QUEUE_FLAG_TX);
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
    NBL_COUNTED_QUEUE TxList;
    BOOLEAN AtDispatch = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);

    ASSERT(PortNumber == NDIS_DEFAULT_PORT_NUMBER);

    XdpGenericReceive(
        Generic, NetBufferLists, PortNumber, &PassList, &DropList, &TxList,
        (SendFlags & XDP_LWF_GENERIC_INSPECT_NDIS_TX_MASK) | XDP_LWF_GENERIC_INSPECT_FLAG_TX);

    if (!NdisIsNblCountedQueueEmpty(&PassList)) {
        NdisFSendNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&PassList),
            PortNumber, SendFlags);
    }

    if (!NdisIsNblQueueEmpty(&DropList)) {
        NdisFSendNetBufferListsComplete(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&DropList),
            AtDispatch ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }

    if (!NdisIsNblCountedQueueEmpty(&TxList)) {
        NdisFIndicateReceiveNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&TxList), PortNumber,
            (ULONG)TxList.NblCount, AtDispatch ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

ULONG
XdpGenericTxGetNbls(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue
    )
{
    return TxQueue->FrameCount - TxQueue->OutstandingCount;
}

VOID
XdpGenericBuildTxNbl(
    _In_ XDP_LWF_GENERIC_TX_QUEUE *TxQueue,
    _In_ XDP_FRAME *Frame,
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
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
    NblTxContext(Nbl)->TxQueue = TxQueue;
    NblTxContext(Nbl)->InjectionType = XDP_LWF_GENERIC_INJECTION_SEND;
    NblTxContext(Nbl)->BufferAddress = BufferMdl->MdlOffset;

    if (TxQueue->Flags.TxCompletionContextEnabled) {
        NblTxContext(Nbl)->CompletionContext =
            *XdpGetFrameTxCompletionContextExtension(
                Frame, &TxQueue->FrameTxCompletionContextExtension);
    }
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
        XDP_TX_FRAME_COMPLETION *Completion;

        Nbl = CompleteList;
        CompleteList = CompleteList->Next;

        Completion = XdpRingGetElement(Ring, Ring->ProducerIndex++ & Ring->Mask);

        ASSERT(TxQueue == NblTxContext(Nbl)->TxQueue);
        Completion->BufferAddress = NblTxContext(Nbl)->BufferAddress;

        if (TxQueue->Flags.TxCompletionContextEnabled) {
            XDP_TX_FRAME_COMPLETION_CONTEXT *CompletionContext =
                XdpGetTxCompletionContextExtension(
                    Completion, &TxQueue->TxCompletionContextExtension);
            *CompletionContext = NblTxContext(Nbl)->CompletionContext;
        }

        //
        // In lieu of calling MmPrepareMdlForReuse, assert our MDL did not get
        // mapped by the memory manager: the original MDL should have been
        // mapped by XDP itself, and the partial MDL inherited that mapping,
        // precluding the need for it to be mapped by itself.
        //
        ASSERT(Nbl->FirstNetBuffer->Next == NULL);
        ASSERT(Nbl->FirstNetBuffer->MdlChain->Next == NULL);
        ASSERT(Nbl->FirstNetBuffer->MdlChain->MdlFlags & MDL_PARTIAL);
        ASSERT((Nbl->FirstNetBuffer->MdlChain->MdlFlags & MDL_PARTIAL_HAS_BEEN_MAPPED) == 0);

        if (Nbl->Status != NDIS_STATUS_SUCCESS) {
            STAT_INC(&TxQueue->PcwStats, FramesDroppedNic);
        }

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

        Frame = XdpRingGetElement(FrameRing, FrameRing->ConsumerIndex & FrameRing->Mask);
        Buffer = &Frame->Buffer;
        BufferMdl = XdpGetMdlExtension(Buffer, &TxQueue->BufferMdlExtension);

        Nbl = TxQueue->FreeNbls;
        TxQueue->FreeNbls = TxQueue->FreeNbls->Next;
        XdpGenericBuildTxNbl(TxQueue, Frame, Buffer, BufferMdl, Nbl);

        NdisAppendSingleNblToNblCountedQueue(&Nbls, Nbl);

        EventWriteGenericTxEnqueue(
            &MICROSOFT_XDP_PROVIDER, TxQueue, FrameRing->ConsumerIndex,
            TxQueue->Stats.BatchesPosted);

        FrameRing->ConsumerIndex++;
    }

    ASSERT(Nbls.NblCount > 0);

    TxQueue->OutstandingCount += (ULONG)Nbls.NblCount;

    EventWriteGenericTxPostBatchStart(
        &MICROSOFT_XDP_PROVIDER, TxQueue, TxQueue->Stats.BatchesPosted);

    if (!TxQueue->Flags.RxInject) {
        NdisFSendNetBufferLists(
            TxQueue->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
            NDIS_DEFAULT_PORT_NUMBER, NDIS_SEND_FLAGS_DISPATCH_LEVEL);
    } else {
        NdisFIndicateReceiveNetBufferLists(
            TxQueue->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
            NDIS_DEFAULT_PORT_NUMBER, (ULONG)Nbls.NblCount, NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    }

    EventWriteGenericTxPostBatchStop(
        &MICROSOFT_XDP_PROVIDER, TxQueue, TxQueue->Stats.BatchesPosted);

    TxQueue->Stats.BatchesPosted++;

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
    UINT32 Drops;
    CONST UINT32 MaxDrops = 1024;

    if (ReadPointerAcquire(&TxQueue->XdpTxQueue) == NULL) {
        return FALSE;
    }

    FrameRing = TxQueue->FrameRing;
    CompletionRing = TxQueue->CompletionRing;

    if (XdpRingCount(FrameRing) == 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }

    for (Drops = 0; XdpRingCount(FrameRing) > 0 && Drops < MaxDrops; Drops++) {
        XDP_FRAME *Frame;
        XDP_BUFFER *Buffer;
        XDP_BUFFER_MDL *BufferMdl;
        XDP_TX_FRAME_COMPLETION *Completion;

        Frame = XdpRingGetElement(FrameRing, FrameRing->ConsumerIndex++ & FrameRing->Mask);
        Buffer = &Frame->Buffer;
        BufferMdl = XdpGetMdlExtension(Buffer, &TxQueue->BufferMdlExtension);

        ASSERT(XdpRingFree(CompletionRing) > 0);
        Completion =
            XdpRingGetElement(
                CompletionRing, CompletionRing->ProducerIndex++ & CompletionRing->Mask);
        Completion->BufferAddress = BufferMdl->MdlOffset;

        if (TxQueue->Flags.TxCompletionContextEnabled) {
            XDP_TX_FRAME_COMPLETION_CONTEXT *FrameCompletionContext =
                XdpGetFrameTxCompletionContextExtension(
                    Frame, &TxQueue->FrameTxCompletionContextExtension);
            XDP_TX_FRAME_COMPLETION_CONTEXT *CompletionContext =
                XdpGetTxCompletionContextExtension(
                    Completion, &TxQueue->TxCompletionContextExtension);
            *CompletionContext = *FrameCompletionContext;
        }

        if (XdpRingFree(CompletionRing) == 0) {
            XdpFlushTransmit(TxQueue->XdpTxQueue);
        }
    }

    if (XdpRingCount(CompletionRing) > 0) {
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }

    STAT_ADD(&TxQueue->PcwStats, FramesDroppedPause, Drops);

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

    if (TxQueue->NeedFlush) {
        TxQueue->NeedFlush = FALSE;
        XdpFlushTransmit(TxQueue->XdpTxQueue);
    }

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
    if (Flags & (XDP_NOTIFY_QUEUE_FLAG_TX | XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH)) {
        if (Flags & XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH) {
            TxQueue->NeedFlush = TRUE;
        }

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

_IRQL_requires_(PASSIVE_LEVEL)
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
    DECLARE_UNICODE_STRING_SIZE(
        Name, ARRAYSIZE("if_" MAXUINT32_STR "_queue_" MAXUINT32_STR "_rx"));
    const WCHAR *DirectionString;

    RtlAcquirePushLockExclusive(&Generic->Lock);

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

    if (HookId.Direction == XDP_HOOK_RX) {
        DirectionString = L"_rx";
    } else {
        ASSERT(HookId.Direction == XDP_HOOK_TX);
        DirectionString = L"";
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

    Status =
        RtlUnicodeStringPrintf(
            &Name, L"if_%u_queue_%u%s", Generic->IfIndex, QueueInfo->QueueId, DirectionString);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPcwCreateLwfTxQueue(&TxQueue->PcwInstance, &Name, &TxQueue->PcwStats);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

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
            XDP_LWF_PARAMETERS_KEY, L"GenericTxFrameCount", &TxQueue->FrameCount);
    if (!NT_SUCCESS(Status)) {
        TxQueue->FrameCount = DEFAULT_TX_FRAME_COUNT;
        Status = STATUS_SUCCESS;
    } else if (TxQueue->FrameCount == 0 || TxQueue->FrameCount > MAX_TX_FRAME_COUNT) {
        TraceWarn(
            TRACE_GENERIC, "IfIndex=%u QueueId=%u Invalid TX frame count. Using default instead.",
            Generic->IfIndex, QueueInfo->QueueId);
        TxQueue->FrameCount = DEFAULT_TX_FRAME_COUNT;
    }

    for (ULONG Index = 0; Index < TxQueue->FrameCount; Index++) {
        NET_BUFFER_LIST *Nbl;
        NET_BUFFER *Nb;
        MDL *Mdl;

        Nbl = NdisAllocateNetBufferList(TxQueue->NblPool, PoolParams.ContextSize, 0);
        if (Nbl == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
        Nbl->SourceHandle = Generic->NdisFilterHandle;
        Mdl = (MDL *)(NET_BUFFER_LIST_CONTEXT_DATA_START(Nbl) + sizeof(NBL_TX_CONTEXT));
        MmInitializeMdl(Mdl, (VOID *)(PAGE_SIZE - 1), MAX_TX_BUFFER_LENGTH);
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
    NeedRestart = XdpGenericReferenceDatapath(Generic, Datapath);
    if (NeedRestart) {
        TxQueue->Flags.Pause = TRUE;
    } else {
        TxQueue->Flags.Pause = Generic->Flags.Paused;
    }

    InsertTailList(&Generic->Tx.Queues, &TxQueue->Link);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
        XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpTxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
        XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpTxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
        XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
        XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION);
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

    RtlReleasePushLockExclusive(&Generic->Lock);

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
            if (TxQueue->PcwInstance != NULL) {
                PcwCloseInstance(TxQueue->PcwInstance);
            }
            ExFreePoolWithTag(TxQueue, POOLTAG_SEND);
        }
    }

    TraceExitStatus(TRACE_GENERIC);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericTxActivateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue = (XDP_LWF_GENERIC_TX_QUEUE *)InterfaceTxQueue;
    XDP_LWF_GENERIC *Generic = TxQueue->Generic;
    XDP_EXTENSION_INFO ExtensionInfo;

    RtlAcquirePushLockExclusive(&Generic->Lock);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
        XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpTxQueueGetExtension(Config, &ExtensionInfo, &TxQueue->BufferMdlExtension);

    ASSERT(XdpTxQueueIsOutOfOrderCompletionEnabled(Config));
    ASSERT(!XdpTxQueueIsFragmentationEnabled(Config));

    TxQueue->FrameRing = XdpTxQueueGetFrameRing(Config);
    TxQueue->CompletionRing = XdpTxQueueGetCompletionRing(Config);

    TxQueue->Flags.TxCompletionContextEnabled = XdpTxQueueIsTxCompletionContextEnabled(Config);

    if (TxQueue->Flags.TxCompletionContextEnabled) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
            XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &TxQueue->FrameTxCompletionContextExtension);

        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
            XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
            XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &TxQueue->TxCompletionContextExtension);
    }

    WritePointerRelease(&TxQueue->XdpTxQueue, XdpTxQueue);

    RtlReleasePushLockExclusive(&Generic->Lock);

    return STATUS_SUCCESS;
}

VOID
XdpGenericFreeTxQueue(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue =
        CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_TX_QUEUE, DeleteEntry);

    XdpEcCleanup(&TxQueue->Ec);
    XdpPcwCloseLwfTxQueue(TxQueue->PcwInstance);
    KeSetEvent(TxQueue->DeleteComplete, 0, FALSE);
    ExFreePoolWithTag(TxQueue, POOLTAG_SEND);
}

_IRQL_requires_(PASSIVE_LEVEL)
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

    RtlAcquirePushLockExclusive(&Generic->Lock);

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
    NeedRestart = XdpGenericDereferenceDatapath(Generic, Datapath);

    RtlReleasePushLockExclusive(&Generic->Lock);

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
