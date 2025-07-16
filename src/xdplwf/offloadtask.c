//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offloadtask.tmh"

UINT16
XdpOffloadChecksumNb(
    _In_ const NET_BUFFER *NetBuffer,
    _In_ UINT32 DataLength,
    _In_ UINT32 DataOffset
    )
{
    UINT32 Checksum = 0;
    UINT32 MdlOffset;
    UINT32 Offset;
    MDL *Mdl;

    //
    // The first MDL must be checksummed from an offset.
    //
    Mdl = NetBuffer->CurrentMdl;
    MdlOffset = NetBuffer->CurrentMdlOffset;

    //
    // Advance past any additional offset.
    //
    while (DataOffset > 0) {
        UINT32 MdlBytes = Mdl->ByteCount - MdlOffset;
        if (DataOffset < MdlBytes) {
            MdlOffset += DataOffset;
            DataOffset = 0;
        } else {
            Mdl = Mdl->Next;
            MdlOffset = 0;
            DataOffset -= MdlBytes;
        }
    }

    Offset = 0;

    ASSERT(DataLength <= NetBuffer->DataLength);
    while (DataLength != 0) {
        UCHAR *Buffer;
        UINT32 BufferLength;
        UINT16 BufferChecksum;

        ASSERT(Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL));
        Buffer = Mdl->MappedSystemVa;

        BufferLength = Mdl->ByteCount;
        ASSERT(BufferLength >= MdlOffset);

        Buffer += MdlOffset;
        BufferLength -= MdlOffset;

        //
        // BufferLength might be bigger than we need, if there is "extra" data
        // in the packet.
        //
        if (BufferLength > DataLength) {
            BufferLength = DataLength;
        }

        BufferChecksum = XdpPartialChecksum(Buffer, BufferLength);
        if ((Offset & 1) == 0) {
            Checksum += BufferChecksum;
        } else {
            //
            // We're at an odd offset into the logical buffer, so we need to
            // swap the bytes that XdpPartialChecksum returns.
            //
            Checksum += (BufferChecksum >> 8) + ((BufferChecksum & MAXUINT8) << 8);
        }

        Offset += BufferLength;
        DataLength -= BufferLength;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    //
    // Wrap in the carries to reduce Checksum to 16 bits.
    // (Twice is sufficient because it can only overflow once.)
    //
    Checksum = XdpChecksumFold(Checksum);

    //
    // Take ones-complement and replace 0 with 0xffff.
    //
    if (Checksum != MAXUINT16) {
        Checksum = (UINT16)~Checksum;
    }

    return (UINT16)Checksum;
}

static
_Offload_work_routine_
VOID
XdpLwfFreeTaskOffloadSetting(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *OldOffload =
        CONTAINING_RECORD(Entry, XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD, DeleteEntry);

    ExFreePoolWithTag(OldOffload, POOLTAG_OFFLOAD);
}

typedef struct _XDP_LWF_OFFLOAD_TASK_INITIALIZE {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
} XDP_LWF_OFFLOAD_TASK_INITIALIZE;

static
_Offload_work_routine_
VOID
XdpLwfOffloadTaskInitializeWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_TASK_INITIALIZE *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_TASK_INITIALIZE, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    NTSTATUS Status;
    NDIS_OFFLOAD TaskConfig = {0};
    ULONG BytesReturned = 0;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_TCP_OFFLOAD_CURRENT_CONFIG, &TaskConfig,
            sizeof(TaskConfig), 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed OID_TCP_OFFLOAD_CURRENT_CONFIG Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XdpOffloadUpdateTaskOffloadConfig(Filter, &TaskConfig, BytesReturned);
    Status = STATUS_SUCCESS;

Exit:

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);

    TraceExitStatus(TRACE_LWF);
}

VOID
XdpLwfOffloadTaskInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_TASK_INITIALIZE Request = {0};

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadTaskInitializeWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_LWF);
}

