//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

VOID
MpCleanupRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    if (Adapter->RssQueues == NULL) {
        return;
    }

    ExFreePoolWithTag(Adapter->RssQueues, POOLTAG_RSS);
    Adapter->RssQueues = NULL;
}

NDIS_STATUS
MpCreateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_STATUS NdisStatus;
    NDIS_RSS_PROCESSOR_INFO *RssProcessorInfo = NULL;
    SIZE_T AllocationSize;
    SIZE_T Size = 0;

    NdisStatus = NdisGetRssProcessorInformation(Adapter->MiniportHandle, RssProcessorInfo, &Size);
    NT_FRE_ASSERT(NdisStatus == NDIS_STATUS_BUFFER_TOO_SHORT);
    RssProcessorInfo = ExAllocatePoolZero(NonPagedPoolNx, Size, POOLTAG_RSS);
    if (RssProcessorInfo == NULL) {
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    NdisStatus = NdisGetRssProcessorInformation(Adapter->MiniportHandle, RssProcessorInfo, &Size);
    if (NdisStatus != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Adapter->NumRssProcs = RssProcessorInfo->RssProcessorCount;
    AllocationSize = sizeof(*Adapter->RssQueues) * Adapter->NumRssQueues;

    Adapter->RssQueues = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, POOLTAG_RSS);
    if (Adapter->RssQueues == NULL) {
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        RssQueue->QueueId = Index;
    }

    NdisStatus = NDIS_STATUS_SUCCESS;

Exit:

    if (RssProcessorInfo != NULL) {
        ExFreePool(RssProcessorInfo);
    }

    return NdisStatus;
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

    UNREFERENCED_PARAMETER(Adapter);

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
    }

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];
        UINT32 RssHash = 0;
        UINT32 ProcessorIndex = 0;

        if (Index < AssignedProcessorCount) {
            RssHash = RssHashValues[Index];
            ProcessorIndex = AssignedProcessors[Index];
        }

        RssQueue->RssHash = RssHash;
        RssQueue->ProcessorIndex = ProcessorIndex;
    }
}
