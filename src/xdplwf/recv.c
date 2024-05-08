//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "recv.tmh"

#define RECV_MAX_FRAGMENTS 64
#define RECV_TX_INSPECT_BATCH_SIZE 64
#define RECV_DEFAULT_MAX_TX_BUFFERS 256
#define RECV_MAX_MAX_TX_BUFFERS 4096
//
// Rather than tracking the current lookaside via OIDs, which is subject to
// theoretical race conditions, simply set the minimum lookahead for forwarding
// TX inspected packets onto the RX path based on the known minimums for TCPIP
// and TCPIP6. Older OS builds also erroneously configure excessively large
// lookasides. The value here is the maximum IPv4 header size, which is larger
// than the IPv6 header, plus the L2 header.
//
#define RECV_TX_INSPECT_LOOKAHEAD (sizeof(ETHERNET_HEADER) + (0xF * sizeof(UINT32)))

typedef struct _XDP_LWF_GENERIC_RX_FRAME_CONTEXT {
    NET_BUFFER *Nb;
} XDP_LWF_GENERIC_RX_FRAME_CONTEXT;

typedef struct _NBL_RX_TX_CONTEXT {
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue;
    XDP_LWF_GENERIC_INJECTION_TYPE InjectionType;
} NBL_RX_TX_CONTEXT;

C_ASSERT(
    FIELD_OFFSET(NBL_RX_TX_CONTEXT, RxQueue) ==
    FIELD_OFFSET(XDP_LWF_GENERIC_INJECTION_CONTEXT, InjectionCompletionContext));
C_ASSERT(
    FIELD_OFFSET(NBL_RX_TX_CONTEXT, InjectionType) ==
    FIELD_OFFSET(XDP_LWF_GENERIC_INJECTION_CONTEXT, InjectionType));

#define RX_TX_CONTEXT_SIZE sizeof(NBL_RX_TX_CONTEXT)
C_ASSERT(RX_TX_CONTEXT_SIZE % MEMORY_ALLOCATION_ALIGNMENT == 0);

static UINT32 RxMaxTxBuffers = RECV_DEFAULT_MAX_TX_BUFFERS;

static
NBL_RX_TX_CONTEXT *
NblRxTxContext(
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    //
    // Review: we could use the protocol or miniport reserved space for
    // regular TX and RX-inject respectively.
    //
    return (NBL_RX_TX_CONTEXT *)NET_BUFFER_LIST_CONTEXT_DATA_START(NetBufferList);
}

static
UINT32
XdpGenericRecvInjectReturnNbls(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ NBL_COUNTED_QUEUE *Queue
    )
{
    UINT32 NblCount = (UINT32)Queue->NblCount;
    NBL_QUEUE ReturnList;

    NdisInitializeNblQueue(&ReturnList);

    ASSERT(!NdisIsNblCountedQueueEmpty(Queue));

    for (NET_BUFFER_LIST *Nbl = Queue->Queue.First; Nbl != NULL; Nbl = Nbl->Next) {
        NET_BUFFER_LIST *ParentNbl = Nbl->ParentNetBufferList;

        ASSERT(NblRxTxContext(Nbl)->InjectionType == XDP_LWF_GENERIC_INJECTION_RECV);
        ASSERT(NblRxTxContext(Nbl)->RxQueue == RxQueue);

        if (ParentNbl != NULL) {
            if (InterlockedDecrement((LONG *)&Nbl->ParentNetBufferList->ChildRefCount) == 0) {
                NdisAppendSingleNblToNblQueue(&ReturnList, Nbl->ParentNetBufferList);
            }
        } else {
            NdisAdvanceNetBufferListDataStart(Nbl, Nbl->FirstNetBuffer->DataLength, TRUE, NULL);
        }
    }

    if (!NdisIsNblQueueEmpty(&ReturnList)) {
        if (RxQueue->Flags.TxInspect) {
            NdisFSendNetBufferListsComplete(
                RxQueue->Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&ReturnList), 0);
        } else {
            NdisFReturnNetBufferLists(
                RxQueue->Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&ReturnList), 0);
        }
    }

    NDIS_ASSERT_VALID_NBL_COUNTED_QUEUE(Queue);
    ASSERT(Queue->Queue.First != NULL);

    InterlockedPushListSList(
        &RxQueue->TxCloneNblSList,
        (SLIST_ENTRY *)&Queue->Queue.First->Next,
        (SLIST_ENTRY *)Queue->Queue.Last,
        (ULONG)Queue->NblCount);

    NdisInitializeNblCountedQueue(Queue);

    return NblCount;
}

VOID
XdpGenericRecvInjectComplete(
    _In_ VOID *ClassificationResult,
    _In_ NBL_COUNTED_QUEUE *Queue
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = ClassificationResult;
    UINT32 NblCount;

    NblCount = XdpGenericRecvInjectReturnNbls(RxQueue, Queue);

    ExReleaseRundownProtectionEx(&RxQueue->NblRundown, NblCount);
}

