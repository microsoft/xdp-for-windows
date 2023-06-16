//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "rss.tmh"

static
VOID
XdpGenericRssFreeLifetimeIndirection(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_INDIRECTION_TABLE *IndirectionTable =
        CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_INDIRECTION_TABLE, DeleteEntry);

    ExFreePoolWithTag(IndirectionTable, POOLTAG_RSS);
}

VOID
XdpGenericRssFreeIndirection(
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    )
{
    if (Indirection->NewIndirectionTable != NULL) {
        ExFreePoolWithTag(Indirection->NewIndirectionTable, POOLTAG_RSS);
        Indirection->NewIndirectionTable = NULL;
    }

    if (Indirection->NewQueues != NULL) {
        ExFreePoolWithTag(Indirection->NewQueues, POOLTAG_RSS);
        Indirection->NewQueues = NULL;
    }
}

NTSTATUS
XdpGenericRssCreateIndirection(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *RssParams,
    _In_ ULONG RssParamsLength,
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    )
{
    NTSTATUS Status;
    XDP_LWF_GENERIC_RSS_QUEUE *NewQueues = NULL;
    ULONG AssignedQueues = 0;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *NewIndirectionTable = NULL;
    ULONG EntryCount;
    ULONG MaxProcessors = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    PROCESSOR_NUMBER *RssTable;
    PROCESSOR_NUMBER DisabledRssTable;

    //
    // XdpGenericRssCreateIndirection preallocates all data structures needed to
    // update the RSS indirection table before applying changes to NDIS. This is
    // so NDIS does not get out of sync with us, as could happen if this was done
    // after NDIS changes were applied and the allocations failed.
    //

    RtlZeroMemory(Indirection, sizeof(*Indirection));

    if (KeGetCurrentIrql() == DISPATCH_LEVEL) {
        //
        // TCPIP posts OIDs at passive level, so simplify our implementation.
        //
        // TODO: post a workitem.
        //
        TraceError(
            TRACE_LWF, "IfIndex=%u RSS update not supported at dispatch level",
            Generic->IfIndex);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (RssParamsLength < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        RssParams->Header.Type != NDIS_OBJECT_TYPE_RSS_PARAMETERS ||
        RssParams->Header.Revision < NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        RssParams->Header.Size < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2) {
        //
        // For simplicity, require RSS revision 2 which uses PROCESSOR_NUMBER
        // entries in the indirection table.
        //
        // TODO: support revision 1.
        //
        // TODO: handle revision 3 DefaultProcessorNumber.
        //
        TraceError(
            TRACE_LWF, "IfIndex=%u support for RSS parameters not implemented",
            Generic->IfIndex);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(RssParams->HashInformation) != 0 &&
        (RssParams->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) == 0) {
        EntryCount = RssParams->IndirectionTableSize / sizeof(PROCESSOR_NUMBER);
        RssTable = (PROCESSOR_NUMBER *)
            (((UCHAR *)RssParams) + RssParams->IndirectionTableOffset);
    } else {
        EntryCount = 1;
        KeGetProcessorNumberFromIndex(0, &DisabledRssTable);
        RssTable = &DisabledRssTable;
    }

    if ((RssParams->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED) != 0) {
        //
        // Don't update the indirection table if it is not changing.
        //
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    NewQueues =
        ExAllocatePoolZero(
            PagedPool, sizeof(*NewQueues) * MaxProcessors, POOLTAG_RSS);
    if (NewQueues == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    NewIndirectionTable =
        ExAllocatePoolZero(
            NonPagedPoolNxCacheAligned,
            sizeof(*NewIndirectionTable) +
                EntryCount * sizeof(NewIndirectionTable->Entries[0]),
            POOLTAG_RSS);
    if (NewIndirectionTable == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    NewIndirectionTable->IndirectionMask = EntryCount - 1;

    //
    // Figure out the new queue processor affinities.
    //
    for (ULONG Index = 0; Index < EntryCount; Index++) {
        ULONG TargetProcessor = KeGetProcessorIndexFromNumber(&RssTable[Index]);
        ULONG QueueIndex;

        for (QueueIndex = 0; QueueIndex < AssignedQueues; QueueIndex++) {
            if (NewQueues[QueueIndex].IdealProcessor == TargetProcessor) {
                break;
            }
        }

        if (QueueIndex == AssignedQueues) {
            if (AssignedQueues == MaxProcessors) {
                //
                // We found too many processors.
                //
                ASSERT(FALSE);
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }

            QueueIndex = AssignedQueues++;
            NewQueues[QueueIndex].IdealProcessor = TargetProcessor;
            NewQueues[QueueIndex].RssHash = Index;
        }

        NewIndirectionTable->Entries[Index].QueueIndex = QueueIndex;
    }

    Indirection->AssignedQueues = AssignedQueues;
    Indirection->NewIndirectionTable = NewIndirectionTable;
    Indirection->NewQueues = NewQueues;
    NewIndirectionTable = NULL;
    NewQueues = NULL;

    Status = STATUS_SUCCESS;

Exit:

    TraceInfo(
        TRACE_GENERIC, "IfIndex=%u AssignedQueues=%u Status=%!STATUS!",
        Generic->IfIndex, AssignedQueues, Status);

    if (NewIndirectionTable != NULL) {
        ExFreePoolWithTag(NewIndirectionTable, POOLTAG_RSS);
    }

    if (NewQueues != NULL) {
        ExFreePoolWithTag(NewQueues, POOLTAG_RSS);
    }

    return Status;
}

VOID
XdpGenericRssApplyIndirection(
    _In_ XDP_LWF_GENERIC *Generic,
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    )
{
    XDP_LWF_GENERIC_RSS *Rss = &Generic->Rss;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *OldIndirectionTable;

    //
    // Applies an indirection table created by XdpGenericRssCreateIndirection.
    // This procedure cannot fail unless the RSS queue count becomes out of sync.
    //

    if (Indirection->NewIndirectionTable == NULL) {
        ASSERT(Indirection->AssignedQueues == 0);
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&Generic->Lock);

    if (Indirection->AssignedQueues > Rss->QueueCount) {
        //
        // If generic RSS is not initialized, skip the OID. We'll figure it out
        // later.
        //
        ASSERT(Rss->QueueCount == 0);
        if (Rss->QueueCount > 0) {
            TraceError(
                TRACE_LWF,
                "IfIndex=%u Queue count incompatible with new indirection table",
                Generic->IfIndex);
        }
        RtlReleasePushLockExclusive(&Generic->Lock);
        goto Exit;
    }

    for (ULONG QueueIndex = 0; QueueIndex < Indirection->AssignedQueues; QueueIndex++) {
        Rss->Queues[QueueIndex].IdealProcessor = Indirection->NewQueues[QueueIndex].IdealProcessor;
        Rss->Queues[QueueIndex].RssHash = Indirection->NewQueues[QueueIndex].RssHash;
    }

    OldIndirectionTable = Rss->IndirectionTable;
    Rss->IndirectionTable = Indirection->NewIndirectionTable;
    Indirection->NewIndirectionTable = NULL;

    RtlReleasePushLockExclusive(&Generic->Lock);

    XdpLifetimeDelete(
        XdpGenericRssFreeLifetimeIndirection, &OldIndirectionTable->DeleteEntry);

Exit:

    TraceInfo(
        TRACE_GENERIC, "IfIndex=%u AssignedQueues=%u",
        Generic->IfIndex, Indirection->AssignedQueues);

    XdpGenericRssFreeIndirection(Indirection);
}

XDP_LWF_GENERIC_RSS_QUEUE *
XdpGenericRssGetQueueById(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 QueueId
    )
{
    if (QueueId >= Generic->Rss.QueueCount) {
        return NULL;
    }

    return &Generic->Rss.Queues[QueueId];
}

_IRQL_requires_(DISPATCH_LEVEL)
XDP_LWF_GENERIC_RSS_QUEUE *
XdpGenericRssGetQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ ULONG CurrentProcessor,
    _In_ BOOLEAN TxInspect,
    _In_ UINT32 RssHash
    )
{
    XDP_LWF_GENERIC_RSS *Rss = &Generic->Rss;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *IndirectionTable;
    XDP_LWF_GENERIC_RSS_QUEUE *Queues;
    RSS_INDIRECTION_ENTRY *IndirectionEntry;
    UINT32 IndirectionIndex;
    XDP_LWF_GENERIC_RSS_QUEUE *Queue;

    IndirectionTable = ReadPointerNoFence(&Rss->IndirectionTable);
    Queues = ReadPointerNoFence(&Rss->Queues);

    if (IndirectionTable == NULL || Queues == NULL) {
        return NULL;
    } else if (RssHash == 0 && IndirectionTable->IndirectionMask > 0 && !TxInspect) {
        Queue = NULL;

        //
        // Some NIC vendors support RSS, but do not fill out the hash OOB fields.
        // In this case, infer the queue from the current processor. For TX
        // inspect, do not use the current CPU to infer the RSS queue ID since
        // the NDIS send path is not RSS-affinitized.
        //
        // TODO: Optimize with (CPU -> queue) index.
        //
        for (IndirectionIndex = 0;
            IndirectionIndex <= IndirectionTable->IndirectionMask;
            IndirectionIndex++) {
            IndirectionEntry = &IndirectionTable->Entries[IndirectionIndex];
            Queue = &Queues[IndirectionEntry->QueueIndex];
            if (Queue->IdealProcessor == CurrentProcessor) {
                return Queue;
            }
        }

        //
        // Assign to queue at index 0 if appropriate queue was not found.
        //
        return &Queues[0];
    } else {
        //
        // Normal case where RSS is supported and hash OOB is filled out.
        //
        IndirectionIndex = RssHash & IndirectionTable->IndirectionMask;
        IndirectionEntry = &IndirectionTable->Entries[IndirectionIndex];
        Queue = &Queues[IndirectionEntry->QueueIndex];

        return Queue;
    }
}

NDIS_STATUS
XdpGenericRssInspectOidRequest(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_OID_REQUEST *Request
    )
{
    NTSTATUS Status;
    XDP_LWF_GENERIC_INDIRECTION_STORAGE Indirection = {0};

    if (Request->RequestType == NdisRequestSetInformation &&
        Request->DATA.SET_INFORMATION.Oid == OID_GEN_RECEIVE_SCALE_PARAMETERS) {

        Status =
            XdpGenericRssCreateIndirection(
                Generic,
                Request->DATA.SET_INFORMATION.InformationBuffer,
                Request->DATA.SET_INFORMATION.InformationBufferLength,
                &Indirection);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        XdpGenericRssApplyIndirection(Generic, &Indirection);
    }

    Status = STATUS_SUCCESS;

Exit:

    XdpGenericRssFreeIndirection(&Indirection);

    return XdpConvertNtStatusToNdisStatus(Status);
}

NTSTATUS
XdpGenericRssInitialize(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    NTSTATUS Status;
    XDP_LWF_GENERIC_RSS *Rss = &Generic->Rss;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps = {0};
    NDIS_RECEIVE_SCALE_PARAMETERS *RssParams = NULL;
    ULONG BytesReturned = 0;
    ULONG QueueCount;
    XDP_LWF_GENERIC_RSS_QUEUE *Queues = NULL;
    XDP_LWF_GENERIC_RSS_CLEANUP *QueueCleanup = NULL;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *IndirectionTable = NULL;
    XDP_LWF_GENERIC_INDIRECTION_STORAGE Indirection = {0};

    Status =
        XdpLwfOidInternalRequest(
            Generic->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_CAPABILITIES, &RssCaps,
            sizeof(RssCaps), 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED) {
        goto Exit;
    }

    QueueCount = 1;

    if (NT_SUCCESS(Status) &&
        BytesReturned >= NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1 &&
        RssCaps.Header.Type == NDIS_OBJECT_TYPE_RSS_CAPABILITIES &&
        RssCaps.Header.Revision >= NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_1 &&
        RssCaps.Header.Size >= NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1) {
        //
        // RSS allows more processors than receive queues, but the RSS
        // implementation in TCPIP constrains the number of processors to the
        // number of receive queues. The NDIS API to query the actual maximum
        // processor count is unavailable to LWFs, so simply use the number of
        // receive queues instead.
        //
        // TODO: can we query the max processor count some other way? (It can be
        // queried via miniport and protocol driver handles, and WMI.)
        //
        QueueCount = max(QueueCount, RssCaps.NumberOfReceiveQueues);
    }

    Queues =
        ExAllocatePoolZero(
            NonPagedPoolNxCacheAligned, sizeof(*Queues) * QueueCount,
            POOLTAG_RSS);
    if (Queues == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    QueueCleanup = ExAllocatePoolZero(PagedPool, sizeof(*QueueCleanup), POOLTAG_RSS);
    if (QueueCleanup == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Create an indirection table of size 1, i.e. RSS is disabled. Later on
    // we'll query NDIS and attempt to enable RSS, and we'll also update the RSS
    // state whenever an RSS set OID is passed down.
    //
    IndirectionTable =
        ExAllocatePoolZero(
            NonPagedPoolNxCacheAligned,
            sizeof(*IndirectionTable) + sizeof(IndirectionTable->Entries[0]),
            POOLTAG_RSS);
    if (IndirectionTable == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    IndirectionTable->IndirectionMask = 0;
    for (ULONG Index = 0; Index <= IndirectionTable->IndirectionMask; Index++) {
        RSS_INDIRECTION_ENTRY *Entry = &IndirectionTable->Entries[Index];

        //
        // While RSS is disabled, steer all packets to the 0th queue.
        //
        Entry->QueueIndex = 0;
    }

    RtlAcquirePushLockExclusive(&Generic->Lock);
    Rss->QueueCount = QueueCount;
    Rss->Queues = Queues;
    Rss->QueueCleanup = QueueCleanup;
    Rss->QueueCleanup->Queues = Queues;
    Rss->IndirectionTable = IndirectionTable;
    Queues = NULL;
    QueueCleanup = NULL;
    IndirectionTable = NULL;
    RtlReleasePushLockExclusive(&Generic->Lock);

    //
    // Attempt to query the indirection table.
    //

    Status =
        XdpLwfOidInternalRequest(
            Generic->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_PARAMETERS, RssParams, 0, 0, 0,
            &BytesReturned);
    if (Status != STATUS_BUFFER_TOO_SMALL || BytesReturned == 0) {
        if (Status == STATUS_NOT_SUPPORTED) {
            // Fall back to the implicit single RSS queue.
            Status = STATUS_SUCCESS;
        }
        goto Exit;
    }

    RssParams =
        ExAllocatePoolZero(NonPagedPoolNx, BytesReturned, POOLTAG_RSS);
    if (RssParams == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status =
        XdpLwfOidInternalRequest(
            Generic->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_PARAMETERS, RssParams, BytesReturned,
            0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpGenericRssCreateIndirection(
            Generic, RssParams, BytesReturned, &Indirection);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XdpGenericRssApplyIndirection(Generic, &Indirection);

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpGenericRssCleanup(Generic);
    }

    if (RssParams != NULL) {
        ExFreePoolWithTag(RssParams, POOLTAG_RSS);
    }

    if (IndirectionTable != NULL) {
        ExFreePoolWithTag(IndirectionTable, POOLTAG_RSS);
    }

    if (QueueCleanup != NULL) {
        ExFreePoolWithTag(QueueCleanup, POOLTAG_RSS);
    }

    if (Queues != NULL) {
        ExFreePoolWithTag(Queues, POOLTAG_RSS);
    }

    XdpGenericRssFreeIndirection(&Indirection);

    return Status;
}

static
VOID
XdpGenericRssCleanupQueues(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_GENERIC_RSS_CLEANUP *Cleanup =
        CONTAINING_RECORD(Entry, XDP_LWF_GENERIC_RSS_CLEANUP, DeleteEntry);

    ExFreePoolWithTag(Cleanup->Queues, POOLTAG_RSS);
    ExFreePoolWithTag(Cleanup, POOLTAG_RSS);
}

VOID
XdpGenericRssCleanup(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    XDP_LWF_GENERIC_RSS *Rss = &Generic->Rss;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *IndirectionTable = NULL;
    XDP_LWF_GENERIC_RSS_CLEANUP *QueueCleanup = NULL;

    RtlAcquirePushLockExclusive(&Generic->Lock);

    if (Rss->QueueCleanup != NULL) {
        QueueCleanup = Rss->QueueCleanup;
        Rss->Queues = NULL;
        Rss->QueueCleanup = NULL;
        Rss->QueueCount = 0;
    }

    if (Rss->IndirectionTable != NULL) {
        IndirectionTable = Rss->IndirectionTable;
        Rss->IndirectionTable = NULL;
    }

    RtlReleasePushLockExclusive(&Generic->Lock);

    if (QueueCleanup != NULL) {
        XdpLifetimeDelete(XdpGenericRssCleanupQueues, &QueueCleanup->DeleteEntry);
    }

    if (IndirectionTable != NULL) {
        XdpLifetimeDelete(XdpGenericRssFreeLifetimeIndirection, &IndirectionTable->DeleteEntry);
    }
}
