//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "rx.tmh"

static
VOID
MpHwReceiveReturn(
    ADAPTER_RX_QUEUE *Rq,
    UINT32 *HwRxDescriptors,
    UINT32 *Count
    )
{
    KIRQL OldIrql;
    UINT32 Head;

    HwRingBoundedMpReserve(Rq->HwRing, *Count, &Head, &OldIrql);

    for (UINT32 Index = 0; Index < *Count; Index++) {
        UINT32 *HwReturnDescriptor;

        HwReturnDescriptor =
            HwRingGetElement(Rq->HwRing, (Head + Index) & Rq->HwRing->Mask);
        *HwReturnDescriptor = HwRxDescriptors[Index];
    }

    HwRingMpCommit(Rq->HwRing, *Count, Head, OldIrql);

    *Count = 0;
}

static
VOID
MpReceiveRecycle(
    ADAPTER_RX_QUEUE *Rq,
    UINT32 HwRxDescriptor
    )
{
    ASSERT(Rq->RecycleIndex < Rq->NumBuffers);
    Rq->RecycleArray[Rq->RecycleIndex++] = HwRxDescriptor;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpReceiveRecycleFlush(
    _In_ ADAPTER_RX_QUEUE *Rq
    )
{
    ASSERT(Rq->RecycleIndex > 0);
    MpHwReceiveReturn(Rq, Rq->RecycleArray, &Rq->RecycleIndex);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpReceiveCompleteRxTx(
    _In_ CONST ADAPTER_RX_QUEUE *Rq,
    _In_ UINT64 LogicalAddress
    )
{
    UINT32 HwRxDescriptor;

    //
    // Calculate the original start of the RX buffer from the TX buffer, which
    // may have an additional offset.
    //
    HwRxDescriptor = ((UCHAR *)LogicalAddress - Rq->BufferArray) & Rq->BufferMask;
    MpReceiveRecycle((ADAPTER_RX_QUEUE *)Rq, HwRxDescriptor);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpReceiveFlushRxTx(
    _In_ CONST ADAPTER_RX_QUEUE *Rq
    )
{
    MpReceiveRecycleFlush((ADAPTER_RX_QUEUE *)Rq);
}

static
VOID
MpNdisReceive(
    ADAPTER_RX_QUEUE *Rq,
    UINT32 HwRxDescriptor,
    UINT32 DataOffset,
    UINT32 DataLength,
    COUNTED_NBL_CHAIN *NblChain
    )
{
    UINT32 DescriptorIndex = HwRxDescriptor / Rq->BufferLength;
    NET_BUFFER_LIST *NetBufferList = Rq->NblArray[DescriptorIndex];
    NET_BUFFER *NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);

    NET_BUFFER_DATA_OFFSET(NetBuffer) = DataOffset;
    NET_BUFFER_DATA_LENGTH(NetBuffer) = DataLength;

    NET_BUFFER_LIST_SET_HASH_VALUE(NetBufferList, Rq->RssHash);
    NET_BUFFER_LIST_SET_HASH_TYPE(NetBufferList, NDIS_HASH_IPV4);

    CountedNblChainAppend(NblChain, NetBufferList);

    Rq->Stats.RxFrames++;
    Rq->Stats.RxBytes += DataLength;
}

static
UINT32
MpReceiveProcessBatch(
    _In_ ADAPTER_RX_QUEUE *Rq,
    _Inout_ UINT32 *StartIndex,
    _Inout_ COUNTED_NBL_CHAIN *NblChain
    )
{
    XDP_RING *FrameRing = Rq->FrameRing;
    XDP_FRAME *Frame;
    XDP_BUFFER *Buffer;
    XDP_FRAME_RX_ACTION *Action;
    XDP_BUFFER_VIRTUAL_ADDRESS *Va;
    UINT32 HwRxDescriptor;
    UINT32 XdpAbsorbed = 0;

    //
    // Inspect a batch of frames.
    //
    XdpReceive(Rq->XdpRxQueue);

    //
    // Perform action for each frame in the batch.
    //
    while (*StartIndex != FrameRing->ProducerIndex) {
        UINT32 FrameRingIndex = (*StartIndex)++ & FrameRing->Mask;
        Frame = XdpRingGetElement(FrameRing, FrameRingIndex);
        Buffer = &Frame->Buffer;
        Action = XdpGetRxActionExtension(Frame, &Rq->RxActionExtension);
        Va = XdpGetVirtualAddressExtension(Buffer, &Rq->BufferVaExtension);
        HwRxDescriptor = (UINT32)(Va->VirtualAddress - Rq->BufferArray);

        switch (Action->RxAction) {
        case XDP_RX_ACTION_PASS:
            //
            // Pass the frame onto the regular NDIS receive path.
            //
            MpNdisReceive(Rq, HwRxDescriptor, Buffer->DataOffset, Buffer->DataLength, NblChain);
            break;

        case XDP_RX_ACTION_DROP:
            //
            // Drop the frame. For statistical purposes, treat the frame as
            // if it were successfully indicated to the local NDIS stack.
            //
            XdpAbsorbed++;
            Rq->Stats.RxFrames++;
            Rq->Stats.RxBytes += Buffer->DataLength;
            MpReceiveRecycle(Rq, HwRxDescriptor);
            break;

        case XDP_RX_ACTION_TX:
            Rq->Stats.RxFrames++;
            Rq->Stats.RxBytes += Buffer->DataLength;
            XdpAbsorbed++;
            Rq->RxTxArray[Rq->RxTxIndex++] = FrameRingIndex;
            break;

        default:
            ASSERT(FALSE);
            break;
        }
    }

    if (Rq->RxTxIndex > 0) {
        KIRQL OldIrql;
        UINT32 Count;
        UINT32 Head;

        Count = HwRingBestEffortMpReserve(Rq->Tq->HwRing, Rq->RxTxIndex, &Head, &OldIrql);

        //
        // Transmit as many of the RX->TX frames as possible to the paired TX
        // queue.
        //
        if (Count > 0) {
            for (UINT32 Index = 0; Index < Count; Index++) {
                Frame = XdpRingGetElement(FrameRing, Rq->RxTxArray[Index]);
                Va = XdpGetVirtualAddressExtension(&Frame->Buffer, &Rq->BufferVaExtension);

                //
                // XDPMP is a software device not capable of DMA, so just use
                // the VA here.
                //
                MpTransmitRxTx(
                    Rq->Tq, Head + Index, (UINT64)(Va->VirtualAddress) + Frame->Buffer.DataOffset,
                    Frame->Buffer.DataLength);
            }

            HwRingMpCommit(Rq->Tq->HwRing, Count, Head, OldIrql);
        }

        //
        // Drop the remainder of the RX->TX frames.
        //
        if (Count < Rq->RxTxIndex) {
            for (UINT32 Index = Count; Index < Rq->RxTxIndex; Index++) {
                Frame = XdpRingGetElement(FrameRing, Rq->RxTxArray[Index]);
                Buffer = &Frame->Buffer;
                Action = XdpGetRxActionExtension(Frame, &Rq->RxActionExtension);
                Va = XdpGetVirtualAddressExtension(Buffer, &Rq->BufferVaExtension);
                HwRxDescriptor = (UINT32)(Va->VirtualAddress - Rq->BufferArray);

                MpReceiveRecycle(Rq, HwRxDescriptor);
            }
        }

        Rq->RxTxIndex = 0;
    }

    return XdpAbsorbed;
}

static
UINT32
MpReceiveUseXdpMultiFrameApi(
    ADAPTER_RX_QUEUE *Rq,
    UINT32 FrameQuota,
    COUNTED_NBL_CHAIN *NblChain
    )
{
    UINT32 *HwRxDescriptor;
    UINT32 XdpAbsorbed = 0;

    if (ReadUInt32Acquire((UINT32 *)&Rq->XdpState) == XDP_STATE_ACTIVE) {
        XDP_RING *FrameRing = Rq->FrameRing;
        UINT32 StartIndex = FrameRing->ProducerIndex;

        while (FrameQuota-- > 0 && HwRingConsPeek(Rq->HwRing) > 0) {
            XDP_FRAME *Frame;
            XDP_BUFFER_VIRTUAL_ADDRESS *Va;

            HwRxDescriptor = HwRingConsPopElement(Rq->HwRing);

            if (Rq->PatternLength > 0) {
                //
                // Reinitialize packet content. This is disabled by default, but
                // needs to be enabled in scenarios where the upper protocol
                // rewrites packets.
                //
                UCHAR *Pkt = Rq->BufferArray + *HwRxDescriptor;
                RtlCopyMemory(Pkt, Rq->PatternBuffer, Rq->PatternLength);
            }

            Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex++ & FrameRing->Mask);

            Frame->Buffer.DataLength = Rq->DataLength;
            Frame->Buffer.BufferLength = Rq->BufferLength;
            Frame->Buffer.DataOffset = 0;

            Va = XdpGetVirtualAddressExtension(&Frame->Buffer, &Rq->BufferVaExtension);
            Va->VirtualAddress = Rq->BufferArray + *HwRxDescriptor;

            if (XdpRingFree(FrameRing) == 0) {
                XdpAbsorbed += MpReceiveProcessBatch(Rq, &StartIndex, NblChain);
            }
        }

        if (XdpRingCount(FrameRing) > 0) {
            XdpAbsorbed += MpReceiveProcessBatch(Rq, &StartIndex, NblChain);
        }

        if (Rq->NeedFlush) {
            Rq->NeedFlush = FALSE;
            XdpFlushReceive(Rq->XdpRxQueue);
        }
    } else {
        while (FrameQuota-- > 0 && HwRingConsPeek(Rq->HwRing) > 0) {
            HwRxDescriptor = HwRingConsPopElement(Rq->HwRing);

            if (Rq->PatternLength > 0) {
                //
                // Reinitialize packet content. This is disabled by default, but
                // needs to be enabled in scenarios where the upper protocol
                // rewrites packets.
                //
                UCHAR *Pkt = Rq->BufferArray + *HwRxDescriptor;
                RtlCopyMemory(Pkt, Rq->PatternBuffer, Rq->PatternLength);
            }

            MpNdisReceive(Rq, *HwRxDescriptor, 0, Rq->DataLength, NblChain);
        }
    }

    return XdpAbsorbed;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpReceive(
    _In_ ADAPTER_RX_QUEUE *Rq,
    _Inout_ NDIS_POLL_RECEIVE_DATA *Poll,
    _Inout_ XDP_POLL_RECEIVE_DATA *XdpPoll
    )
{
    COUNTED_NBL_CHAIN NblChain;

    CountedNblChainInitialize(&NblChain);

    ASSERT(Rq->RecycleIndex == 0);
    ASSERT(Rq->RxTxIndex == 0);

    //
    // First, check if the XDP state needs to be updated.
    //
    if (ReadUInt32Acquire((UINT32 *)&Rq->XdpState) == XDP_STATE_DELETE_PENDING) {
        Rq->XdpState = XDP_STATE_INACTIVE;
        KeSetEvent(Rq->DeleteComplete, 0, FALSE);
    }

    XdpPoll->FramesAbsorbed +=
        MpReceiveUseXdpMultiFrameApi(Rq, Poll->MaxNblsToIndicate, &NblChain);

    //
    // If NBLs were created, return those to NDIS, as long as the adapter is not
    // pausing/paused.
    //
    if (NblChain.Count > 0) {
        if (ExAcquireRundownProtectionCacheAwareEx(Rq->NblRundown, NblChain.Count)) {
            Poll->IndicatedNblChain = NblChain.Head;
            Poll->NumberOfIndicatedNbls = NblChain.Count;
        } else {
            while (NblChain.Head != NULL) {
                MpReceiveRecycle(Rq, (UINT32)(ULONG_PTR)NblChain.Head->MiniportReserved[1]);
                NblChain.Head = NblChain.Head->Next;
            }
        }
    }

    //
    // Return recycled RX descriptors to the hardware RX ring.
    //
    if (Rq->RecycleIndex > 0) {
        MpReceiveRecycleFlush(Rq);
    }
}

VOID
MpReturnNetBufferLists(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NET_BUFFER_LIST *NetBufferLists,
   _In_ ULONG ReturnFlags
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    ADAPTER_RX_QUEUE *BatchRq = NULL;
    UINT32 HwDescriptors[32];
    UINT32 HwDescriptorCount = 0;
    UINT32 NblCount = 0;

    UNREFERENCED_PARAMETER(ReturnFlags);

    //
    // Build per-RQ descriptor arrays from the NBL chain, flush those to the
    // hardware RX ring, and then release the rundown reference.
    //

    while (NetBufferLists != NULL) {
        ADAPTER_RX_QUEUE *Rq = NetBufferLists->MiniportReserved[0];

        NblCount++;

        //
        // This NBL belongs to a different RQ, so flush the existing batch and
        // restart.
        //
        if (Rq != BatchRq) {
            if (BatchRq != NULL) {
                MpHwReceiveReturn(BatchRq, HwDescriptors, &HwDescriptorCount);
            }
            BatchRq = Rq;
        }

        HwDescriptors[HwDescriptorCount] = (UINT32)(ULONG_PTR)NetBufferLists->MiniportReserved[1];

        if (++HwDescriptorCount == RTL_NUMBER_OF(HwDescriptors)) {
            MpHwReceiveReturn(BatchRq, HwDescriptors, &HwDescriptorCount);
        }

        NetBufferLists = NetBufferLists->Next;
    }

    if (HwDescriptorCount > 0) {
        MpHwReceiveReturn(BatchRq, HwDescriptors, &HwDescriptorCount);
    }

    ExReleaseRundownProtectionCacheAwareEx(Adapter->NblRundown, NblCount);
}

VOID
MpCleanupReceiveQueue(
    _Inout_ ADAPTER_RX_QUEUE *Rq
    )
{
    if (Rq->NblArray != NULL) {
        for (UINT32 Index = 0; Index < Rq->NumBuffers; Index++) {
#pragma warning(suppress:6001) // Rq->NblArray is initialized.
            if (Rq->NblArray[Index] != NULL) {
                NdisFreeNetBufferList(Rq->NblArray[Index]);
            }
        }

        ExFreePoolWithTag(Rq->NblArray, POOLTAG_RXBUFFER);
        Rq->NblArray = NULL;
    }

    if (Rq->RxTxArray != NULL) {
        ExFreePoolWithTag(Rq->RxTxArray, POOLTAG_RXBUFFER);
        Rq->RxTxArray = NULL;
    }

    if (Rq->RecycleArray != NULL) {
        ExFreePoolWithTag(Rq->RecycleArray, POOLTAG_RXBUFFER);
        Rq->RecycleArray = NULL;
    }

    if (Rq->HwRing != NULL) {
        HwRingFreeRing(Rq->HwRing);
        Rq->HwRing = NULL;
    }

    if (Rq->BufferArray != NULL) {
        ExFreePoolWithTag(Rq->BufferArray, POOLTAG_RXBUFFER);
        Rq->BufferArray = NULL;
    }
}

NDIS_STATUS
MpInitializeReceiveQueue(
    _Inout_ ADAPTER_RX_QUEUE *Rq,
    _In_ CONST ADAPTER_QUEUE *RssQueue
    )
{
    NDIS_STATUS Status;
    CONST ADAPTER_CONTEXT *Adapter = RssQueue->Adapter;
    const UINT32 PatternLength = min(Adapter->RxDataLength, Adapter->RxPatternLength);

    TraceEnter(TRACE_CONTROL, "NdisMiniportHandle=%p", Adapter->MiniportHandle);

    Rq->NumBuffers = Adapter->NumRxBuffers;
    Rq->BufferLength = Adapter->RxBufferLength;
    Rq->BufferMask = ~(Rq->BufferLength - 1);
    Rq->DataLength = Adapter->RxDataLength;
    Rq->NblRundown = Adapter->NblRundown;
    Rq->Tq = &RssQueue->Tq;

    Rq->BufferArray =
        ExAllocatePoolZero(
            NonPagedPoolNx, (SIZE_T)Rq->NumBuffers * (SIZE_T)Rq->BufferLength, POOLTAG_RXBUFFER);
    if (Rq->BufferArray == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Status =
        HwRingAllocateRing(
            sizeof(UINT32), Rq->NumBuffers, __alignof(UINT32), &Rq->HwRing);
    if (Status != STATUS_SUCCESS) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Rq->RecycleArray =
        ExAllocatePoolZero(
            NonPagedPoolNx, Rq->NumBuffers * sizeof(*Rq->RecycleArray), POOLTAG_RXBUFFER);
    if (Rq->RecycleArray == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Rq->RxTxArray =
        ExAllocatePoolZero(
            NonPagedPoolNx, Rq->NumBuffers * sizeof(*Rq->RxTxArray), POOLTAG_RXBUFFER);
    if (Rq->RxTxArray == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Rq->NblArray =
        ExAllocatePoolZero(
            NonPagedPoolNx, Rq->NumBuffers * sizeof(*Rq->NblArray), POOLTAG_RXBUFFER);
    if (Rq->NblArray == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Rq->PatternBuffer = Adapter->RxPattern;
    Rq->PatternLength = Adapter->RxPatternCopy ? PatternLength : 0;

    for (UINT32 i = 0; i < Rq->NumBuffers; i++) {
        UINT32 *Descriptor = HwRingGetElement(Rq->HwRing, i & Rq->HwRing->Mask);
        NET_BUFFER_LIST *NetBufferList;
        NET_BUFFER *NetBuffer;
        MDL *Mdl;
        NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *CsoInfo;

        *Descriptor = i * Rq->BufferLength;

#pragma warning(push) // i is always less than Rq->NumBuffers
#pragma warning(disable:6385)
#pragma warning(disable:6386)
        Rq->NblArray[i] =
            NdisAllocateNetBufferList(Adapter->RxNblPool, (USHORT)Adapter->MdlSize, 0);
        if (Rq->NblArray[i] == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        NetBufferList = Rq->NblArray[i];
#pragma warning(pop)
        NetBufferList->SourceHandle = Adapter->MiniportHandle;
        NetBufferList->MiniportReserved[0] = (VOID *)Rq;
        NetBufferList->MiniportReserved[1] = (VOID *)*Descriptor;

        Mdl = (MDL *)NET_BUFFER_LIST_CONTEXT_DATA_START(NetBufferList);
        MmInitializeMdl(Mdl, Rq->BufferArray + *Descriptor, Adapter->RxBufferLength);
        MmBuildMdlForNonPagedPool(Mdl);
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
        NET_BUFFER_FIRST_MDL(NetBuffer) = Mdl;
        NET_BUFFER_CURRENT_MDL(NetBuffer) = Mdl;

        NET_BUFFER_LIST_SET_HASH_FUNCTION(NetBufferList, NdisHashFunctionToeplitz);

        //
        // Declare all checksum validation has been offloaded. Implementation
        // detail: the TCPIP stack will examine each succeeded bit only if it
        // matches the protocol in the header.
        //
        CsoInfo = (NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO *)
            &NET_BUFFER_LIST_INFO(NetBufferList, TcpIpChecksumNetBufferListInfo);
        CsoInfo->Receive.IpChecksumSucceeded = TRUE;
        CsoInfo->Receive.TcpChecksumSucceeded = TRUE;
        CsoInfo->Receive.UdpChecksumSucceeded = TRUE;

        //
        // Initialize packet content.
        //
        if (PatternLength > 0) {
            UCHAR *Pkt = Rq->BufferArray + *Descriptor;
            RtlCopyMemory(Pkt, Rq->PatternBuffer, PatternLength);
        }

        MpReceiveRecycle(Rq, *Descriptor);
    }

    MpReceiveRecycleFlush(Rq);

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static CONST XDP_INTERFACE_RX_QUEUE_DISPATCH MpXdpRxDispatch = {
    MpXdpNotify,
};

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpCreateRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)InterfaceContext;
    CONST XDP_QUEUE_INFO *QueueInfo;
    ADAPTER_QUEUE *AdapterQueue;
    ADAPTER_RX_QUEUE *Rq;
    XDP_RX_CAPABILITIES RxCapabilities;
    XDP_POLL_INFO PollInfo;

    QueueInfo = XdpRxQueueGetTargetQueueInfo(Config);

    if (QueueInfo->QueueType != XDP_QUEUE_TYPE_DEFAULT_RSS) {
        return STATUS_NOT_SUPPORTED;
    }

    if (QueueInfo->QueueId >= Adapter->NumRssQueues) {
        return STATUS_NOT_FOUND;
    }

    AdapterQueue = &Adapter->RssQueues[QueueInfo->QueueId];
    Rq = &AdapterQueue->Rq;
    ASSERT(Rq->XdpState == XDP_STATE_INACTIVE);

    XdpRxQueueRegisterExtensionVersion(Config, &MpSupportedXdpExtensions.VirtualAddress);

    XdpRxQueueRegisterExtensionVersion(Config, &MpSupportedXdpExtensions.RxAction);

    XdpInitializeRxCapabilitiesDriverVa(&RxCapabilities);
    RxCapabilities.TxActionSupported = TRUE;
    XdpRxQueueSetCapabilities(Config, &RxCapabilities);

    XdpInitializeExclusivePollInfo(&PollInfo, AdapterQueue->NdisPollHandle);
    XdpRxQueueSetPollInfo(Config, &PollInfo);

    *InterfaceRxQueue = (XDP_INTERFACE_HANDLE)AdapterQueue;
    *InterfaceRxQueueDispatch = &MpXdpRxDispatch;

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpActivateRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    ADAPTER_QUEUE *AdapterQueue = (ADAPTER_QUEUE *)InterfaceRxQueue;
    ADAPTER_RX_QUEUE *Rq = &AdapterQueue->Rq;

    ASSERT(Rq->XdpState == XDP_STATE_INACTIVE);

    Rq->XdpRxQueue = XdpRxQueue;
    Rq->FrameRing = XdpRxQueueGetFrameRing(Config);
    Rq->NeedFlush = FALSE;
    Rq->DeleteComplete = NULL;

    ASSERT(XdpRxQueueIsVirtualAddressEnabled(Config));
    XdpRxQueueGetExtension(
        Config, &MpSupportedXdpExtensions.VirtualAddress, &Rq->BufferVaExtension);

    XdpRxQueueGetExtension(
        Config, &MpSupportedXdpExtensions.RxAction, &Rq->RxActionExtension);

    WriteUInt32Release((UINT32 *)&Rq->XdpState, XDP_STATE_ACTIVE);

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
MpXdpDeleteRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    )
{
    ADAPTER_QUEUE *AdapterQueue = (ADAPTER_QUEUE*)InterfaceRxQueue;
    ADAPTER_RX_QUEUE *Rq = &AdapterQueue->Rq;
    KEVENT DeleteComplete;

    if (Rq->XdpState == XDP_STATE_INACTIVE) {
        //
        // XDP is allowed to delete a created but inactive queue.
        //
        return;
    }

    KeInitializeEvent(&DeleteComplete, NotificationEvent, FALSE);
    Rq->DeleteComplete = &DeleteComplete;

    //
    // Ensure the state is changed with release semantics, then trigger
    // an NDIS poll to perform the actual delete work within the poll EC.
    //
    ASSERT(Rq->XdpState == XDP_STATE_ACTIVE);
    WriteUInt32Release((UINT32 *)&Rq->XdpState, XDP_STATE_DELETE_PENDING);
    AdapterQueue->Adapter->PollDispatch.RequestPoll(AdapterQueue->NdisPollHandle, 0);

    KeWaitForSingleObject(&DeleteComplete, Executive, KernelMode, FALSE, NULL);
    ASSERT(Rq->XdpState == XDP_STATE_INACTIVE);

    Rq->DeleteComplete = NULL;
    Rq->XdpRxQueue = NULL;
    Rq->FrameRing = NULL;
}