_Use_decl_annotations_
VOID
XdpGenericReturnNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    NET_BUFFER_LIST *NetBufferLists,
    ULONG ReturnFlags
    )
{
    XDP_LWF_GENERIC *Generic = XdpGenericFromFilterContext(FilterModuleContext);
    KIRQL OldIrql = DISPATCH_LEVEL;

    if (!NDIS_TEST_RETURN_AT_DISPATCH_LEVEL(ReturnFlags)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    NetBufferLists = XdpGenericInjectNetBufferListsComplete(Generic, NetBufferLists);

    if (OldIrql != DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }

    if (NetBufferLists != NULL) {
        NdisFReturnNetBufferLists(Generic->NdisFilterHandle, NetBufferLists, ReturnFlags);
    }
}

static
VOID
XdpGenericFlushReceive(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    XdpFlushReceive(XdpRxQueue);
    RxQueue->FragmentBufferInUse = FALSE;
}

static
VOID
XdpGenericReceiveNotify(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = (XDP_LWF_GENERIC_RX_QUEUE *)InterfaceQueue;
    KIRQL OldIrql;

    if (Flags & XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH) {
        BOOLEAN NeedEcNotify = FALSE;

        KeAcquireSpinLock(&RxQueue->EcLock, &OldIrql);

        //
        // If the TX inspect worker is active, pend the flush, otherwise simply
        // invoke flush inline while holding the EC lock.
        //
        if (RxQueue->Flags.TxInspectInline || RxQueue->Flags.TxInspectWorker) {
            RxQueue->Flags.TxInspectNeedFlush = TRUE;
            NeedEcNotify = TRUE;
        } else {
            XdpGenericFlushReceive(RxQueue, RxQueue->XdpRxQueue);
        }

        KeReleaseSpinLock(&RxQueue->EcLock, OldIrql);

        if (NeedEcNotify) {
            XdpEcNotify(&RxQueue->TxInspectEc);
        }
    }
}

static CONST XDP_INTERFACE_RX_QUEUE_DISPATCH RxDispatch = {
    .InterfaceNotifyQueue = XdpGenericReceiveNotify,
};

static
VOID
XdpGenericReceiveLowResources(
    _In_ NDIS_HANDLE NdisHandle,
    _Inout_ KSPIN_LOCK *EcLock,
    _Inout_ NBL_COUNTED_QUEUE *PassList,
    _Inout_ NBL_QUEUE *ReturnList,
    _Inout_ NBL_QUEUE *LowResourcesList,
    _In_ ULONG PortNumber,
    _In_ BOOLEAN Flush
    )
{
    //
    // NDIS6 requires the data path support "low resources" receive indications
    // where the original, unmodified NBL chain is returned inline. Therefore
    // chain splitting must be reverted and then processed in contiguous batches
    // of NBLs instead.
    //
    // The caller must invoke this function for every NBL on its original list.
    // The pass list is allowed to grow but the return list is immediately
    // flushed to the output (identical to the original) low resources list.
    //
    ASSERT(
        ReturnList->First == NULL ||
        ReturnList->First == CONTAINING_RECORD(ReturnList->Last, NET_BUFFER_LIST, Next));

    if (!NdisIsNblCountedQueueEmpty(PassList) && (Flush || !NdisIsNblQueueEmpty(ReturnList))) {
        //
        // We just dropped one NBL after a chain of passed NBLs. Indicate the
        // pass list up the stack, then continue reassembling the original
        // LowResourcesList chain. Release the EC spinlock prior to calling into
        // NDIS; the receive data path is otherwise reentrant.
        //

        KeReleaseSpinLockFromDpcLevel(EcLock);
        NdisFIndicateReceiveNetBufferLists(
            NdisHandle, NdisGetNblChainFromNblCountedQueue(PassList), PortNumber,
            (ULONG)PassList->NblCount,
            NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL | NDIS_RECEIVE_FLAGS_RESOURCES);
        KeAcquireSpinLockAtDpcLevel(EcLock);

        NdisAppendNblChainToNblQueueFast(
            LowResourcesList, PassList->Queue.First,
            CONTAINING_RECORD(PassList->Queue.Last, NET_BUFFER_LIST, Next));
        NdisInitializeNblCountedQueue(PassList);
    }

    NdisAppendNblQueueToNblQueueFast(LowResourcesList, ReturnList);
}

_IRQL_requires_(DISPATCH_LEVEL)
static
VOID
XdpGenericReceiveEnterEc(
    _In_ XDP_LWF_GENERIC *Generic,
    _Inout_ NET_BUFFER_LIST **NetBufferLists,
    _In_ ULONG CurrentProcessor,
    _In_ BOOLEAN TxInspect,
    _In_ BOOLEAN TxWorker,
    _Out_ XDP_LWF_GENERIC_RSS_QUEUE **RssQueue,
    _Out_ XDP_LWF_GENERIC_RX_QUEUE **RxQueue,
    _Out_ XDP_RX_QUEUE_HANDLE *XdpRxQueue,
    _Inout_ NBL_COUNTED_QUEUE *PassList
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *CandidateRxQueue;
    ULONG RssHash = NET_BUFFER_LIST_GET_HASH_VALUE(*NetBufferLists);

    *RssQueue = NULL;
    *RxQueue = NULL;
    *XdpRxQueue = NULL;

    //
    // Find the target RSS queue based on the first NBL's RSS hash.
    //
    *RssQueue = XdpGenericRssGetQueue(Generic, CurrentProcessor, TxInspect, RssHash);
    if (*RssQueue == NULL) {
        //
        // RSS is uninitialized, so pass the NBLs through. Note that the XDP
        // interface cannot be opened, and therefore queues cannot be created,
        // until RSS is successfully initialized. Therefore this code path is
        // unreachable if any XDP queue exists on this interface.
        //
        NdisAppendNblChainToNblCountedQueue(PassList, *NetBufferLists);
        *NetBufferLists = NULL;
        return;
    }

    *RxQueue = NULL;

    //
    // Find the generic RX queue attached to the RSS queue.
    //
    CandidateRxQueue =
        ReadPointerAcquire(TxInspect ? &(*RssQueue)->TxInspectQueue : &(*RssQueue)->RxQueue);
    if (CandidateRxQueue == NULL) {
        //
        // An RX queue is not attached, so pass the NBLs through.
        //
        NdisAppendNblChainToNblCountedQueue(PassList, *NetBufferLists);
        *NetBufferLists = NULL;
        return;
    }

    *RxQueue = CandidateRxQueue;

    *XdpRxQueue = ReadPointerAcquire(&(*RxQueue)->XdpRxQueue);
    if (*XdpRxQueue == NULL) {
        //
        // The XDP queue is not activated on this RX queue, so pass the
        // NBLs through.
        //
        NdisAppendNblChainToNblCountedQueue(PassList, *NetBufferLists);
        *NetBufferLists = NULL;
    } else {
        KeAcquireSpinLockAtDpcLevel(&(*RxQueue)->EcLock);

        //
        // If this is TX inspection, instead of processing the NBLs while
        // holding the EcLock, check if this is the only indication in flight.
        // If it is, process the NBL chain in inline mode, otherwise queue the
        // NBLs to the worker.
        //
        if (TxInspect) {
            if (TxWorker) {
                ASSERT((*RxQueue)->Flags.TxInspectWorker);
            } else if ((*RxQueue)->Flags.TxInspectInline || (*RxQueue)->Flags.TxInspectWorker) {
                NdisAppendNblChainToNblQueue(&(*RxQueue)->TxInspectNblQueue, *NetBufferLists);
                *NetBufferLists = NULL;
                *XdpRxQueue = NULL;
            } else {
                (*RxQueue)->Flags.TxInspectInline = TRUE;
            }
            KeReleaseSpinLockFromDpcLevel(&(*RxQueue)->EcLock);
        }
    }
}

static
VOID
XdpGenericReceiveExitEc(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ BOOLEAN TxWorker,
    _Inout_ NBL_COUNTED_QUEUE *PassList
    )
{
    BOOLEAN NeedEcNotify = FALSE;
    XDP_RX_QUEUE_HANDLE XdpRxQueue;

    //
    // Exit the inspection execution context.
    //
    if (!TxWorker) {
        if (RxQueue->Flags.TxInspectInline) {
            //
            // Before releasing the inline context, send any NBLs on the pass
            // list, otherwise reordering may occur between the inline and
            // worker threads.
            //
            if (!NdisIsNblCountedQueueEmpty(PassList)) {
                NdisFSendNetBufferLists(
                    RxQueue->Generic->NdisFilterHandle,
                    NdisGetNblChainFromNblCountedQueue(PassList),
                    NDIS_DEFAULT_PORT_NUMBER, NDIS_SEND_FLAGS_DISPATCH_LEVEL);
                NdisInitializeNblCountedQueue(PassList);
            }

            //
            // Re-acquire the EcLock and check for any pending work.
            //
            KeAcquireSpinLockAtDpcLevel(&RxQueue->EcLock);

            RxQueue->Flags.TxInspectInline = FALSE;

            if (RxQueue->Flags.TxInspectNeedFlush) {
                RxQueue->Flags.TxInspectNeedFlush = FALSE;

                XdpRxQueue = ReadPointerNoFence(&RxQueue->XdpRxQueue);
                if (XdpRxQueue != NULL) {
                    XdpGenericFlushReceive(RxQueue, XdpRxQueue);
                }
            }

            //
            // More NBLs arrived during inspection; start the TX inspection
            // worker.
            //
            if (!NdisIsNblQueueEmpty(&RxQueue->TxInspectNblQueue)) {
                RxQueue->Flags.TxInspectWorker = TRUE;
                NeedEcNotify = TRUE;
            }
        }

        KeReleaseSpinLockFromDpcLevel(&RxQueue->EcLock);

        if (NeedEcNotify) {
            XdpEcNotify(&RxQueue->TxInspectEc);
        }
    }
}

static
BOOLEAN
XdpGenericReceiveExpandFragmentBuffer(
    _Inout_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue
    )
{
    UINT32 NewBufferSize;
    UCHAR *NewBuffer;

    //
    // Double the size of the fragment buffer, using 64KB as the base value.
    // The size is all leading zero bits, followed by all ones, which limits
    // the size to MAXUINT32.
    //
    FRE_ASSERT(RxQueue->FragmentBufferSize < MAXUINT32);
    NewBufferSize = (max(RxQueue->FragmentBufferSize, 0xffff) << 1) | 1;

    NewBuffer =
        ExAllocatePoolPriorityZero(NonPagedPoolNx, NewBufferSize, POOLTAG_RECV, LowPoolPriority);
    if (NewBuffer == NULL) {
        return FALSE;
    }

    if (RxQueue->FragmentBuffer != NULL) {
        RtlCopyMemory(NewBuffer, RxQueue->FragmentBuffer, RxQueue->FragmentBufferSize);
        ExFreePoolWithTag(RxQueue->FragmentBuffer, POOLTAG_RECV);
    }

    RxQueue->FragmentBuffer = NewBuffer;
    RxQueue->FragmentBufferSize = NewBufferSize;
    return TRUE;
}

static
BOOLEAN
XdpGenericReceiveLinearizeNb(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ NET_BUFFER *Nb
    )
{
    XDP_RING *FrameRing = RxQueue->FrameRing;
    XDP_FRAME *Frame;
    XDP_BUFFER *Buffer;
    MDL *Mdl = NET_BUFFER_CURRENT_MDL(Nb);
    UINT32 MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(Nb);
    UINT32 DataLength = NET_BUFFER_DATA_LENGTH(Nb);
    XDP_BUFFER_VIRTUAL_ADDRESS *SystemVa;

    ASSERT(RxQueue->FragmentBufferInUse == FALSE);

    ASSERT(XdpRingFree(FrameRing) > 0);
    Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex & FrameRing->Mask);

    Buffer = &Frame->Buffer;
    Buffer->DataLength = 0;

    //
    // Walk the MDL chain, copying data and expanding the backing buffer as
    // necessary.
    //
    while (Mdl != NULL && DataLength > 0) {
        UCHAR *MdlBuffer;
        UINT32 CopyLength = min(Mdl->ByteCount - MdlOffset, DataLength);

        if (!NT_SUCCESS(RtlUInt32Add(CopyLength, Buffer->DataLength, &Buffer->DataLength))) {
            return FALSE;
        }

        MdlBuffer = MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority | MdlMappingNoExecute);
        if (MdlBuffer == NULL || XdpLwfFaultInject()) {
            return FALSE;
        }

        //
        // If the existing contiguous buffer is too small, attempt to expand it,
        // which also copies any existing payload.
        //
        if (Buffer->DataLength > RxQueue->FragmentBufferSize) {
            if (!XdpGenericReceiveExpandFragmentBuffer(RxQueue)) {
                return FALSE;
            }

            ASSERT(Buffer->DataLength <= RxQueue->FragmentBufferSize);
        }

        RtlCopyMemory(
            RxQueue->FragmentBuffer + Buffer->DataLength - CopyLength,
            MdlBuffer + MdlOffset, CopyLength);

        DataLength -= CopyLength;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    Buffer->DataOffset = 0;
    Buffer->BufferLength = Buffer->DataLength;
    SystemVa = XdpGetVirtualAddressExtension(Buffer, &RxQueue->BufferVaExtension);
    SystemVa->VirtualAddress = RxQueue->FragmentBuffer;

    RxQueue->FragmentBufferInUse = TRUE;
    return TRUE;
}

