//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "recv.tmh"

#define RECV_MAX_FRAGMENTS 64
#define RECV_TX_INSPECT_BATCH_SIZE 64
#define RECV_DEFAULT_MAX_TX_BUFFERS 1024
#define RECV_MAX_MAX_TX_BUFFERS 65536
#define RECV_MAX_GSO_HEADER_SIZE (sizeof(ETHERNET_HEADER) + sizeof(IPV6_HEADER) + TH_MAX_LEN)
#define RECV_MAX_GSO_PAYLOAD_SIZE MAXUINT16

//
// Rather than tracking the current lookaside via OIDs, which is subject to
// theoretical race conditions, simply set the minimum lookahead for forwarding
// TX inspected packets onto the RX path based on the known minimums for TCPIP
// and TCPIP6. Older OS builds also erroneously configure excessively large
// lookasides. The value here is the maximum IPv4 header size, which is larger
// than the IPv6 header, plus the L2 header.
//
#define RECV_TX_INSPECT_LOOKAHEAD (sizeof(ETHERNET_HEADER) + (0xF * sizeof(UINT32)))

#define DEFINE_MDL_AND_PFNS(_Name, _Size) \
    MDL _Name; PFN_NUMBER _Name##Pfns[ADDRESS_AND_SIZE_TO_SPAN_PAGES(PAGE_SIZE - 1, (_Size))]
C_ASSERT(__alignof(MDL) % __alignof(PFN_NUMBER) == 0);
C_ASSERT(sizeof(MDL) % __alignof(PFN_NUMBER) == 0);

typedef struct _XDP_LWF_GENERIC_RX_FRAME_CONTEXT {
    NET_BUFFER *Nb;
} XDP_LWF_GENERIC_RX_FRAME_CONTEXT;

typedef struct _NBL_RX_TX_CONTEXT {
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue;
    XDP_LWF_GENERIC_INJECTION_TYPE InjectionType;

    union {
        struct {
            UINT8 Gso : 1;
        };
        UINT8 Value;
    } ValidFields;

    union {
        struct {
            UCHAR Headers[RECV_MAX_GSO_HEADER_SIZE];
            DEFINE_MDL_AND_PFNS(HeaderMdl, RECV_MAX_GSO_HEADER_SIZE);
            DEFINE_MDL_AND_PFNS(PartialMdl, RECV_MAX_GSO_PAYLOAD_SIZE);
        } Gso;
    };
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
            NET_BUFFER *Nb = Nbl->FirstNetBuffer;
            ASSERT(Nb->Next == NULL);
            NdisAdvanceNetBufferDataStart(Nb, Nb->DataLength, TRUE, NULL);
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

static const XDP_INTERFACE_RX_QUEUE_DISPATCH RxDispatch = {
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
XdpGenericRxChecksumNbl(
    _Inout_ NET_BUFFER_LIST *Nbl
    )
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *Csum =
        (NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *)
            &NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo);
    NET_BUFFER *Nb = Nbl->FirstNetBuffer;
    ETHERNET_HEADER *Ethernet = RTL_PTR_ADD(Nb->CurrentMdl->MappedSystemVa, Nb->CurrentMdlOffset);

    ASSERT(Nb->CurrentMdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL));
    ASSERT(Nb->Next == NULL);

    //
    // For simplicity, assume all checksum fields lie within the first MDL.
    // Since this code path is only used by RSC/LSO, this is safe, fast, and
    // simple.
    //
    ASSERT(Csum->Transmit.TcpChecksum);
    ASSERT(
        Csum->Transmit.TcpHeaderOffset + RTL_SIZEOF_THROUGH_FIELD(TCP_HDR, th_sum) <=
            Nb->CurrentMdl->ByteCount - Nb->CurrentMdlOffset);

    if (Csum->Transmit.IpHeaderChecksum) {
        IPV4_HEADER *Ipv4 = RTL_PTR_ADD(Ethernet, sizeof(ETHERNET_HEADER));

        ASSERT(Csum->Transmit.TcpHeaderOffset > sizeof(ETHERNET_HEADER));

        Ipv4->HeaderChecksum =
            XdpOffloadChecksumNb(
                Nb, Csum->Transmit.TcpHeaderOffset - sizeof(ETHERNET_HEADER),
                sizeof(ETHERNET_HEADER));
    }

    if (Csum->Transmit.TcpChecksum) {
        TCP_HDR *Tcp = RTL_PTR_ADD(Ethernet, Csum->Transmit.TcpHeaderOffset);

        ASSERT(Csum->Transmit.TcpHeaderOffset < Nb->DataLength);

        Tcp->th_sum =
            XdpOffloadChecksumNb(
                Nb, Nb->DataLength - Csum->Transmit.TcpHeaderOffset,
                Csum->Transmit.TcpHeaderOffset);
    }

    //
    // Clear the checksum OOB after all offloads have been performed in SW.
    //
    Csum->Value = NULL;
}

