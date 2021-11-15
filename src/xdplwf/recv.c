//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "recv.tmh"

#define RECV_MAX_FRAGMENTS 64
#define RECV_TX_INSPECT_BATCH_SIZE 64

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
        // RSS is uninitialized, so pass the NBLs through.
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
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
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

    //
    // Generic XDP reserves a single contiguous buffer for linearization; if a
    // NB contains more than the maximum number of fragments (MDLs), copy the
    // contents of the entire MDL chain into one contiguous buffer.
    //
    // If the contiguous buffer is already in use, flush the queue first.
    //
    if (RxQueue->FragmentBufferInUse) {
        XdpGenericFlushReceive(RxQueue, XdpRxQueue);
    }

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
        if (MdlBuffer == NULL) {
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
XDP_RX_ACTION
XdpGenericReceiveNbl(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    XDP_RING *FrameRing = RxQueue->FrameRing;
    XDP_RING *FragmentRing = RxQueue->FragmentRing;
    XDP_FRAME *Frame;
    XDP_FRAME_FRAGMENT *FragmentExtension;
    UINT8 FragmentCount;
    XDP_BUFFER *Buffer;
    MDL *Mdl;
    XDP_BUFFER_VIRTUAL_ADDRESS *SystemVa;
    UINT32 DataLength;
    XDP_RX_ACTION Action;

    do {
        ASSERT(XdpRingFree(FrameRing) > 0);
        Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex & FrameRing->Mask);

        Buffer = &Frame->Buffer;
        DataLength = NET_BUFFER_DATA_LENGTH(Nb);
        Mdl = Nb->CurrentMdl;
        Buffer->DataOffset = Nb->CurrentMdlOffset;
        Buffer->DataLength = min(Mdl->ByteCount - Buffer->DataOffset, DataLength);
        Buffer->BufferLength = Mdl->ByteCount;
        DataLength -= Buffer->DataLength;
        FragmentCount = 0;

        SystemVa = XdpGetVirtualAddressExtension(Buffer, &RxQueue->BufferVaExtension);
        SystemVa->VirtualAddress =
            MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority | MdlMappingNoExecute);
        if (SystemVa->VirtualAddress == NULL) {
            Action = XDP_RX_ACTION_DROP;
            goto Exit;
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
                if (!XdpGenericReceiveLinearizeNb(RxQueue, XdpRxQueue, Nb)) {
                    Action = XDP_RX_ACTION_DROP;
                    goto Exit;
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
                XdpGenericFlushReceive(RxQueue, XdpRxQueue);

                ASSERT(XdpRingFree(FragmentRing) >= FragmentCount);
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
            if (SystemVa->VirtualAddress == NULL) {
                Action = XDP_RX_ACTION_DROP;
                goto Exit;
            }
        }

        //
        // The NB has successfully been converted to XDP descriptors, so commit
        // the descriptors to the XDP rings and invoke XDP inspection.
        //
        FragmentExtension = XdpGetFragmentExtension(Frame, &RxQueue->FragmentExtension);
        FragmentExtension->FragmentBufferCount = FragmentCount;
        FragmentRing->ProducerIndex += FragmentCount;
        FrameRing->ProducerIndex++;
        Action = XdpReceive(XdpRxQueue);

        //
        // Currently XDP does not advance/retreat buffers, so there's no need to
        // explicitly update the NB; however, the payload data may have already
        // been rewritten.
        //

        //
        // Advance to the next NB in the NBL. NBLs with multiple NBs are only
        // permitted on the NDIS send path.
        //
        // Since all NBs sharing an NBL are grouped by flow, simply return the
        // XDP action for the last NB in the NBL. This is an oversimplification
        // (we need to split NBLs in the general case) but is sufficient for the
        // filtering conditions supported by XDP today: all map to a property
        // greater than or equal to the scope of a single flow, so filters today
        // cannot split NBLs.
        //
        Nb = Nb->Next;

        if (Nb != NULL && XdpRingFree(FrameRing) == 0) {
            XdpGenericFlushReceive(RxQueue, XdpRxQueue);
        }
    } while (Nb != NULL);

Exit:
    return Action;
}