static
VOID
XdpGenericRxFreeNblCloneCache(
    _In_opt_ NET_BUFFER_LIST *NblChain
    )
{
    while (NblChain != NULL) {
        NET_BUFFER_LIST *Nbl = NblChain;
        NblChain = NblChain->Next;

        ASSERT(Nbl->FirstNetBuffer->Next == NULL);
        Nbl->FirstNetBuffer->MdlChain = NULL;
        Nbl->FirstNetBuffer->CurrentMdl = NULL;
        Nbl->FirstNetBuffer->DataLength = 0;
        Nbl->FirstNetBuffer->DataOffset = 0;
        Nbl->FirstNetBuffer->CurrentMdlOffset = 0;

        NdisFreeNetBufferList(Nbl);
    }
}

static
VOID
XdpGenericReceiveEnqueueTxNb(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _In_ NET_BUFFER_LIST *Nbl,
    _In_ NET_BUFFER *Nb,
    _In_ BOOLEAN CanPend
    )
{
    NET_BUFFER *OriginalFirstNb = Nbl->FirstNetBuffer;
    NET_BUFFER *OriginalNextNb = Nb->Next;
    NET_BUFFER_LIST *TxNbl;

    Nbl->FirstNetBuffer = Nb;
    Nbl->FirstNetBuffer->Next = NULL;

    //
    // TODO: Convert OOBs between recv/send semantics.
    // TODO: Perform any software offloads required.
    //

    if (RxQueue->TxCloneNblList == NULL) {
        RxQueue->TxCloneNblList =
            (NET_BUFFER_LIST *)InterlockedFlushSList(&RxQueue->TxCloneNblSList);
    }

    if (RxQueue->TxCloneNblList != NULL) {
        TxNbl = RxQueue->TxCloneNblList;
        RxQueue->TxCloneNblList = TxNbl->Next;
    } else if (RxQueue->TxCloneCacheCount < RxQueue->TxCloneCacheLimit) {
        TxNbl =
            NdisAllocateNetBufferAndNetBufferList(
                RxQueue->TxCloneNblPool, RX_TX_CONTEXT_SIZE, 0, NULL, 0, 0);
        if (TxNbl == NULL) {
            STAT_INC(&RxQueue->PcwStats, ForwardingFailures);
            goto Exit;
        }

        RxQueue->TxCloneCacheCount++;
    } else {
        STAT_INC(&RxQueue->PcwStats, ForwardingFailures);
        goto Exit;
    }

    ASSERT(TxNbl->FirstNetBuffer->Next == NULL);

    //
    // If we are allowed to clone and pend NBLs, reuse the existing MDL chain.
    //
    // We cannot reuse the MDL chain if the NBL is being forwarded onto the
    // local RX path and the frame is potentially discontiguous within L2 or L3
    // headers.
    //
    if (CanPend &&
            (!RxQueue->Flags.TxInspect ||
                Nb->CurrentMdl->ByteCount - Nb->CurrentMdlOffset >= RECV_TX_INSPECT_LOOKAHEAD)) {
        TxNbl->FirstNetBuffer->MdlChain = Nb->MdlChain;
        TxNbl->FirstNetBuffer->CurrentMdl = Nb->CurrentMdl;
        TxNbl->FirstNetBuffer->DataLength = Nb->DataLength;
        TxNbl->FirstNetBuffer->DataOffset = Nb->DataOffset;
        TxNbl->FirstNetBuffer->CurrentMdlOffset = Nb->CurrentMdlOffset;
        TxNbl->ParentNetBufferList = Nbl;
        Nbl->ChildRefCount++;
    } else {
        NDIS_STATUS NdisStatus;
        ULONG BytesCopied;

        NdisStatus =
            NdisRetreatNetBufferListDataStart(TxNbl, Nb->DataLength, Nb->DataOffset, NULL, NULL);
        if (NdisStatus != NDIS_STATUS_SUCCESS) {
            TxNbl->Next = RxQueue->TxCloneNblList;
            RxQueue->TxCloneNblList = TxNbl;
            goto Exit;
        }

        NdisStatus =
            NdisCopyFromNetBufferToNetBuffer(
                TxNbl->FirstNetBuffer, 0, Nb->DataLength, Nb, 0, &BytesCopied);
        ASSERT(NdisStatus == NDIS_STATUS_SUCCESS);
        ASSERT(BytesCopied == Nb->DataLength);

        TxNbl->ParentNetBufferList = NULL;
    }

    ASSERT(TxNbl != NULL);
    NblRxTxContext(TxNbl)->RxQueue = RxQueue;
    NblRxTxContext(TxNbl)->InjectionType = XDP_LWF_GENERIC_INJECTION_RECV;
    TxNbl->SourceHandle = RxQueue->Generic->NdisFilterHandle;
    NET_BUFFER_LIST_SET_HASH_VALUE(TxNbl, NET_BUFFER_LIST_GET_HASH_VALUE(Nbl));
    NdisAppendSingleNblToNblCountedQueue(TxList, TxNbl);

Exit:

    Nbl->FirstNetBuffer = OriginalFirstNb;
    Nb->Next = OriginalNextNb;
}