static
VOID
XdpGenericRxClearNblCloneData(
    _Inout_ NET_BUFFER_LIST *Nbl
    )
{
    Nbl->FirstNetBuffer->MdlChain = NULL;
    Nbl->FirstNetBuffer->CurrentMdl = NULL;
    Nbl->FirstNetBuffer->DataLength = 0;
    Nbl->FirstNetBuffer->DataOffset = 0;
    Nbl->FirstNetBuffer->CurrentMdlOffset = 0;
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
        XdpGenericRxClearNblCloneData(Nbl);

        NdisFreeNetBufferList(Nbl);
    }
}

static
FORCEINLINE
VOID
XdpGenericRxReturnTxCloneNbl(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NET_BUFFER_LIST *TxNbl
    )
{
    TxNbl->Next = RxQueue->TxCloneNblList;
    RxQueue->TxCloneNblList = TxNbl;
}

static
FORCEINLINE
NET_BUFFER_LIST *
XdpGenericRxAllocateTxCloneNbl(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue
    )
{
    NET_BUFFER_LIST *TxNbl;

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
            STAT_INC(&RxQueue->PcwStats, ForwardingFailuresAllocation);
            return NULL;
        }

        RxQueue->TxCloneCacheCount++;

        NblRxTxContext(TxNbl)->ValidFields.Value = 0;
    } else {
        TxNbl = NULL;
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresAllocationLimit);
        return NULL;
    }

    ASSERT(TxNbl != NULL);
    ASSERT(TxNbl->FirstNetBuffer->Next == NULL);

    TxNbl->NetBufferListInfo[TcpIpChecksumNetBufferListInfo] = 0;
    TxNbl->NetBufferListInfo[TcpLargeSendNetBufferListInfo] = 0;

    return TxNbl;
}

static
FORCEINLINE
VOID
XdpGenericRxEnqueueTxNbl(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _Inout_ NET_BUFFER_LIST *Nbl,
    _Inout_ NET_BUFFER_LIST *TxNbl
    )
{
    NblRxTxContext(TxNbl)->RxQueue = RxQueue;
    NblRxTxContext(TxNbl)->InjectionType = XDP_LWF_GENERIC_INJECTION_RECV;
    TxNbl->SourceHandle = RxQueue->Generic->NdisFilterHandle;
    NET_BUFFER_LIST_SET_HASH_VALUE(TxNbl, NET_BUFFER_LIST_GET_HASH_VALUE(Nbl));
    NdisAppendSingleNblToNblCountedQueue(TxList, TxNbl);
    STAT_INC(&RxQueue->PcwStats, ForwardingNbsSent);
}

