//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "rss.tmh"

typedef
VOID
POLL_COMPLETE_HELPER(
    _In_ NDIS_HANDLE PollHandle,
    _In_ NDIS_POLL_DATA *Poll,
    _In_ XDP_POLL_DATA *XdpData,
    _In_ XDP_NDIS_REQUEST_POLL *RequestPoll
    );

static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(NDIS_POLL)
VOID
MpPoll(
    _In_ VOID *MiniportPollContext,
    _Inout_ NDIS_POLL_DATA *Poll
    )
{
    ADAPTER_QUEUE *RssQueue = (ADAPTER_QUEUE *)MiniportPollContext;
    XDP_POLL_DATA XdpPoll = {0};
    POLL_COMPLETE_HELPER *PollComplete;

#if DBG
    ASSERT(InterlockedIncrement(&RssQueue->ActivePolls) == 1);
#endif

    MpRateSim(RssQueue);

    MpReceive(&RssQueue->Rq, &Poll->Receive, &XdpPoll.Receive);
    MpTransmit(&RssQueue->Tq, &Poll->Transmit, &XdpPoll.Transmit);

#if DBG
    ASSERT(InterlockedDecrement(&RssQueue->ActivePolls) == 0);
#endif

    if (RssQueue->Adapter->PollProvider == PollProviderFndis) {
        PollComplete = FNdisCompletePoll;
    } else {
        PollComplete = XdpCompleteNdisPoll;
    }

    PollComplete(
        RssQueue->NdisPollHandle, Poll, &XdpPoll, RssQueue->Adapter->PollDispatch.RequestPoll);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(NDIS_SET_POLL_NOTIFICATION)
VOID
MpInterruptControl(
    _In_ VOID *MiniportPollContext,
    _Inout_ NDIS_POLL_NOTIFICATION *InterruptParameters
    )
{
    ADAPTER_QUEUE *RssQueue = (ADAPTER_QUEUE *)MiniportPollContext;

    if (InterruptParameters->Enabled) {
        MpRateSimEnableInterrupt(RssQueue);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpXdpNotify(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    )
{
    ADAPTER_QUEUE *RssQueue = (ADAPTER_QUEUE *)InterfaceQueue;

    static CONST PollMask =
        XDP_NOTIFY_QUEUE_FLAG_TX |
        XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH |
        XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH;

    if (Flags & XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH) {
        RssQueue->Rq.NeedFlush = TRUE;
    }

    if (Flags & XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH) {
        RssQueue->Tq.NeedFlush = TRUE;
    }

    if (Flags & PollMask) {
        RssQueue->Adapter->PollDispatch.RequestPoll(RssQueue->NdisPollHandle, 0);
    }
}

VOID
MpDepopulateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    if (Adapter->RssQueues == NULL) {
        return;
    }

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        // Tell NDIS to stop calling the miniport's poll routine.
        if (RssQueue->NdisPollHandle != NULL) {
            Adapter->PollDispatch.DeregisterPoll(RssQueue->NdisPollHandle);
            RssQueue->NdisPollHandle = NULL;
        }

        MpCleanupReceiveQueue(&RssQueue->Rq);
        MpCleanupTransmitQueue(&RssQueue->Tq);
    }

    MpCleanupPoll(Adapter);

    ExFreePoolWithTag(Adapter->RssQueues, POOLTAG_QUEUE);
    Adapter->RssQueues = NULL;
}

NDIS_STATUS
MpPopulateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_STATUS Status;
    SIZE_T AllocationSize;

    TraceEnter(TRACE_CONTROL, "NdisMiniportHandle=%p", Adapter->MiniportHandle);

    AllocationSize = sizeof(*Adapter->RssQueues) * Adapter->NumRssQueues;

    Adapter->RssQueues =
        ExAllocatePoolZero(
            NonPagedPoolNxCacheAligned, AllocationSize, POOLTAG_QUEUE);
    if (Adapter->RssQueues == NULL) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p MpPopulateRssQueues failed to allocate RssQueues",
            Adapter->MiniportHandle);
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Status = MpInitializePoll(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        RssQueue->QueueId = Index;
        RssQueue->Adapter = Adapter;

        Status = MpInitializeReceiveQueue(&RssQueue->Rq, RssQueue);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        Status = MpInitializeTransmitQueue(&RssQueue->Tq, RssQueue);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }
    }

    Status = NDIS_STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