static
VOID
XdpGenericReceiveEnqueueTxNbl(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _Inout_ NBL_QUEUE *DropList,
    _In_ NET_BUFFER_LIST *Nbl,
    _In_ BOOLEAN CanPend
    )
{
    Nbl->ChildRefCount = 0;

    for (NET_BUFFER *Nb = Nbl->FirstNetBuffer; Nb != NULL; Nb = Nb->Next) {
        XdpGenericReceiveEnqueueTxNb(RxQueue, TxList, Nbl, Nb, CanPend);
    }

    ASSERT(CanPend || Nbl->ChildRefCount == 0);

    if (Nbl->ChildRefCount == 0) {
        NdisAppendSingleNblToNblQueue(DropList, Nbl);
    }
}

static
VOID
XdpGenericReceivePreInspectNbs(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ BOOLEAN CanPend,
    _Inout_ NET_BUFFER_LIST **Nbl,
    _Inout_ NET_BUFFER **Nb
    )
{
    XDP_RING *FrameRing = RxQueue->FrameRing;
    XDP_RING *FragmentRing = RxQueue->FragmentRing;
    XDP_FRAME *Frame;
    XDP_FRAME_FRAGMENT *FragmentExtension;
    UINT8 FragmentCount;
    XDP_BUFFER *Buffer;
    MDL *Mdl;
    XDP_BUFFER_VIRTUAL_ADDRESS *SystemVa;
    UINT32 DataLength;
    XDP_LWF_GENERIC_RX_FRAME_CONTEXT *InterfaceExtension;
    UINT32 FrameRingReservedCount;

    //
    // For low resources indications, ensure only one frame is enqueued at a time.
    // This is required since we release the EC lock to indicate low resource
    // frames up the stack, so we must leave the RX queue in a state that allows
    // another thread (or the same thread) to inspect while the low resources
    // indication is in progress.
    //
    FrameRingReservedCount = CanPend ? 0 : FrameRing->Mask;

    do {
        ASSERT(XdpRingFree(FrameRing) > FrameRingReservedCount);
        Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex & FrameRing->Mask);

        Buffer = &Frame->Buffer;
        DataLength = NET_BUFFER_DATA_LENGTH(*Nb);
        Mdl = NET_BUFFER_CURRENT_MDL(*Nb);
        Buffer->DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(*Nb);
        Buffer->DataLength = min(Mdl->ByteCount - Buffer->DataOffset, DataLength);
        Buffer->BufferLength = Mdl->ByteCount;
        DataLength -= Buffer->DataLength;
        FragmentCount = 0;

        //
        // NDIS components may request that packets sent locally be looped back
        // on the receive path. Skip inspection of these packets.
        //
        if (NdisTestNblFlag(*Nbl, NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET)) {
            goto Next;
        }

        SystemVa = XdpGetVirtualAddressExtension(Buffer, &RxQueue->BufferVaExtension);
        SystemVa->VirtualAddress =
            MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority | MdlMappingNoExecute);
        if (SystemVa->VirtualAddress == NULL || XdpLwfFaultInject()) {
            STAT_INC(&RxQueue->PcwStats, MappingFailures);
            goto Next;
        }

        //
        // Loop over fragment MDLs. NDIS allows excess MDLs past the data length;
        // ignore those, but allow trailing bytes in the final buffer.
        //
        for (Mdl = Mdl->Next; Mdl != NULL && DataLength > 0; Mdl = Mdl->Next) {
            //
            // Check if the number of MDLs exceeds the maximum XDP fragments. If so,
            // attempt to convert the MDL chain to a single flat buffer.
            //
            if (FragmentCount + 1ui32 > RxQueue->FragmentLimit) {
                //
                // Generic XDP reserves a single contiguous buffer for
                // linearization; if a NB contains more than the maximum number
                // of fragments (MDLs), copy the contents of the entire MDL
                // chain into one contiguous buffer.
                //
                // If the contiguous buffer is already in use, flush the queue
                // first.
                //
                if (RxQueue->FragmentBufferInUse) {
                    return;
                }

                if (!XdpGenericReceiveLinearizeNb(RxQueue, *Nb)) {
                    STAT_INC(&RxQueue->PcwStats, LinearizationFailures);
                    goto Next;
                }

                //
                // The NB was converted to a single, virtually-contiguous buffer,
                // so abandon fragmentation and proceed to XDP inspection.
                //
                FragmentCount = 0;
                break;
            }

            //
            // Reserve XDP descriptor space for this MDL. If space does not exist,
            // flush existing descriptors to create space.
            //
            if (++FragmentCount > XdpRingFree(FragmentRing)) {
                return;
            }

            Buffer =
                XdpRingGetElement(
                    FragmentRing,
                    (FragmentRing->ProducerIndex + FragmentCount - 1) & FragmentRing->Mask);

            Buffer->DataOffset = 0;
            Buffer->DataLength = min(Mdl->ByteCount, DataLength);
            Buffer->BufferLength = Mdl->ByteCount;
            DataLength -= Buffer->DataLength;

            SystemVa = XdpGetVirtualAddressExtension(Buffer, &RxQueue->BufferVaExtension);
            SystemVa->VirtualAddress =
                MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority | MdlMappingNoExecute);
            if (SystemVa->VirtualAddress == NULL || XdpLwfFaultInject()) {
                STAT_INC(&RxQueue->PcwStats, MappingFailures);
                goto Next;
            }
        }

        FragmentExtension = XdpGetFragmentExtension(Frame, &RxQueue->FragmentExtension);
        FragmentExtension->FragmentBufferCount = FragmentCount;

        //
        // Store the original NB address so uninspected frames (e.g. those where
        // virtual mappings failed) can be identified and dropped later.
        //
        InterfaceExtension =
            XdpGetFrameInterfaceContextExtension(Frame, &RxQueue->FrameInterfaceContextExtension);
        InterfaceExtension->Nb = *Nb;

        //
        // The NB has successfully been converted to XDP descriptors, so commit
        // the descriptors to the XDP rings.
        //
        FragmentRing->ProducerIndex += FragmentCount;
        FrameRing->ProducerIndex++;