static
FORCEINLINE
BOOLEAN
XdpGenericRxCloneOrCopyTxNblData2(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NET_BUFFER_LIST *Nbl,
    _In_ UINT32 DataLength,
    _In_ UINT32 IdealBackfill,
    _In_ MDL *Mdl,
    _In_ UINT32 MdlOffset,
    _In_ BOOLEAN CanPend,
    _Inout_ NET_BUFFER_LIST *TxNbl
    )
{
    NET_BUFFER *TxNb = TxNbl->FirstNetBuffer;

    //
    // If we are allowed to clone and pend NBLs, reuse the existing MDL chain
    // and reference the parent net buffer list.
    //
    // We cannot reuse the MDL chain if the NBL is being forwarded onto the
    // local RX path and the frame is potentially discontiguous within L2 or L3
    // headers; in that case, allocate a new MDL chain and copy the data.
    //

    if (CanPend &&
            (!RxQueue->Flags.TxInspect ||
                Mdl->ByteCount - MdlOffset >= RECV_TX_INSPECT_LOOKAHEAD)) {
        TxNb->MdlChain = Mdl;
        TxNb->CurrentMdl = Mdl;
        TxNb->DataLength = DataLength;
        TxNb->DataOffset = MdlOffset;
        TxNb->CurrentMdlOffset = MdlOffset;
        TxNbl->ParentNetBufferList = Nbl;
        Nbl->ChildRefCount++;
    } else {
        NDIS_STATUS NdisStatus;

        STAT_INC(&RxQueue->PcwStats, ForwardingLowResources);

        XdpGenericRxClearNblCloneData(TxNbl);

        NdisStatus = NdisRetreatNetBufferDataStart(TxNb, DataLength, IdealBackfill, NULL);
        if (NdisStatus != NDIS_STATUS_SUCCESS) {
            STAT_INC(&RxQueue->PcwStats, ForwardingFailuresAllocation);
            return FALSE;
        }

        //
        // NBL data copy cannot fail because XDP has already mapped all MDLs
        // into the system virtual address space.
        //
        NT_VERIFY(
            NT_SUCCESS(
                MdlCopyMdlChainToMdlChainAtOffsetNonTemporal(
                    TxNb->CurrentMdl, TxNb->CurrentMdlOffset, Mdl, MdlOffset, DataLength)));

        TxNbl->ParentNetBufferList = NULL;
    }

    return TRUE;
}

static
FORCEINLINE
BOOLEAN
XdpGenericRxCloneOrCopyTxNblData(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NET_BUFFER_LIST *Nbl,
    _In_ NET_BUFFER *Nb,
    _In_ BOOLEAN CanPend,
    _Inout_ NET_BUFFER_LIST *TxNbl
    )
{
    return
        XdpGenericRxCloneOrCopyTxNblData2(
            RxQueue, Nbl, Nb->DataLength, Nb->DataOffset, Nb->CurrentMdl, Nb->CurrentMdlOffset,
            CanPend, TxNbl);
}

static
DECLSPEC_NOINLINE
VOID
XdpGenericRxInitializeGsoContextMdl(
    _Inout_ NBL_RX_TX_CONTEXT *NblContext
    )
{
    MmInitializeMdl(
        &NblContext->Gso.HeaderMdl, NblContext->Gso.Headers, sizeof(NblContext->Gso.Headers));
    MmBuildMdlForNonPagedPool(&NblContext->Gso.HeaderMdl);
    NblContext->ValidFields.Value = 0;
}