VOID
XdpGenericReceive(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _Out_ NBL_COUNTED_QUEUE *PassList,
    _Out_ NBL_QUEUE *DropList,
    _In_ UINT32 XdpInspectFlags
    )
{
    NBL_QUEUE LowResourcesList;
    KIRQL OldIrql = DISPATCH_LEVEL;
    ULONG Processor;
    BOOLEAN CanPend = !(XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_RESOURCES);
    BOOLEAN TxInspect = XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_TX;
    BOOLEAN TxWorker = XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_TX_WORKER;
    BOOLEAN NeedFlush = FALSE;
    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue = NULL;
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = NULL;
    XDP_RX_QUEUE_HANDLE XdpRxQueue = NULL;

    NdisInitializeNblCountedQueue(PassList);
    NdisInitializeNblQueue(DropList);
    NdisInitializeNblQueue(&LowResourcesList);

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

    while (NetBufferLists != NULL) {
        NET_BUFFER_LIST *Nbl = NetBufferLists;
        XDP_RX_ACTION Action;

        NetBufferLists = NetBufferLists->Next;

        //
        // Convert the NBL to XDP descriptors and submit to XDP for inspection.
        //
        Action = XdpGenericReceiveNbl(RxQueue, XdpRxQueue, Nbl);

        switch (Action) {

        case XDP_RX_ACTION_PASS:
            NdisAppendSingleNblToNblCountedQueue(PassList, Nbl);
            break;

        case XDP_RX_ACTION_PEND:
            NeedFlush = TRUE;
            __fallthrough;

        case XDP_RX_ACTION_DROP:
            NdisAppendSingleNblToNblQueue(DropList, Nbl);
            break;

        default:
            ASSERT(FALSE);
        }

        if (!CanPend) {
            //
            // Enforce NDIS low resources constraints on pass and return lists.
            // N.B. This releases and reacquires the EC spinlock.
            //
            ASSERT(!RxQueue->Flags.TxInspect);
            XdpGenericReceiveLowResources(
                Generic->NdisFilterHandle, &RxQueue->EcLock, PassList, DropList, &LowResourcesList,
                PortNumber, (NetBufferLists == NULL));
        }
    }

    if (NeedFlush) {
        XdpGenericFlushReceive(RxQueue, XdpRxQueue);
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

    if (OldIrql != DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }
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
    NBL_COUNTED_QUEUE PassList;
    NBL_QUEUE DropList;

    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    XdpGenericReceive(
        Generic, NetBufferLists, PortNumber, &PassList, &DropList,
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
            NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags) ?
                NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
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
            &DropList,
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

        PollDidWork = TRUE;
    }

    return PollDidWork;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericAttachIfRx(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath
    )
{
    BOOLEAN NeedRestart;
    LARGE_INTEGER Timeout;

    ExAcquirePushLockExclusive(&Generic->Lock);
    XdpGenericReferenceDatapath(Generic, Datapath, &NeedRestart);
    ExReleasePushLockExclusive(&Generic->Lock);

    if (NeedRestart) {
        TraceVerbose(TRACE_GENERIC, "IfIndex=%u Requesting RX datapath attach", Generic->IfIndex);
        XdpGenericRequestRestart(Generic);
    }

    Timeout.QuadPart =
        -1 * RTL_MILLISEC_TO_100NANOSEC(GENERIC_DATAPATH_RESTART_TIMEOUT_MS);
    KeWaitForSingleObject(
        &Datapath->ReadyEvent, Executive, KernelMode, FALSE, &Timeout);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericDetachIfRx(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath
    )
{
    BOOLEAN NeedRestart;

    ExAcquirePushLockExclusive(&Generic->Lock);
    XdpGenericDereferenceDatapath(Generic, Datapath, &NeedRestart);
    ExReleasePushLockExclusive(&Generic->Lock);

    if (NeedRestart) {
        TraceVerbose(TRACE_GENERIC, "IfIndex=%u Requesting RX datapath detach", Generic->IfIndex);
        XdpGenericRequestRestart(Generic);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
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
    XDP_EXTENSION_INFO ExtensionInfo;
    XDP_RX_CAPABILITIES RxCapabilities;
    XDP_HOOK_ID HookId = {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_RX,
        .SubLayer   = XDP_HOOK_INSPECT,
    };

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

    RxQueue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RxQueue), POOLTAG_RECV);
    if (RxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    RxQueue->QueueId = QueueInfo->QueueId;
    RxQueue->Generic = Generic;
    RxQueue->Flags.TxInspect = (HookId.Direction == XDP_HOOK_TX);

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

    if (RxQueue->Flags.TxInspect) {
        WritePointerRelease(&RssQueue->TxInspectQueue, RxQueue);
        XdpGenericAttachIfRx(Generic, &Generic->Tx.Datapath);
    } else {
        WritePointerRelease(&RssQueue->RxQueue, RxQueue);
        XdpGenericAttachIfRx(Generic, &Generic->Rx.Datapath);
    }

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
        XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueRegisterExtensionVersion(Config, &ExtensionInfo);

    RxQueue->FragmentLimit = RECV_MAX_FRAGMENTS;

    XdpInitializeRxCapabilitiesDriverVa(&RxCapabilities);
    RxCapabilities.MaximumFragments = RxQueue->FragmentLimit;
    XdpRxQueueSetCapabilities(Config, &RxCapabilities);

    *InterfaceRxQueueDispatch = &RxDispatch;
    *InterfaceRxQueue = (XDP_INTERFACE_HANDLE)RxQueue;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (RxQueue != NULL) {
            XdpEcCleanup(&RxQueue->TxInspectEc);
            ExFreePoolWithTag(RxQueue, POOLTAG_RECV);
        }
    }

    TraceExitStatus(TRACE_GENERIC);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
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

    ASSERT(XdpRxQueueIsVirtualAddressEnabled(Config));
    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->BufferVaExtension);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
        XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &RxQueue->FragmentExtension);

    WritePointerRelease(&RxQueue->XdpRxQueue, XdpRxQueue);
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

_IRQL_requires_max_(PASSIVE_LEVEL)
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
        XdpGenericDetachIfRx(Generic, &Generic->Tx.Datapath);
    } else {
        #pragma warning(suppress:6387) // WritePointerRelease second parameter is not _In_opt_
        WritePointerRelease(&Generic->Rss.Queues[RxQueue->QueueId].RxQueue, NULL);
        XdpGenericDetachIfRx(Generic, &Generic->Rx.Datapath);
    }

    KeInitializeEvent(&DeleteComplete, NotificationEvent, FALSE);
    RxQueue->DeleteComplete = &DeleteComplete;
    XdpLifetimeDelete(XdpGenericRxDeleteTxInspectEc, &RxQueue->DeleteEntry);
    KeWaitForSingleObject(&DeleteComplete, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_GENERIC);
}