Next:

        *Nb = NET_BUFFER_NEXT_NB(*Nb);

        if (*Nb == NULL) {
            *Nbl = NET_BUFFER_LIST_NEXT_NBL(*Nbl);

            if (*Nbl != NULL) {
                *Nb = NET_BUFFER_LIST_FIRST_NB(*Nbl);
            }
        }
    } while (*Nb != NULL && XdpRingFree(FrameRing) > FrameRingReservedCount);
}

static
VOID
XdpGenericReceivePostInspectNbs(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ BOOLEAN CanPend,
    _In_ NET_BUFFER_LIST *NblHead,
    _In_ NET_BUFFER *NbHead,
    _In_opt_ NET_BUFFER *NbTail,
    _Inout_ NBL_COUNTED_QUEUE *PassList,
    _Inout_ NBL_QUEUE *DropList,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _Inout_ NBL_QUEUE *LowResourcesList
    )
{
    XDP_RING *FrameRing = RxQueue->FrameRing;

    //
    // XDP must have inspected and returned all frames in the ring.
    //
    ASSERT(FrameRing->ConsumerIndex == FrameRing->ProducerIndex);

    while (NbHead != NbTail) {
        XDP_FRAME *Frame;
        XDP_RX_ACTION XdpRxAction;
        XDP_LWF_GENERIC_RX_FRAME_CONTEXT *InterfaceExtension;
        NET_BUFFER_LIST *ActionNbl = NULL;

        ASSERT(NblHead != NULL);
        ASSERT(NbHead != NULL);
        Frame = XdpRingGetElement(FrameRing, FrameRing->InterfaceReserved & FrameRing->Mask);
        InterfaceExtension =
            XdpGetFrameInterfaceContextExtension(Frame, &RxQueue->FrameInterfaceContextExtension);

        if (FrameRing->InterfaceReserved != FrameRing->ProducerIndex &&
            NbHead == InterfaceExtension->Nb) {
            XdpRxAction = XdpGetRxActionExtension(Frame, &RxQueue->RxActionExtension)->RxAction;
            FrameRing->InterfaceReserved++;
        } else {
            //
            // This NB's action was decided prior to XDP inspection.
            //
            if (NdisTestNblFlag(NblHead, NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET)) {
                XdpRxAction = XDP_RX_ACTION_PASS;
            } else {
                XdpRxAction = XDP_RX_ACTION_DROP;
            }
        }

        //
        // Currently XDP does not advance/retreat buffers, so there's no need to
        // explicitly update the NB; however, the payload data may have already
        // been rewritten.
        //

        //
        // Handle the NBL based on the action of the first NB in the NBL. NBLs
        // with multiple NBs are only permitted on the NDIS send path, and we
        // expect all NBs within an NBL to *usually* be assigned the same
        // action.
        //
        // TODO: Implement NBL splitting such that NBs assigned different
        // actions within an NBL are correctly handled.
        //
        if (NET_BUFFER_LIST_FIRST_NB(NblHead) == NbHead) {
            ActionNbl = NblHead;
        }

        //
        // Advance to the next NB in the chain, and advance to the next NBL if
        // necessary.
        //
        NbHead = NET_BUFFER_NEXT_NB(NbHead);

        if (NbHead == NULL) {
            NblHead = NET_BUFFER_LIST_NEXT_NBL(NblHead);

            if (NblHead != NULL) {
                NbHead = NET_BUFFER_LIST_FIRST_NB(NblHead);
            }
        }

        //
        // Now that we've finished dereferencing ActionNbl, apply the RX action.
        //
        if (ActionNbl != NULL) {
            switch (XdpRxAction) {

            case XDP_RX_ACTION_PASS:
                NdisAppendSingleNblToNblCountedQueue(PassList, ActionNbl);
                break;

            case XDP_RX_ACTION_TX:
                XdpGenericReceiveEnqueueTxNbl(RxQueue, TxList, DropList, ActionNbl, CanPend);
                break;

            case XDP_RX_ACTION_DROP:
                NdisAppendSingleNblToNblQueue(DropList, ActionNbl);
                break;

            default:
                ASSERT(FALSE);
            }
        }

        if (!CanPend) {
            //
            // Enforce NDIS low resources constraints on pass and return lists.
            // N.B. This releases and reacquires the EC spinlock.
            //
            ASSERT(!RxQueue->Flags.TxInspect);
            ASSERT(FrameRing->InterfaceReserved == FrameRing->ProducerIndex);
            XdpGenericReceiveLowResources(
                RxQueue->Generic->NdisFilterHandle, &RxQueue->EcLock, PassList, DropList,
                LowResourcesList, PortNumber, (NbHead == NULL));
        }
    }

    ASSERT(FrameRing->InterfaceReserved == FrameRing->ProducerIndex);
}

static
VOID
XdpGenericReceiveInspect(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ NET_BUFFER_LIST *NetBufferListChain,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ BOOLEAN CanPend,
    _Inout_ NBL_COUNTED_QUEUE *PassList,
    _Inout_ NBL_QUEUE *DropList,
    _Inout_ NBL_COUNTED_QUEUE *TxList
    )
{
    NBL_QUEUE LowResourcesList;
    NET_BUFFER_LIST *NblHead, *NextNbl;
    NET_BUFFER *NbHead, *NextNb;

    ASSERT(NetBufferListChain != NULL);

    NdisInitializeNblQueue(&LowResourcesList);
    NextNbl = NetBufferListChain;
    NextNb = NET_BUFFER_LIST_FIRST_NB(NextNbl);

    do {
        NblHead = NextNbl;
        NbHead = NextNb;

        //
        // Queue a batch of NBLs into the XDP receive ring for inspection.
        //
        XdpGenericReceivePreInspectNbs(RxQueue, CanPend, &NextNbl, &NextNb);

        //
        // Invoke XDP inspection. Use the dispatch table (indirect call) rather
        // than a direct call since XDP may substitute for an optimized routine.
        //
        XdpReceiveThunk(XdpRxQueue);

        //
        // Apply XDP actions from the XDP receive ring to the NBL chain.
        //
        XdpGenericReceivePostInspectNbs(
            RxQueue, PortNumber, CanPend, NblHead, NbHead, NextNb, PassList, DropList, TxList,
            &LowResourcesList);
    } while (NextNb != NULL);
}