static
FORCEINLINE
VOID
XdpGenericRxInitializeGsoContext(
    _Inout_ NBL_RX_TX_CONTEXT *NblContext,
    _In_ UINT32 HeaderSize
    )
{
    if (!NblContext->ValidFields.Gso) {
        XdpGenericRxInitializeGsoContextMdl(NblContext);
        NblContext->ValidFields.Gso = TRUE;
    }

    ASSERT(HeaderSize <= sizeof(NblContext->Gso.Headers));
    NblContext->Gso.HeaderMdl.ByteCount = HeaderSize;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
DECLSPEC_NOINLINE
VOID
XdpGenericRxSegmentRscToLso(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _In_ NET_BUFFER_LIST *Nbl,
    _In_opt_ const XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *const TaskOffload,
    _In_ const BOOLEAN CanPend,
    _In_ const NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO LsoInfo,
    _In_ const UINT32 TcpTotalHdrLen
    )
{
    const UINT32 TcpOffset = LsoInfo.LsoV2Transmit.TcpHeaderOffset;
    const UINT32 LsoMss = LsoInfo.LsoV2Transmit.MSS;
    const UINT32 LsoIpVersion = LsoInfo.LsoV2Transmit.IPVersion;
    NET_BUFFER *const Nb = Nbl->FirstNetBuffer;
    MDL *Mdl = Nb->CurrentMdl;
    UINT32 MdlOffset = Nb->CurrentMdlOffset;
    ETHERNET_HEADER *const RscEthernet = RTL_PTR_ADD(Mdl->MappedSystemVa, Nb->CurrentMdlOffset);
    TCP_HDR *const RscTcp = RTL_PTR_ADD(RscEthernet, TcpOffset);
    const UINT8 RscThFlags = RscTcp->th_flags;
    UINT32 SeqNum = ntohl(RscTcp->th_seq);
    UINT32 TcpUserDataLen = Nb->DataLength - TcpTotalHdrLen;
    UINT32 MinOffloadSize = MAXUINT32;
    NET_BUFFER_LIST *TxNbl;
    UINT32 SegmentsCreated = 0;

    //
    // This routine takes an arbitrary RSC frame and produces as many LSO
    // segments as possible, followed by as many regular segments as needed.
    // This routine also supports arbitrary hardware offload capabilities.
    //

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!RxQueue->Flags.TxInspect);
    ASSERT(NET_BUFFER_LIST_IS_TCP_RSC_SET(Nbl));
    ASSERT(Nb->Next == NULL);
    ASSERT(LsoMss > 0);
    ASSERT(TcpTotalHdrLen <= Nb->DataLength);

    //
    // Clear any TCP flags that require special handling before they get copied
    // into each segment.
    //
    RscTcp->th_flags &= ~(TH_PSH);

    if (TaskOffload != NULL) {
        UINT32 CandidateMinOffloadSize;

        ASSERT(TaskOffload->Lso.MinSegments >= 1);
        CandidateMinOffloadSize = (TaskOffload->Lso.MinSegments - 1) * LsoMss + 1;

        //
        // Simplify the inner loop below by requiring the minimum offload size
        // be compatible with the maximum offload size.
        //
        if (CandidateMinOffloadSize <= TaskOffload->Lso.MaxOffloadSize) {
            MinOffloadSize = CandidateMinOffloadSize;
        }
    }

    //
    // Advance past headers to the start of TCP payload. The MDLs must be
    // contiguous through the TCP header, but the first byte of data could be in
    // the remainder of the MDL chain.
    //

    MdlOffset += TcpTotalHdrLen;
    ASSERT(MdlOffset <= Mdl->ByteCount);
    if (MdlOffset == Mdl->ByteCount) {
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    //
    // Generate a chain of NBLs for the original RSC frame, producing as many
    // LSO frames as possible using the greedy algorithm (not globally optimal)
    // followed by regular NBLs.
    //
    // N.B. We must always create at least one NBL, even for pure ACKs with no
    // TCP payload.
    //

    do {
        UINT32 SegmentSize = TcpUserDataLen;
        UINT32 MssInSegment;
        NET_BUFFER *TxNb;
        TCP_HDR *TxTcp;
        NBL_RX_TX_CONTEXT *NblContext;
        BOOLEAN NeedChecksum = FALSE;

        TxNbl = XdpGenericRxAllocateTxCloneNbl(RxQueue);
        if (TxNbl == NULL) {
            goto Exit;
        }

        TxNb = TxNbl->FirstNetBuffer;
        NblContext = NblRxTxContext(TxNbl);

        //
        // Copy L2-L4 headers from the RSC packet into the clone.
        //
        XdpGenericRxInitializeGsoContext(NblContext, TcpTotalHdrLen);
        RtlCopyMemory(NblContext->Gso.Headers, RscEthernet, TcpTotalHdrLen);
        TxTcp = RTL_PTR_ADD(NblContext->Gso.Headers, TcpOffset);

        //
        // Figure out how much data we can send, generating as many LSO segments
        // as possible, followed by regular TCP segments.
        //
        if (TcpUserDataLen >= MinOffloadSize) {
            //
            // Generate LSO segments.
            //

            NET_BUFFER_LIST_INFO(TxNbl, TcpLargeSendNetBufferListInfo) = LsoInfo.Value;

            if (TcpUserDataLen > TaskOffload->Lso.MaxOffloadSize) {
                //
                // Review: these values are loop-invariant and could be
                // precomputed.
                //
                MssInSegment = TaskOffload->Lso.MaxOffloadSize / LsoMss;
                SegmentSize = MssInSegment * LsoMss;
            } else {
                MssInSegment = (TcpUserDataLen + LsoMss - 1) / LsoMss;
            }
        } else {
            //
            // Generate a single TCP segment.
            //

            NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *Csum =
                (NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *)
                    &NET_BUFFER_LIST_INFO(TxNbl, TcpIpChecksumNetBufferListInfo);
            UINT16 TcpHeaderAndPayloadLength;

            SegmentSize = min(TcpUserDataLen, LsoMss);
            MssInSegment = 1;
            TcpHeaderAndPayloadLength = (UINT16)(TcpTotalHdrLen - TcpOffset + SegmentSize);

            ASSERT(Csum->Value == NULL);
            Csum->Transmit.TcpChecksum = TRUE;
            Csum->Transmit.TcpHeaderOffset = TcpOffset;
            TxTcp->th_sum =
                XdpChecksumFold(TxTcp->th_sum + htons(TcpHeaderAndPayloadLength));

            if (LsoIpVersion == NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4) {
                IPV4_HEADER *Ipv4 =
                    RTL_PTR_ADD(NblContext->Gso.Headers, sizeof(ETHERNET_HEADER));

                Ipv4->TotalLength =
                    htons((UINT16)(TcpTotalHdrLen - sizeof(ETHERNET_HEADER) + SegmentSize));
                Csum->Transmit.IpHeaderChecksum = TRUE;
                Csum->Transmit.IsIPv4 = TRUE;
            } else {
                IPV6_HEADER *Ipv6 =
                    RTL_PTR_ADD(NblContext->Gso.Headers, sizeof(ETHERNET_HEADER));

                ASSERT(LsoIpVersion == NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6);

                Ipv6->PayloadLength = htons(TcpHeaderAndPayloadLength);
                Csum->Transmit.IsIPv6 = TRUE;
            }

            if (TaskOffload == NULL || !TaskOffload->Checksum.Enabled) {
                NeedChecksum = TRUE;
            }
        }

        ASSERT(SegmentSize > 0 || (TcpUserDataLen == 0 && SegmentsCreated == 0));
        ASSERT(MssInSegment > 0);

        //
        // Finalize IP and TCP header values.
        //

        TxTcp->th_seq = htonl(SeqNum);

        if (SegmentSize == TcpUserDataLen) {
            //
            // This is the final segment.
            //
            TxTcp->th_flags |= (RscThFlags & TH_PSH);
        }

        if (LsoIpVersion == NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4) {
            IPV4_HEADER *Ipv4 = RTL_PTR_ADD(NblContext->Gso.Headers, sizeof(ETHERNET_HEADER));

            //
            // Update the IP identification field. The LSO spec requires
            // segments be constrained to a subset of the range (0x0000 to
            // 0x7FFF) but this is incompatible with arbitrary packet
            // sources (e.g. Linux) and relies on a deprecated feature
            // (Chimney). Violate the spec and use all 16 bits, which should
            // be harmless.
            //
            // N.B. the RSC packet could have had been created from a series
            // of packets with arbitrary IDs, including all zero IDs. Re-
            // segmenting the packet will produce different IDs, but this
            // should be harmless according to RFC 6864.
            //
            Ipv4->Identification = htons((UINT16)(ntohs(Ipv4->Identification) + SegmentsCreated));
        }

        //
        // If the first MDL of payload is already being used by a previous
        // segment, create a partial MDL. This ensures each NB contains unique
        // MDLs, as required by NDIS, and that the MDL offset for data is always
        // zero.
        //
        if (MdlOffset != 0) {
            UINT32 PartialMdlLength = min(Mdl->ByteCount - MdlOffset, SegmentSize);

            ASSERT(PartialMdlLength <= RECV_MAX_GSO_PAYLOAD_SIZE);
            IoBuildPartialMdl(
                Mdl, &NblContext->Gso.PartialMdl,
                RTL_PTR_ADD(MmGetMdlVirtualAddress(Mdl), MdlOffset), PartialMdlLength);

            TxNb->CurrentMdl = &NblContext->Gso.PartialMdl;

            //
            // Link the remainder of the MDL chain behind the partial MDL. The
            // next MDL needs to be valid only if the segment contains data
            // beyond the partial MDL.
            //
            ASSERT(PartialMdlLength == SegmentSize || Mdl->Next != NULL);
            TxNb->CurrentMdl->Next = Mdl->Next;
        } else {
            TxNb->CurrentMdl = Mdl;
        }

        //
        // Link the headers in ahead of the TCP data payload.
        //
        NblContext->Gso.HeaderMdl.Next = TxNb->CurrentMdl;
        TxNb->CurrentMdl = &NblContext->Gso.HeaderMdl;

        //
        // Clone or copy the TCP data into the clone NBL.
        //
        if (!XdpGenericRxCloneOrCopyTxNblData2(
                RxQueue, Nbl, TcpTotalHdrLen + SegmentSize, 0, TxNb->CurrentMdl, 0,
                CanPend, TxNbl)) {
            XdpGenericRxReturnTxCloneNbl(RxQueue, TxNbl);
            goto Exit;
        }

        if (NeedChecksum) {
            XdpGenericRxChecksumNbl(TxNbl);
        }

        XdpGenericRxEnqueueTxNbl(RxQueue, TxList, Nbl, TxNbl);

        //
        // Move on to the next TCP segments.
        //
        SegmentsCreated += MssInSegment;
        SeqNum += SegmentSize;
        ASSERT(TcpUserDataLen >= SegmentSize);
        TcpUserDataLen -= SegmentSize;

        while (SegmentSize > 0) {
            if (SegmentSize >= Mdl->ByteCount - MdlOffset) {
                SegmentSize -= Mdl->ByteCount - MdlOffset;
                Mdl = Mdl->Next;
                MdlOffset = 0;
            } else {
                MdlOffset += SegmentSize;
                SegmentSize = 0;
            }
        }
    } while (TcpUserDataLen > 0);

Exit:

    return;
}

static
_IRQL_requires_(DISPATCH_LEVEL)
FORCEINLINE
VOID
XdpGenericRxConvertRscToLso(
    _In_ XDP_LWF_GENERIC_RX_QUEUE *RxQueue,
    _Inout_ NBL_COUNTED_QUEUE *TxList,
    _In_ NET_BUFFER_LIST *Nbl,
    _In_ BOOLEAN CanPend
    )
{
    NET_BUFFER *Nb = Nbl->FirstNetBuffer;
    MDL *Mdl = Nb->CurrentMdl;
    UINT32 DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(Nb);
    UINT32 MdlDataLength = min(Mdl->ByteCount - DataOffset, Nb->DataLength);
    ETHERNET_HEADER *Ethernet = RTL_PTR_ADD(Mdl->MappedSystemVa, DataOffset);
    UINT8 IpAddressLength;
    const VOID *IpSrc;
    const VOID *IpDst;
    UINT32 TruncatedBytes = 0;
    UINT16 IpPayloadLength;
    UINT16 TcpOffset;
    UINT16 TcpHdrLen;
    UINT32 TcpTotalHdrLen;
    UINT32 TcpUserDataLen;
    UINT32 TcpUserDataLenOrPureAck;
    UINT32 RscMss;
    UINT32 TxMss;
    TCP_HDR *Tcp;
    const XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *TaskOffload;
    UINT16 NumSeg = NET_BUFFER_LIST_COALESCED_SEG_COUNT(Nbl);
    UINT32 LsoMss;
    NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO LsoInfo = {
        .LsoV2Transmit.Type = NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE,
    };

    //
    // This routine converts an RSC NBL into one or more NBLs for forwarding.
    //

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!RxQueue->Flags.TxInspect);
    ASSERT(NET_BUFFER_LIST_IS_TCP_RSC_SET(Nbl));
    ASSERT(Nb->Next == NULL);
    ASSERT(Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL));
    ASSERT(NumSeg > 0);

    //
    // N.B. Windows software coalescing violates the RSC specification by
    // coalescing segments that have not had their checksums validated. XDP
    // ignores this contract violation for two reasons:
    //
    // 1. NICs used in XDP scenarios are expected to in fact validate checksums;
    //    typically only consumer NICs lack this feature.
    // 2. All production traffic should be encrypted, by L2 encapsulation or
    //    TCP/TLS. Cryptographic checksums perform much stronger validation than
    //    TCP checksum, so the weaker TCP checksum is practically redundant.
    //
    // This optimization implies RSC TCP segments forwarded by XDP will have
    // correct checksums even if the original checksums were invalid,
    // potentially introducing corruption.
    //

    if (MdlDataLength < sizeof(*Ethernet)) {
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
        goto Exit;
    }

    switch (Ethernet->Type) {
    case CONST_HTONS(ETHERNET_TYPE_IPV4):
    {
        IPV4_HEADER *Ipv4 = (IPV4_HEADER *)(Ethernet + 1);
        TcpOffset = sizeof(*Ethernet) + sizeof(*Ipv4);
        IpAddressLength = sizeof(Ipv4->SourceAddress);
        IpSrc = &Ipv4->SourceAddress;
        IpDst = &Ipv4->DestinationAddress;

        if (MdlDataLength < TcpOffset + sizeof(*Tcp)) {
            STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
            goto Exit;
        }

        IpPayloadLength = ntohs(Ipv4->TotalLength);
        if (IpPayloadLength < sizeof(*Ipv4)) {
            STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
            goto Exit;
        }
        IpPayloadLength -= sizeof(*Ipv4);
        Ipv4->TotalLength = 0;
        Ipv4->HeaderChecksum = 0;
        LsoInfo.LsoV2Transmit.IPVersion = NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4;
        break;
    }
    case CONST_HTONS(ETHERNET_TYPE_IPV6):
    {
        IPV6_HEADER *Ipv6 = (IPV6_HEADER *)(Ethernet + 1);
        TcpOffset = sizeof(*Ethernet) + sizeof(*Ipv6);
        IpAddressLength = sizeof(Ipv6->SourceAddress);
        IpSrc = &Ipv6->SourceAddress;
        IpDst = &Ipv6->DestinationAddress;

        if (MdlDataLength < TcpOffset + sizeof(*Tcp)) {
            STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
            goto Exit;
        }

        IpPayloadLength = ntohs(Ipv6->PayloadLength);
        Ipv6->PayloadLength = 0;
        LsoInfo.LsoV2Transmit.IPVersion = NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6;
        break;
    }
    default:
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
        goto Exit;
    }

    ASSERT(TcpOffset + sizeof(*Tcp) <= MdlDataLength);

    //
    // Truncate the frame based on the IP packet's total length field.
    //
    if (TcpOffset + (UINT32)IpPayloadLength > Nb->DataLength) {
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
        goto Exit;
    }
    ASSERT(Nb->DataLength >= TcpOffset + (UINT32)IpPayloadLength);
    TruncatedBytes = Nb->DataLength - (TcpOffset + (UINT32)IpPayloadLength);
    Nb->DataLength -= TruncatedBytes;
    MdlDataLength = min(Mdl->ByteCount - DataOffset, Nb->DataLength);

    Tcp = RTL_PTR_ADD(Ethernet, TcpOffset);
    TcpHdrLen = Tcp->th_len << 2;
    TcpTotalHdrLen = TcpOffset + TcpHdrLen;

    if (TcpHdrLen < sizeof(*Tcp) || MdlDataLength < TcpTotalHdrLen ||
        Nb->DataLength < TcpTotalHdrLen) {
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
        goto Exit;
    }

    ASSERT(Nb->DataLength >= TcpTotalHdrLen);
    TcpUserDataLen = Nb->DataLength - TcpTotalHdrLen;

    //
    // Pure ACKs are also valid RSC frames. To handle pure ACKs along with
    // regular segments while performing the MSS arithmetic, pad the user data
    // length to one for pure ACKs.
    //
    TcpUserDataLenOrPureAck = TcpUserDataLen | !TcpUserDataLen;
    ASSERT(
        (TcpUserDataLen == 0 && (TcpUserDataLenOrPureAck == 1)) ||
        (TcpUserDataLen == TcpUserDataLenOrPureAck));

    //
    // While RSC NICs are required to provide a valid number of segments in each
    // coalesced NBL, the XDP program can rewrite headers and cause the TCP
    // payload size to drop below the segment count, producing a contradiction.
    //
    if (TcpUserDataLenOrPureAck < NumSeg) {
        STAT_INC(&RxQueue->PcwStats, ForwardingFailuresRscInvalidHeaders);
        goto Exit;
    }

    RscMss = (TcpUserDataLenOrPureAck + NumSeg - 1) / NumSeg;
    ASSERT(RxQueue->Generic->Tx.Mtu > TcpTotalHdrLen);
    TxMss = RxQueue->Generic->Tx.Mtu - TcpTotalHdrLen;
    LsoMss = min(TxMss, RscMss);

    ASSERT(LsoMss > 0);

    LsoInfo.LsoV2Transmit.TcpHeaderOffset = TcpOffset;
    LsoInfo.LsoV2Transmit.MSS = LsoMss;
    Tcp->th_sum =
        XdpChecksumFold(
            XdpPartialChecksum(IpSrc, IpAddressLength) +
            XdpPartialChecksum(IpDst, IpAddressLength) +
            (IPPROTO_TCP << 8));

    TaskOffload = ReadPointerNoFence(&RxQueue->Generic->Filter->Offload.LowerEdge.TaskOffload);

    if (TaskOffload != NULL &&
        TcpUserDataLen <= TaskOffload->Lso.MaxOffloadSize &&
        (TcpUserDataLen + NumSeg - 1) / LsoMss >= TaskOffload->Lso.MinSegments) {
        NET_BUFFER_LIST *TxNbl;

        //
        // Fast path: the RSC NBL can be directly converted to a single LSO NBL.
        //

        TxNbl = XdpGenericRxAllocateTxCloneNbl(RxQueue);
        if (TxNbl == NULL) {
            goto Exit;
        }

        if (!XdpGenericRxCloneOrCopyTxNblData(RxQueue, Nbl, Nb, CanPend, TxNbl)) {
            XdpGenericRxReturnTxCloneNbl(RxQueue, TxNbl);
            goto Exit;
        }

        NET_BUFFER_LIST_INFO(TxNbl, TcpLargeSendNetBufferListInfo) = LsoInfo.Value;

        XdpGenericRxEnqueueTxNbl(RxQueue, TxList, Nbl, TxNbl);
    } else {
        //
        // Slow and careful path: the RSC NBL needs special handling.
        //

        XdpGenericRxSegmentRscToLso(
            RxQueue, TxList, Nbl, TaskOffload, CanPend, LsoInfo, TcpTotalHdrLen);
    }