NDIS_STATUS
MpStartRss(
    _In_ ADAPTER_CONTEXT *Adapter,
    _Inout_ ADAPTER_QUEUE *RssQueue
    )
{
    NDIS_STATUS Status;
    NDIS_POLL_CHARACTERISTICS PollAttributes = {0};

    PollAttributes.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PollAttributes.Header.Revision = NDIS_POLL_CHARACTERISTICS_REVISION_1;
    PollAttributes.Header.Size = sizeof(PollAttributes);
    PollAttributes.PollHandler = MpPoll;
    PollAttributes.SetPollNotificationHandler = MpInterruptControl;

    Status =
        Adapter->PollDispatch.RegisterPoll(
            Adapter->MiniportHandle, RssQueue, &PollAttributes, &RssQueue->NdisPollHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    return Status;
}

VOID
MpSetRss(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *RssParams,
    _In_ SIZE_T RssParamsLength
    )
{
    UINT32 EntryCount;
    PROCESSOR_NUMBER *RssTable;
    UINT32 AssignedProcessors[MAX_RSS_QUEUES];
    UINT32 RssHashValues[MAX_RSS_QUEUES];
    UINT32 AssignedProcessorCount = 0;

    if (RssParamsLength < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        RssParams->Header.Type != NDIS_OBJECT_TYPE_RSS_PARAMETERS ||
        RssParams->Header.Revision < NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        RssParams->Header.Size < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2) {
        //
        // For simplicity, require RSS revision 2 which uses PROCESSOR_NUMBER
        // entries in the indirection table.
        //
        return;
    }

    if (NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(RssParams->HashInformation) == 0 ||
        (RssParams->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS)) {
        //
        // RSS is being disabled; ignore.
        //
        return;
    }

    EntryCount = RssParams->IndirectionTableSize / sizeof(PROCESSOR_NUMBER);
    RssTable = (PROCESSOR_NUMBER *)
        (((UCHAR *)RssParams) + RssParams->IndirectionTableOffset);

    for (ULONG Index = 0; Index < EntryCount; Index++) {
        ULONG TargetProcessor = KeGetProcessorIndexFromNumber(&RssTable[Index]);
        ULONG AssignedIndex;

        for (AssignedIndex = 0; AssignedIndex < AssignedProcessorCount; AssignedIndex++) {
            if (AssignedProcessors[AssignedIndex] == TargetProcessor) {
                break;
            }
        }

        if (AssignedIndex == AssignedProcessorCount) {
            if (AssignedProcessorCount == RTL_NUMBER_OF(AssignedProcessors)) {
                //
                // For simplicity, we support up to 64 queues.
                //
                return;
            }

            AssignedProcessors[AssignedProcessorCount] = TargetProcessor;
            RssHashValues[AssignedProcessorCount] = 0x80000000 | Index;
            AssignedProcessorCount++;
        }

        Adapter->IndirectionTable[Index] = AssignedIndex;
    }

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        if (Index < AssignedProcessorCount) {
            PROCESSOR_NUMBER ProcessorNumber;

            RssQueue->Rq.RssHash = RssHashValues[Index];
            RssQueue->HwActiveRx = TRUE;

            KeGetProcessorNumberFromIndex(AssignedProcessors[Index], &ProcessorNumber);
            Adapter->PollDispatch.SetPollAffinity(RssQueue->NdisPollHandle, &ProcessorNumber);
        } else {
            //
            // There's no convenient way to disable the unused queue, so just
            // let it float.
            //
            RssQueue->Rq.RssHash = 0;
            RssQueue->HwActiveRx = FALSE;
        }
    }

    FRE_ASSERT(RTL_IS_POWER_OF_TWO(EntryCount));
    Adapter->IndirectionMask = EntryCount - 1;
}