VOID
XdpGenericReceive(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _Out_ NBL_COUNTED_QUEUE *PassList,
    _Out_ NBL_QUEUE *DropList,
    _Out_ NBL_COUNTED_QUEUE *TxList,
    _In_ UINT32 XdpInspectFlags
    )
{
    KIRQL OldIrql = DISPATCH_LEVEL;
    ULONG Processor;
    BOOLEAN CanPend = !(XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_RESOURCES);
    BOOLEAN TxInspect = XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_TX;
    BOOLEAN TxWorker = XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_TX_WORKER;
    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue = NULL;
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = NULL;
    XDP_RX_QUEUE_HANDLE XdpRxQueue = NULL;

    EventWriteGenericRxInspectStart(&MICROSOFT_XDP_PROVIDER, Generic);

    NdisInitializeNblCountedQueue(PassList);
    NdisInitializeNblQueue(DropList);
    NdisInitializeNblCountedQueue(TxList);

    if (!(XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    Processor = KeGetCurrentProcessorIndex();

    //
    // Attempt to enter the RX queue's EC for the implicit RSS queue. Either we
    // successfully enter the EC and are provided with an XDP queue and NBLs to
    // process, XOR we could not enter the EC (no XDP queue and all NBLs were
    // redirected).
    //
    XdpGenericReceiveEnterEc(
        Generic, &NetBufferLists, Processor, TxInspect, TxWorker, &RssQueue, &RxQueue,
        &XdpRxQueue, PassList);
    ASSERT((NetBufferLists != NULL) == (XdpRxQueue != NULL));

    if (NetBufferLists != NULL) {
        //
        // Perform XDP inspection on each frame within the NBL chain.
        //
        XdpGenericReceiveInspect(
            RxQueue, XdpRxQueue, NetBufferLists, PortNumber, CanPend, PassList, DropList, TxList);
    }

    if (XdpRxQueue != NULL) {
        XdpGenericReceiveExitEc(RxQueue, TxWorker, PassList);
    }

    if (RssQueue != NULL && !TxInspect) {
        //
        // Attempt to steal time from the RX path to ensure TX gets a chance to
        // run. If this processor differs from the ideal TX processor, no time
        // will be stolen.
        //
        XdpGenericTxFlushRss(RssQueue, Processor);
    }

    if (!NdisIsNblCountedQueueEmpty(TxList)) {
        if (!ExAcquireRundownProtectionEx(&RxQueue->NblRundown, (ULONG)TxList->NblCount)) {
            XdpGenericRecvInjectReturnNbls(RxQueue, TxList);
            ASSERT(NdisIsNblCountedQueueEmpty(TxList));
        }
    }

    if (OldIrql != DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }

    EventWriteGenericRxInspectStop(&MICROSOFT_XDP_PROVIDER, Generic);
}

_Use_decl_annotations_
VOID
XdpGenericReceiveNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    NET_BUFFER_LIST *NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG NumberOfNetBufferLists,
    ULONG ReceiveFlags
    )
{
    XDP_LWF_GENERIC *Generic = XdpGenericFromFilterContext(FilterModuleContext);
    BOOLEAN AtDispatch = NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags);
    NBL_COUNTED_QUEUE PassList;
    NBL_QUEUE DropList;
    NBL_COUNTED_QUEUE TxList;

    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    XdpGenericReceive(
        Generic, NetBufferLists, PortNumber, &PassList, &DropList, &TxList,
        ReceiveFlags & XDP_LWF_GENERIC_INSPECT_NDIS_RX_MASK);

    if (!NdisIsNblCountedQueueEmpty(&PassList)) {
        XdpLwfOffloadTransformNbls(
            Generic->Filter, &PassList, ReceiveFlags & XDP_LWF_GENERIC_INSPECT_NDIS_RX_MASK);
        NdisFIndicateReceiveNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&PassList), PortNumber,
            (ULONG)PassList.NblCount, ReceiveFlags);
    }

    if (!NdisIsNblQueueEmpty(&DropList)) {
        NdisFReturnNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&DropList),
            AtDispatch ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }

    if (!NdisIsNblCountedQueueEmpty(&TxList)) {
        NdisFSendNetBufferLists(
            Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&TxList), PortNumber,
            AtDispatch ? NDIS_SEND_FLAGS_DISPATCH_LEVEL : 0);
    }
}