Exit:

    Nb->DataLength += TruncatedBytes;

    return;
}

static
FORCEINLINE
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

    STAT_INC(&RxQueue->PcwStats, ForwardingNbsRequested);

    Nbl->FirstNetBuffer = Nb;
    Nbl->FirstNetBuffer->Next = NULL;

    if (!RxQueue->Flags.TxInspect) {
        if (NET_BUFFER_LIST_IS_TCP_RSC_SET(Nbl)) {
            XdpGenericRxConvertRscToLso(RxQueue, TxList, Nbl, CanPend);
            goto Exit;
        }
    }

    TxNbl = XdpGenericRxAllocateTxCloneNbl(RxQueue);
    if (TxNbl == NULL) {
        goto Exit;
    }

    if (!XdpGenericRxCloneOrCopyTxNblData(RxQueue, Nbl, Nb, CanPend, TxNbl)) {
        XdpGenericRxReturnTxCloneNbl(RxQueue, TxNbl);
        goto Exit;
    }

    XdpGenericRxEnqueueTxNbl(RxQueue, TxList, Nbl, TxNbl);

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
    _Out_ const XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    NTSTATUS Status;
    XDP_LWF_GENERIC *Generic = (XDP_LWF_GENERIC *)InterfaceContext;
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue = NULL;
    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue;
    const XDP_QUEUE_INFO *QueueInfo;
    const XDP_HOOK_ID *QueueHookId;
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
