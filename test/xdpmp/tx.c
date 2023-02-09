//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct {
    UINT64 LogicalAddress;
    UINT32 Length;
} TX_HW_DESCRIPTOR;

#define MP_NBL_GET_REF_COUNT(Nbl)       ((ULONG *)&((Nbl)->MiniportReserved[0]))
#define MP_NB_GET_OWNING_NBL(Nb)        ((NET_BUFFER_LIST **)&((Nb)->MiniportReserved[0]))
#define MP_NB_GET_NB_QUEUE_LINK(Nb)     ((NET_BUFFER **)&((Nb)->MiniportReserved[1]))

inline
UINT32
XdpRingCountInOrder(
    _In_ XDP_RING *Ring
    )
{
    return (Ring->ProducerIndex - Ring->InterfaceReserved);
}

UINT32
PostNbQueueToHw(
    _In_ ADAPTER_TX_QUEUE *Tq,
    _In_ UINT32 NetBufferCount,
    _Inout_ NET_BUFFER **NetBuffer
    )
{
    NET_BUFFER *Nb = *NetBuffer;
    UINT32 Count;
    UINT32 Head;
    KIRQL OldIrql;

    Count = HwRingBestEffortMpReserve(Tq->HwRing, NetBufferCount, &Head, &OldIrql);

    if (Count > 0) {
        for (UINT32 Index = 0; Index < Count; Index++) {
            TX_HW_DESCRIPTOR *HwDescriptor =
                HwRingGetElement(Tq->HwRing, (Head + Index) & Tq->HwRing->Mask);
            TX_SHADOW_DESCRIPTOR *ShadowDescriptor =
                &Tq->ShadowRing[(Head + Index) & Tq->HwRing->Mask];

            //
            // Real miniport would use logical address returned after building
            // SG list.
            //
            ULONG La = 0;

            HwDescriptor->LogicalAddress = (UINT64)La + (UINT64)Nb->DataOffset;
            HwDescriptor->Length = Nb->DataLength;

            ShadowDescriptor->Nb = Nb;
            ShadowDescriptor->Source = TxSourceNdis;

            Nb = *MP_NB_GET_NB_QUEUE_LINK(Nb);
        }

        HwRingMpCommit(Tq->HwRing, Count, Head, OldIrql);

        *NetBuffer = Nb;
    }

    return Count;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32
MpTransmitProcessCompletions(
    ADAPTER_TX_QUEUE *Tq,
    BOOLEAN XdpActive,
    UINT32 Quota,
    COUNTED_NBL_CHAIN *NblChain
    )
{
    XDP_RING *FrameRing = NULL;
    UINT32 PreviousXdpConsumerIndex = 0;
    UINT32 XdpFramesCompleted = 0;
    BOOLEAN RxTxCompleted = FALSE;

    if (XdpActive) {
        FrameRing = Tq->FrameRing;
        PreviousXdpConsumerIndex = FrameRing->ConsumerIndex;
    }

    //
    // Complete XDP and NBL TX from hardware.
    //
    while (Quota > 0) {
        UINT32 Count = min(Quota, HwRingConsPeek(Tq->HwRing));
        if (Count == 0) {
            break;
        }

        for (UINT32 Index = 0; Index < Count; Index++) {
            UINT32 MaskedIndex = (Tq->HwRing->ConsumerIndex + Index) & Tq->HwRing->Mask;
            TX_HW_DESCRIPTOR *HwDescriptor = HwRingGetElement(Tq->HwRing, MaskedIndex);
            TX_SHADOW_DESCRIPTOR *ShadowDescriptor = &Tq->ShadowRing[MaskedIndex];

            Tq->Stats.TxFrames++;
            Tq->Stats.TxBytes += HwDescriptor->Length;

            switch (ShadowDescriptor->Source) {

            case TxSourceNdis:
            {
                NET_BUFFER_LIST **OwningNbl = MP_NB_GET_OWNING_NBL(ShadowDescriptor->Nb);
                ULONG *OwningNblRefCount = MP_NBL_GET_REF_COUNT(*OwningNbl);

                if (--(*OwningNblRefCount) == 0) {
                    (*OwningNbl)->Status = NDIS_STATUS_SUCCESS;
                    CountedNblChainAppend(NblChain, *OwningNbl);
                    //
                    // The NDIS polling quota is expressed in terms of NBLs, not
                    // individual frames.
                    //
                    Quota--;
                }

                break;
            }

            case TxSourceXdpTx:
            {
                ASSERT(XdpActive);
                ASSERT((FrameRing->InterfaceReserved - FrameRing->ConsumerIndex) > 0);
#if DBG
                XDP_FRAME *Frame =
                    XdpRingGetElement(
                        FrameRing, FrameRing->ConsumerIndex & FrameRing->Mask);
                ASSERT(ShadowDescriptor->Frame == Frame);
#endif
                ++FrameRing->ConsumerIndex;
                Tq->XdpHwDescriptorsAvailable++;
                XdpFramesCompleted++;
                Quota--;

                break;
            }

            case TxSourceXdpRx:
            {
                MpReceiveCompleteRxTx(Tq->Rq, HwDescriptor->LogicalAddress);
                RxTxCompleted = TRUE;
                XdpFramesCompleted++;
                Quota--;
                break;
            }

            default:
                ASSERT(FALSE);
            }
        }

        if (XdpActive) {
            if (PreviousXdpConsumerIndex != FrameRing->ConsumerIndex) {
                XdpFlushTransmit(Tq->XdpTxQueue);
                PreviousXdpConsumerIndex = FrameRing->ConsumerIndex;
            }
        }

        HwRingConsPopElements(Tq->HwRing, Count);

        if (RxTxCompleted) {
            MpReceiveFlushRxTx(Tq->Rq);
        }
    }

    if (NblChain->Count > 0) {
        ExReleaseRundownProtectionCacheAwareEx(Tq->NblRundown, NblChain->Count);
    }

    return XdpFramesCompleted;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpTransmitRxTx(
    _In_ CONST ADAPTER_TX_QUEUE *Tq,
    _In_ UINT32 HwIndex,
    _In_ UINT64 LogicalAddress,
    _In_ UINT32 DataLength
    )
{
    TX_HW_DESCRIPTOR *HwDescriptor =
        HwRingGetElement(Tq->HwRing, HwIndex & Tq->HwRing->Mask);
    TX_SHADOW_DESCRIPTOR *ShadowDescriptor = &Tq->ShadowRing[(HwIndex) & Tq->HwRing->Mask];

    HwDescriptor->LogicalAddress = LogicalAddress;
    HwDescriptor->Length = DataLength;
    ShadowDescriptor->Source = TxSourceXdpRx;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32
MpTransmitProcessPosts(
    ADAPTER_TX_QUEUE *Tq,
    BOOLEAN XdpActive
    )
{
    KIRQL OldIrql;
    UINT32 Count;
    UINT32 XdpFramesTransmitted = 0;

    //
    // Post XDP TX to hardware.
    //
    if (XdpActive) {
        XDP_RING *FrameRing = Tq->FrameRing;
        UINT32 Head;

        while (TRUE) {
            if (Tq->XdpHwDescriptorsAvailable == 0) {
                break;
            }

            if (XdpRingCountInOrder(FrameRing) == 0) {
                XdpFlushTransmit(Tq->XdpTxQueue);

                if (XdpRingCountInOrder(FrameRing) == 0) {
                    //
                    // The XDP transmit queue is blocked.
                    //
                    break;
                }
            }

            Count = min(Tq->XdpHwDescriptorsAvailable, XdpRingCountInOrder(FrameRing));
            Count = HwRingBestEffortMpReserve(Tq->HwRing, Count, &Head, &OldIrql);
            if (Count == 0) {
                break;
            }

            for (UINT32 Index = 0; Index < Count; Index++) {
                XDP_FRAME *Frame =
                    XdpRingGetElement(FrameRing, FrameRing->InterfaceReserved++ & FrameRing->Mask);
                XDP_BUFFER_VIRTUAL_ADDRESS *Va =
                    XdpGetVirtualAddressExtension(&Frame->Buffer, &Tq->BufferVaExtension);
                TX_HW_DESCRIPTOR *HwDescriptor =
                    HwRingGetElement(Tq->HwRing, (Head + Index) & Tq->HwRing->Mask);
                TX_SHADOW_DESCRIPTOR *ShadowDescriptor =
                    &Tq->ShadowRing[(Head + Index) & Tq->HwRing->Mask];

                //
                // XDPMP is a software device not capable of DMA, so just use
                // the VA here.
                //
                HwDescriptor->LogicalAddress = (UINT64)(Va->VirtualAddress) + Frame->Buffer.DataOffset;
                HwDescriptor->Length = Frame->Buffer.DataLength;
#if DBG
                ShadowDescriptor->Frame = Frame;
#endif
                ShadowDescriptor->Source = TxSourceXdpTx;
            }

            HwRingMpCommit(Tq->HwRing, Count, Head, OldIrql);
            Tq->XdpHwDescriptorsAvailable -= Count;
            XdpFramesTransmitted += Count;
        }
    }

    //
    // Post NBL TX to HW.
    //
    if (Tq->NbQueueCount > 0) {
        KeAcquireSpinLock(&Tq->NbQueueLock, &OldIrql);

        Count = PostNbQueueToHw(Tq, Tq->NbQueueCount, &Tq->NbQueueHead);
        Tq->NbQueueCount -= Count;
        if (Tq->NbQueueHead == NULL) {
            ASSERT(Tq->NbQueueCount == 0);
            Tq->NbQueueTail = &Tq->NbQueueHead;
        }

        KeReleaseSpinLock(&Tq->NbQueueLock, OldIrql);
    }

    return XdpFramesTransmitted;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpTransmit(
    _In_ ADAPTER_TX_QUEUE *Tq,
    _Inout_ NDIS_POLL_TRANSMIT_DATA *Poll,
    _Inout_ XDP_POLL_TRANSMIT_DATA *XdpPoll
    )
{
    BOOLEAN XdpActive = FALSE;
    XDP_QUEUE_STATE XdpState = ReadUInt32Acquire((UINT32 *)&Tq->XdpState);
    COUNTED_NBL_CHAIN NblChain;

    CountedNblChainInitialize(&NblChain);

    //
    // First, check if the XDP state needs to be updated.
    //
    if (XdpState == XDP_STATE_DELETE_PENDING) {
        Tq->XdpState = XDP_STATE_INACTIVE;
        KeSetEvent(Tq->DeleteComplete, 0, FALSE);
    } else if (XdpState == XDP_STATE_ACTIVE) {
        XdpActive = TRUE;
        if (Tq->NeedFlush) {
            Tq->NeedFlush = FALSE;
            XdpFlushTransmit(Tq->XdpTxQueue);
        }
    }

    XdpPoll->FramesCompleted +=
        MpTransmitProcessCompletions(Tq, XdpActive, Poll->MaxNblsToComplete, &NblChain);
    XdpPoll->FramesTransmitted += MpTransmitProcessPosts(Tq, XdpActive);

    //
    // If NBLs were completed, return those to NDIS.
    //
    if (NblChain.Count > 0) {
        Poll->CompletedNblChain = NblChain.Head;
        Poll->NumberOfCompletedNbls = NblChain.Count;
    }
}

static
ADAPTER_QUEUE *
MpSendGetRssQueue(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    UINT32 RssHash = NET_BUFFER_LIST_GET_HASH_VALUE(NetBufferList);
    UINT32 QueueId = Adapter->IndirectionTable[RssHash & Adapter->IndirectionMask];

    return &Adapter->RssQueues[QueueId];
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(MINIPORT_SEND_NET_BUFFER_LISTS)
VOID
MpSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NET_BUFFER_LIST *NetBufferList,
    _In_ ULONG PortNumber,
    _In_ ULONG SendFlags
    )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    ADAPTER_QUEUE *AdapterQueue;
    ADAPTER_TX_QUEUE *Tq;
    NET_BUFFER_LIST *NetBufferListHead = NetBufferList;
    NET_BUFFER *LocalNbQueueHead = NULL;
    NET_BUFFER **LocalNbQueueTail = &LocalNbQueueHead;
    UINT32 NbCount = 0;
    UINT32 NblCount = 0;
    UINT32 PostedNbCount = 0;
    BOOLEAN RequestPoll = FALSE;

    UNREFERENCED_PARAMETER(PortNumber);

    //
    // For simplicity, assume all NBLs are directed to the same RSS queue.
    //
    AdapterQueue = MpSendGetRssQueue(Adapter, NetBufferList);
    Tq = &AdapterQueue->Tq;

    //
    // Create an NB chain that spans all NBLs.
    //
    while (NetBufferList != NULL) {
        NET_BUFFER *NetBuffer = NetBufferList->FirstNetBuffer;
        ULONG *OwningNblRefCount = MP_NBL_GET_REF_COUNT(NetBufferList);
        *OwningNblRefCount = 0;

        while (NetBuffer != NULL) {
            NET_BUFFER_LIST **OwningNbl = MP_NB_GET_OWNING_NBL(NetBuffer);
            *OwningNbl = NetBufferList;
            *LocalNbQueueTail = NetBuffer;
            LocalNbQueueTail = MP_NB_GET_NB_QUEUE_LINK(NetBuffer);
            ++(*OwningNblRefCount);
            NetBuffer = NetBuffer->Next;
        }

        ++NblCount;
        NbCount += *OwningNblRefCount;
        NetBufferList = NetBufferList->Next;
    }
    *LocalNbQueueTail = NULL;

    //
    // Acquire rundown protection.
    //
    if (!ExAcquireRundownProtectionCacheAwareEx(Tq->NblRundown, NblCount)) {
        NetBufferList = NetBufferListHead;
        while (NetBufferList != NULL) {
            NetBufferList->Status = NDIS_STATUS_PAUSED;
            NetBufferList = NetBufferList->Next;
        }
        NdisMSendNetBufferListsComplete(
            Adapter->MiniportHandle,
            NetBufferListHead,
            NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ?
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        InterlockedAdd64((LONG64 *)&Tq->Stats.TxDrops, NblCount);
        return;
    }

    //
    // Attempt to post NBs to the HW ring.
    //
    if (Tq->NbQueueCount == 0) {
        PostedNbCount = PostNbQueueToHw(Tq, NbCount, &LocalNbQueueHead);
        if (PostedNbCount > 0) {
            //
            // Trigger an interrupt (if armed) so each TX is completed ASAP.
            // This is required for a software driver only.
            //
            MpRateSimInterrupt(AdapterQueue);
        }
    }

    //
    // Queue the rest of the NBs that we were unable to post.
    //
    if (LocalNbQueueHead != NULL) {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Tq->NbQueueLock, &OldIrql);
        *Tq->NbQueueTail = LocalNbQueueHead;
        Tq->NbQueueTail = LocalNbQueueTail;
        Tq->NbQueueCount += (NbCount - PostedNbCount);
        KeReleaseSpinLock(&Tq->NbQueueLock, OldIrql);

        RequestPoll = TRUE;
    }

    if (RequestPoll) {
        Adapter->PollDispatch.RequestPoll(AdapterQueue->NdisPollHandle, 0);
    }
}

VOID
MpCleanupTransmitQueue(
    _Inout_ ADAPTER_TX_QUEUE *Tq
    )
{
    if (Tq->HwRing != NULL) {
        HwRingFreeRing(Tq->HwRing);
        Tq->HwRing = NULL;
    }

    if (Tq->ShadowRing != NULL) {
        ExFreePoolWithTag(Tq->ShadowRing, POOLTAG_TX);
        Tq->ShadowRing = NULL;
    }
}

NDIS_STATUS
MpInitializeTransmitQueue(
    _Inout_ ADAPTER_TX_QUEUE *Tq,
    _In_ CONST ADAPTER_QUEUE *RssQueue
    )
{
    NDIS_STATUS Status;
    CONST ADAPTER_CONTEXT *Adapter = RssQueue->Adapter;

    Tq->NbQueueCount = 0;
    Tq->NbQueueHead = NULL;
    Tq->NbQueueTail = &Tq->NbQueueHead;
    Tq->NblRundown = Adapter->NblRundown;
    Tq->Rq = &RssQueue->Rq;
    KeInitializeSpinLock(&Tq->NbQueueLock);

    Tq->XdpHwDescriptorsAvailable = Adapter->TxRingSize * Adapter->TxXdpQosPct / 100;
    if (Tq->XdpHwDescriptorsAvailable == 0) {
        Tq->XdpHwDescriptorsAvailable = 1;
    }

    Status =
        HwRingAllocateRing(
            sizeof(TX_HW_DESCRIPTOR), Adapter->TxRingSize,
            __alignof(TX_HW_DESCRIPTOR), &Tq->HwRing);
    if (Status != STATUS_SUCCESS) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Tq->ShadowRing =
        ExAllocatePoolZero(
            NonPagedPoolNxCacheAligned,
            (sizeof(TX_SHADOW_DESCRIPTOR) * Adapter->TxRingSize),
            POOLTAG_TX);
    if (Tq->ShadowRing == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

Exit:

    return Status;
}

static CONST XDP_INTERFACE_TX_QUEUE_DISPATCH MpXdpTxDispatch = {
    MpXdpNotify,
};

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpCreateTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)InterfaceContext;
    CONST XDP_QUEUE_INFO *QueueInfo;
    ADAPTER_QUEUE *AdapterQueue;
    ADAPTER_TX_QUEUE *Tq;
    XDP_TX_CAPABILITIES TxCapabilities;
    XDP_POLL_INFO PollInfo;

    QueueInfo = XdpTxQueueGetTargetQueueInfo(Config);

    if (QueueInfo->QueueType != XDP_QUEUE_TYPE_DEFAULT_RSS) {
        return STATUS_NOT_SUPPORTED;
    }

    if (QueueInfo->QueueId >= Adapter->NumRssQueues) {
        return STATUS_NOT_FOUND;
    }

    AdapterQueue = &Adapter->RssQueues[QueueInfo->QueueId];
    Tq = &AdapterQueue->Tq;

    ASSERT(Tq->XdpState == XDP_STATE_INACTIVE);

    XdpTxQueueRegisterExtensionVersion(Config, &MpSupportedXdpExtensions.VirtualAddress);

    XdpInitializeTxCapabilitiesSystemVa(&TxCapabilities);

    TxCapabilities.TransmitFrameCountHint = (UINT16)(Tq->HwRing->Mask + 1);

    XdpTxQueueSetCapabilities(Config, &TxCapabilities);

    XdpInitializeExclusivePollInfo(&PollInfo, AdapterQueue->NdisPollHandle);
    XdpTxQueueSetPollInfo(Config, &PollInfo);

    *InterfaceTxQueue = (XDP_INTERFACE_HANDLE)AdapterQueue;
    *InterfaceTxQueueDispatch = &MpXdpTxDispatch;

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpActivateTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    ADAPTER_QUEUE *AdapterQueue;
    ADAPTER_TX_QUEUE *Tq;

    AdapterQueue = (ADAPTER_QUEUE *)InterfaceTxQueue;
    Tq = &AdapterQueue->Tq;

    ASSERT(Tq->XdpState == XDP_STATE_INACTIVE);

    Tq->XdpTxQueue = XdpTxQueue;
    Tq->FrameRing = XdpTxQueueGetFrameRing(Config);

    XdpTxQueueGetExtension(
        Config, &MpSupportedXdpExtensions.VirtualAddress, &Tq->BufferVaExtension);

    WriteUInt32Release((UINT32 *)&Tq->XdpState, XDP_STATE_ACTIVE);

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
MpXdpDeleteTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    )
{
    ADAPTER_QUEUE *AdapterQueue;
    ADAPTER_TX_QUEUE *Tq;
    KEVENT DeleteComplete;

    AdapterQueue = (ADAPTER_QUEUE *)InterfaceTxQueue;
    Tq = &AdapterQueue->Tq;

    if (Tq->XdpState == XDP_STATE_INACTIVE) {
        //
        // XDP is allowed to delete a created but inactive queue.
        //
        return;
    }

    KeInitializeEvent(&DeleteComplete, NotificationEvent, FALSE);
    Tq->DeleteComplete = &DeleteComplete;

    //
    // Ensure the state is changed with release semantics, then trigger
    // an NDIS poll to perform the actual delete work within the poll EC.
    //
    ASSERT(Tq->XdpState == XDP_STATE_ACTIVE);
    WriteUInt32Release((UINT32 *)&Tq->XdpState, XDP_STATE_DELETE_PENDING);
    AdapterQueue->Adapter->PollDispatch.RequestPoll(AdapterQueue->NdisPollHandle, 0);

    KeWaitForSingleObject(&DeleteComplete, Executive, KernelMode, FALSE, NULL);
    ASSERT(Tq->XdpState == XDP_STATE_INACTIVE);

    Tq->DeleteComplete = NULL;
    Tq->XdpTxQueue = NULL;
    Tq->FrameRing = NULL;
}