static
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpGenericReceiveTxInspectPoll(
    _In_ VOID *Context
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = Context;
    XDP_LWF_GENERIC *Generic = RxQueue->Generic;
    NBL_COUNTED_QUEUE PassList;
    NBL_QUEUE DropList;
    NBL_COUNTED_QUEUE TxList;
    NBL_QUEUE NblBatch;
    BOOLEAN PollDidWork = FALSE;
    XDP_RX_QUEUE_HANDLE XdpRxQueue;

    NdisInitializeNblQueue(&NblBatch);

    //
    // If the poll-owned NBL queue is empty, acquire the EcLock and check for
    // pending work.
    //
    if (NdisIsNblQueueEmpty(&RxQueue->TxInspectPollNblQueue)) {
        KeAcquireSpinLockAtDpcLevel(&RxQueue->EcLock);

        if (RxQueue->Flags.TxInspectWorker) {
            if (RxQueue->Flags.TxInspectNeedFlush) {
                RxQueue->Flags.TxInspectNeedFlush = FALSE;

                XdpRxQueue = ReadPointerNoFence(&RxQueue->XdpRxQueue);
                if (XdpRxQueue != NULL) {
                    XdpGenericFlushReceive(RxQueue, XdpRxQueue);
                }

                PollDidWork = TRUE;
            }

            if (NdisIsNblQueueEmpty(&RxQueue->TxInspectNblQueue)) {
                RxQueue->Flags.TxInspectWorker = FALSE;
            } else {
                NdisAppendNblQueueToNblQueueFast(
                    &RxQueue->TxInspectPollNblQueue, &RxQueue->TxInspectNblQueue);
            }
        }

        KeReleaseSpinLockFromDpcLevel(&RxQueue->EcLock);
    }

    //
    // Produce a batch of NBLs from the poll-owned NBL queue.
    //
    for (UINT32 Index = 0; Index < RECV_TX_INSPECT_BATCH_SIZE; Index++) {
        if (NdisIsNblQueueEmpty(&RxQueue->TxInspectPollNblQueue)) {
            break;
        }

        NdisAppendSingleNblToNblQueue(
            &NblBatch, NdisPopFirstNblFromNblQueue(&RxQueue->TxInspectPollNblQueue));
    }

    //
    // Submit the NBL batch to XDP for inspection. The NBLs are returned to NDIS
    // while other threads cannot perform XDP inspection.
    //
    if (!NdisIsNblQueueEmpty(&NblBatch)) {
        XdpGenericReceive(
            Generic, NdisGetNblChainFromNblQueue(&NblBatch), NDIS_DEFAULT_PORT_NUMBER, &PassList,
            &DropList, &TxList,
            XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH | XDP_LWF_GENERIC_INSPECT_FLAG_TX |
                XDP_LWF_GENERIC_INSPECT_FLAG_TX_WORKER);

        if (!NdisIsNblCountedQueueEmpty(&PassList)) {
            NdisFSendNetBufferLists(
                Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&PassList),
                NDIS_DEFAULT_PORT_NUMBER, NDIS_SEND_FLAGS_DISPATCH_LEVEL);
        }

        if (!NdisIsNblQueueEmpty(&DropList)) {
            NdisFSendNetBufferListsComplete(
                Generic->NdisFilterHandle, NdisGetNblChainFromNblQueue(&DropList),
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
        }

        if (!NdisIsNblCountedQueueEmpty(&TxList)) {
            NdisFIndicateReceiveNetBufferLists(
                Generic->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&TxList),
                NDIS_DEFAULT_PORT_NUMBER, (ULONG)TxList.NblCount,
                NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
        }

        PollDidWork = TRUE;
    }

    return PollDidWork;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericRxPauseQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue
    )
{
    KIRQL OldIrql;
    BOOLEAN NeedPause = FALSE;

    TraceEnter(
        TRACE_GENERIC, "RxQueue=%p IfIndex=%u QueueId=%u TxInspect=%!BOOLEAN!",
        RxQueue, Generic->IfIndex, RxQueue->QueueId, RxQueue->Flags.TxInspect);

    KeAcquireSpinLock(&RxQueue->EcLock, &OldIrql);
    if (!RxQueue->Flags.Paused) {
        RxQueue->Flags.Paused = TRUE;
        NeedPause = TRUE;
    }
    KeReleaseSpinLock(&RxQueue->EcLock, OldIrql);

    if (NeedPause) {
        ExWaitForRundownProtectionRelease(&RxQueue->NblRundown);
    }

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericRxPause(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    TraceEnter(TRACE_GENERIC, "IfIndex=%u", Generic->IfIndex);

    LIST_ENTRY *Entry = Generic->Rx.Queues.Flink;
    while (Entry != &Generic->Rx.Queues) {
        XDP_LWF_GENERIC_RX_QUEUE *RxQueue =
            CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_RX_QUEUE, Link);
        Entry = Entry->Flink;

        XdpGenericRxPauseQueue(Generic, RxQueue);
    }

    TraceExitSuccess(TRACE_GENERIC);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericRxRestartQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue
    )
{
    KIRQL OldIrql;

    TraceEnter(
        TRACE_GENERIC, "RxQueue=%p IfIndex=%u QueueId=%u TxInspect=%!BOOLEAN!",
        RxQueue, Generic->IfIndex, RxQueue->QueueId, RxQueue->Flags.TxInspect);

    KeAcquireSpinLock(&RxQueue->EcLock, &OldIrql);
    ASSERT(RxQueue->Flags.Paused);
    RxQueue->Flags.Paused = FALSE;
    KeReleaseSpinLock(&RxQueue->EcLock, OldIrql);

    ExReInitializeRundownProtection(&RxQueue->NblRundown);

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericRxRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 NewMtu
    )
{
    LIST_ENTRY *Entry = Generic->Rx.Queues.Flink;

    TraceEnter(TRACE_GENERIC, "IfIndex=%u NewMtu=%u", Generic->IfIndex, NewMtu);

    //
    // For RX, the interface MTU is currently not needed. To handle encap or
    // offloads, though, we will eventually need to track the MTU.
    //
    UNREFERENCED_PARAMETER(NewMtu);

    while (Entry != &Generic->Rx.Queues) {
        XDP_LWF_GENERIC_RX_QUEUE *RxQueue =
            CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_RX_QUEUE, Link);
        Entry = Entry->Flink;

        XdpGenericRxRestartQueue(Generic, RxQueue);
    }

    TraceExitSuccess(TRACE_GENERIC);
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericRxCreateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    NTSTATUS Status;
    XDP_LWF_GENERIC *Generic = (XDP_LWF_GENERIC *)InterfaceContext;
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = NULL;
    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue;
    CONST XDP_QUEUE_INFO *QueueInfo;
    CONST XDP_HOOK_ID *QueueHookId;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams = {0};
    XDP_EXTENSION_INFO ExtensionInfo;
    XDP_RX_CAPABILITIES RxCapabilities;
    XDP_RX_DESCRIPTOR_CONTEXTS DescriptorContexts;
    XDP_HOOK_ID HookId = {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_RX,
        .SubLayer   = XDP_HOOK_INSPECT,
    };
    DECLARE_UNICODE_STRING_SIZE(
        Name, ARRAYSIZE("if_" MAXUINT32_STR "_queue_" MAXUINT32_STR "_tx"));
    const WCHAR *DirectionString;

    QueueInfo = XdpRxQueueGetTargetQueueInfo(Config);

    QueueHookId = XdpRxQueueGetHookId(Config);
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
        HookId.SubLayer != XDP_HOOK_INSPECT ||
        (HookId.Direction != XDP_HOOK_RX && HookId.Direction != XDP_HOOK_TX)) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (HookId.Direction == XDP_HOOK_TX) {
        DirectionString = L"_tx";
    } else {
        ASSERT(HookId.Direction == XDP_HOOK_RX);
        DirectionString = L"";
    }

    RxQueue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RxQueue), POOLTAG_RECV);
    if (RxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    RxQueue->QueueId = QueueInfo->QueueId;
    RxQueue->Generic = Generic;
    ExInitializeRundownProtection(&RxQueue->NblRundown);
    InitializeSListHead(&RxQueue->TxCloneNblSList);
    RxQueue->TxCloneCacheLimit = RxMaxTxBuffers;
    RxQueue->Flags.TxInspect = (HookId.Direction == XDP_HOOK_TX);

    Status =
        RtlUnicodeStringPrintf(
            &Name, L"if_%u_queue_%u%s", Generic->IfIndex, QueueInfo->QueueId, DirectionString);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPcwCreateLwfRxQueue(&RxQueue->PcwInstance, &Name, &RxQueue->PcwStats);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_TCP_IP;
    PoolParams.PoolTag = POOLTAG_RECV_TX;
    PoolParams.fAllocateNetBuffer = TRUE;

    PoolParams.ContextSize = RX_TX_CONTEXT_SIZE;

    RxQueue->TxCloneNblPool = NdisAllocateNetBufferListPool(Generic->NdisFilterHandle, &PoolParams);
    if (RxQueue->TxCloneNblPool == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    if (RxQueue->Flags.TxInspect) {
        NdisInitializeNblQueue(&RxQueue->TxInspectNblQueue);
        NdisInitializeNblQueue(&RxQueue->TxInspectPollNblQueue);

        Status =
            XdpEcInitialize(
                &RxQueue->TxInspectEc, XdpGenericReceiveTxInspectPoll, RxQueue,
                &RssQueue->IdealProcessor);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    RtlAcquirePushLockExclusive(&Generic->Lock);
    RxQueue->Flags.Paused = Generic->Flags.Paused;
    InsertTailList(&Generic->Rx.Queues, &RxQueue->Link);
    RtlReleasePushLockExclusive(&Generic->Lock);

    if (RxQueue->Flags.TxInspect) {
        WritePointerRelease(&RssQueue->TxInspectQueue, RxQueue);
    } else {
        WritePointerRelease(&RssQueue->RxQueue, RxQueue);
    }

    //
    // Always attach to both RX and TX data paths: XDP_RX_ACTION_TX requires the
    // ability to hairpin traffic in the opposite direction it was inspected.
    //
    XdpGenericAttachDatapath(Generic, TRUE, TRUE);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_RX_ACTION_NAME,
        XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
        XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
        XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    RxQueue->FragmentLimit = RECV_MAX_FRAGMENTS;

    XdpInitializeRxCapabilitiesDriverVa(&RxCapabilities);
    RxCapabilities.MaximumFragments = RxQueue->FragmentLimit;
    RxCapabilities.TxActionSupported = TRUE;
    XdpRxQueueSetCapabilities(Config, &RxCapabilities);

    XdpInitializeRxDescriptorContexts(&DescriptorContexts);
    DescriptorContexts.FrameContextSize = sizeof(XDP_LWF_GENERIC_RX_FRAME_CONTEXT);
    DescriptorContexts.FrameContextAlignment = __alignof(XDP_LWF_GENERIC_RX_FRAME_CONTEXT);
    XdpRxQueueSetDescriptorContexts(Config, &DescriptorContexts);

    *InterfaceRxQueueDispatch = &RxDispatch;
    *InterfaceRxQueue = (XDP_INTERFACE_HANDLE)RxQueue;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (RxQueue != NULL) {
            XdpEcCleanup(&RxQueue->TxInspectEc);
            if (RxQueue->TxCloneNblPool != NULL) {
                NdisFreeNetBufferListPool(RxQueue->TxCloneNblPool);
            }
            if (RxQueue->PcwInstance != NULL) {
                PcwCloseInstance(RxQueue->PcwInstance);
            }
            ExFreePoolWithTag(RxQueue, POOLTAG_RECV);
        }
    }

    TraceExitStatus(TRACE_GENERIC);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericRxActivateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = (XDP_LWF_GENERIC_RX_QUEUE *)InterfaceRxQueue;
    XDP_EXTENSION_INFO ExtensionInfo;

    RxQueue->FrameRing = XdpRxQueueGetFrameRing(Config);
    RxQueue->FragmentRing = XdpRxQueueGetFragmentRing(Config);

    ASSERT(RxQueue->FrameRing->InterfaceReserved == RxQueue->FrameRing->ProducerIndex);

    ASSERT(XdpRxQueueIsVirtualAddressEnabled(Config));
    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->BufferVaExtension);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_RX_ACTION_NAME,
        XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->RxActionExtension);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
        XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->FragmentExtension);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
        XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->FrameInterfaceContextExtension);

    WritePointerRelease(&RxQueue->XdpRxQueue, XdpRxQueue);

    return STATUS_SUCCESS;
}