_Offload_work_routine_
VOID
XdpLwfOffloadTaskOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *OldOffload;

    if (Filter->Offload.LowerEdge.TaskOffload != NULL) {
        OldOffload = Filter->Offload.LowerEdge.TaskOffload;
        Filter->Offload.LowerEdge.TaskOffload = NULL;
        XdpLifetimeDelete(XdpLwfFreeTaskOffloadSetting, &OldOffload->DeleteEntry);
    }
}

_Offload_work_routine_
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpOffloadUpdateTaskOffloadConfig(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ const NDIS_OFFLOAD *TaskOffload,
    _In_ UINT32 TaskOffloadSize
    )
{
    NTSTATUS Status;
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *NewOffload = NULL;
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *OldOffload = NULL;
    static const UINT32 Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    if (Filter->Offload.Deactivated) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    if (TaskOffloadSize < sizeof(TaskOffload->Header) ||
        TaskOffload->Header.Size > TaskOffloadSize ||
        TaskOffload->Header.Type != NDIS_OBJECT_TYPE_OFFLOAD ||
        TaskOffload->Header.Revision < NDIS_OFFLOAD_REVISION_1 ||
        TaskOffload->Header.Size < NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    NewOffload = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*NewOffload), POOLTAG_OFFLOAD);
    if (NewOffload == NULL) {
        TraceError(
            TRACE_LWF, "Filter=%p Failed to allocate XDP task offload setting", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Simplify the adapter's checksum capabilities and impose minimum
    // requirements. Any modern NIC should support IP, TCP and UDP checksums for
    // all protocols and directions, otherwise it probably doesn't support any.
    //
    if (((TaskOffload->Checksum.IPv4Receive.Encapsulation & Encapsulation) == Encapsulation) &&
        ((TaskOffload->Checksum.IPv6Receive.Encapsulation & Encapsulation) == Encapsulation) &&
        ((TaskOffload->Checksum.IPv4Transmit.Encapsulation & Encapsulation) == Encapsulation) &&
        ((TaskOffload->Checksum.IPv6Transmit.Encapsulation & Encapsulation) == Encapsulation) &&
        TaskOffload->Checksum.IPv4Receive.IpChecksum &&
        TaskOffload->Checksum.IPv4Receive.TcpChecksum &&
        TaskOffload->Checksum.IPv4Receive.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv4Receive.UdpChecksum &&
        TaskOffload->Checksum.IPv6Receive.TcpChecksum &&
        TaskOffload->Checksum.IPv6Receive.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv6Receive.UdpChecksum &&
        TaskOffload->Checksum.IPv4Transmit.IpChecksum &&
        TaskOffload->Checksum.IPv4Transmit.TcpChecksum &&
        TaskOffload->Checksum.IPv4Transmit.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv4Transmit.UdpChecksum &&
        TaskOffload->Checksum.IPv6Transmit.TcpChecksum &&
        TaskOffload->Checksum.IPv6Transmit.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv6Transmit.UdpChecksum) {
        NewOffload->Checksum.Enabled = TRUE;
    }

    if (NewOffload->Checksum.Enabled &&
        TaskOffload->Checksum.IPv4Receive.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv6Receive.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv4Transmit.TcpOptionsSupported &&
        TaskOffload->Checksum.IPv6Transmit.TcpOptionsSupported) {
        NewOffload->Checksum.TcpOptions = TRUE;
    }

    //
    // Simplify the adapter's LSO capabilities and impose minimum requirements.
    // Any modern NIC should support LSOv2, TCPv4, TCPv6, and TCP options.
    //
    if (((TaskOffload->LsoV2.IPv4.Encapsulation & Encapsulation) == Encapsulation) &&
        ((TaskOffload->LsoV2.IPv6.Encapsulation & Encapsulation) == Encapsulation) &&
        TaskOffload->LsoV2.IPv6.TcpOptionsSupported) {
        NewOffload->Lso.MaxOffloadSize =
            min(TaskOffload->LsoV2.IPv4.MaxOffLoadSize, TaskOffload->LsoV2.IPv6.MaxOffLoadSize);
        NewOffload->Lso.MinSegments =
            max(TaskOffload->LsoV2.IPv4.MinSegmentCount, TaskOffload->LsoV2.IPv6.MinSegmentCount);
    }

    //
    // LSO code assumes a minimum of one segment.
    //
    NewOffload->Lso.MinSegments = max(1, NewOffload->Lso.MinSegments);

    TraceInfo(
        TRACE_LWF,
        "Filter=%p updated task offload. "
        "Checksum.Enabled=%!BOOLEAN! Checksum.TcpOptions=%!BOOLEAN!"
        "Lso.MaxOffloadSize=%u Lso.MinSegments=%u",
        Filter, NewOffload->Checksum.Enabled, NewOffload->Checksum.TcpOptions,
        NewOffload->Lso.MaxOffloadSize, NewOffload->Lso.MinSegments);

    OldOffload = Filter->Offload.LowerEdge.TaskOffload;
    Filter->Offload.LowerEdge.TaskOffload = NewOffload;
    NewOffload = NULL;
    Status = STATUS_SUCCESS;

    XdpGenericTxNotifyOffloadChange(&Filter->Generic, &Filter->Offload.LowerEdge);
    // XdpGenericRxNotifyOffloadChange(&Filter->Generic, &Filter->Offload.LowerEdge);

Exit:

    if (OldOffload != NULL) {
        XdpLifetimeDelete(XdpLwfFreeTaskOffloadSetting, &OldOffload->DeleteEntry);
    }

    if (NewOffload != NULL) {
        ExFreePoolWithTag(NewOffload, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);
}

typedef struct _XDP_LWF_OFFLOAD_CHECKSUM_GET {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
    _Out_ NTSTATUS Status;
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext;
    _Out_ XDP_CHECKSUM_CONFIGURATION *ChecksumParams;
    _Inout_ UINT32 *ChecksumParamsLength;
} XDP_LWF_OFFLOAD_CHECKSUM_GET;

static
_Offload_work_routine_
VOID
XdpLwfOffloadChecksumGetWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_CHECKSUM_GET *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_CHECKSUM_GET, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *CurrentTaskSetting = NULL;
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    //
    // Determine the appropriate edge.
    //
    switch (Request->OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        CurrentTaskSetting = Filter->Offload.LowerEdge.TaskOffload;
        break;
    case XdpOffloadEdgeUpper:
        //
        // Offload translation for upper edges is not supported yet.
        //
        CurrentTaskSetting = NULL;
        break;
    default:
        ASSERT(FALSE);
        CurrentTaskSetting = NULL;
        break;
    }

    if (CurrentTaskSetting == NULL) {
        //
        // Task offload not initialized yet.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p task offload params not found", Request->OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    if (*Request->ChecksumParamsLength == 0) {
        *Request->ChecksumParamsLength = XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1;
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (*Request->ChecksumParamsLength < XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Request->ChecksumParams->Header.Revision = XDP_CHECKSUM_CONFIGURATION_REVISION_1;
    Request->ChecksumParams->Header.Size = XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1;
    Request->ChecksumParams->Enabled = CurrentTaskSetting->Checksum.Enabled;
    Request->ChecksumParams->TcpOptions = CurrentTaskSetting->Checksum.TcpOptions;
    *Request->ChecksumParamsLength = Request->ChecksumParams->Header.Size;
    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_LWF);

    Request->Status = Status;
    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
XdpLwfOffloadChecksumGet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ XDP_CHECKSUM_CONFIGURATION *ChecksumParams,
    _Inout_ UINT32 *ChecksumParamsLength
    )
{
    XDP_LWF_OFFLOAD_CHECKSUM_GET Request = {0};
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    Request.OffloadContext = OffloadContext;
    Request.ChecksumParams = ChecksumParams;
    Request.ChecksumParamsLength = ChecksumParamsLength;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadChecksumGetWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    Status = Request.Status;

    TraceExitStatus(TRACE_LWF);

    return Status;
}