static
VOID
XdpGenericRxDeleteQueueEntry(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue;

    RxQueue = CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_RX_QUEUE, DeleteEntry);
    if (RxQueue->FragmentBuffer != NULL) {
        ExFreePoolWithTag(RxQueue->FragmentBuffer, POOLTAG_RECV);
    }
    XdpGenericRxFreeNblCloneCache(
        (NET_BUFFER_LIST *)InterlockedFlushSList(&RxQueue->TxCloneNblSList));
    XdpGenericRxFreeNblCloneCache(RxQueue->TxCloneNblList);
    RxQueue->TxCloneNblList = NULL;
    NdisFreeNetBufferListPool(RxQueue->TxCloneNblPool);
    XdpPcwCloseLwfRxQueue(RxQueue->PcwInstance);
    RxQueue->PcwInstance = NULL;
    KeSetEvent(RxQueue->DeleteComplete, 0, FALSE);
    ExFreePoolWithTag(RxQueue, POOLTAG_RECV);
}

static
VOID
XdpGenericRxDeleteTxInspectEc(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue;

    RxQueue = CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_RX_QUEUE, DeleteEntry);
    XdpEcCleanup(&RxQueue->TxInspectEc);

    XdpLifetimeDelete(XdpGenericRxDeleteQueueEntry, &RxQueue->DeleteEntry);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpGenericRxDeleteQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    )
{
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = (XDP_LWF_GENERIC_RX_QUEUE *)InterfaceRxQueue;
    XDP_LWF_GENERIC *Generic = RxQueue->Generic;
    KEVENT DeleteComplete;

    TraceEnter(TRACE_GENERIC, "IfIndex=%u QueueId=%u", Generic->IfIndex, RxQueue->QueueId);

    if (RxQueue->Flags.TxInspect) {
        #pragma warning(suppress:6387) // WritePointerRelease second parameter is not _In_opt_
        WritePointerRelease(&Generic->Rss.Queues[RxQueue->QueueId].TxInspectQueue, NULL);
    } else {
        #pragma warning(suppress:6387) // WritePointerRelease second parameter is not _In_opt_
        WritePointerRelease(&Generic->Rss.Queues[RxQueue->QueueId].RxQueue, NULL);
    }

    RtlAcquirePushLockExclusive(&Generic->Lock);
    XdpGenericRxPauseQueue(Generic, RxQueue);
    RemoveEntryList(&RxQueue->Link);
    RtlReleasePushLockExclusive(&Generic->Lock);

    XdpGenericDetachDatapath(Generic, TRUE, TRUE);

    KeInitializeEvent(&DeleteComplete, NotificationEvent, FALSE);
    RxQueue->DeleteComplete = &DeleteComplete;
    XdpLifetimeDelete(XdpGenericRxDeleteTxInspectEc, &RxQueue->DeleteEntry);
    KeWaitForSingleObject(&DeleteComplete, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_GENERIC);
}

VOID
XdpGenericReceiveRegistryUpdate(
    VOID
    )
{
    NTSTATUS Status;
    DWORD Value;

    Status =
        XdpRegQueryDwordValue(
            XDP_LWF_PARAMETERS_KEY, L"GenericRxFwdBufferLimit", &Value);
    if (NT_SUCCESS(Status) && Value <= RECV_MAX_MAX_TX_BUFFERS) {
        RxMaxTxBuffers = Value;
    } else {
        RxMaxTxBuffers = RECV_DEFAULT_MAX_TX_BUFFERS;
    }
}
