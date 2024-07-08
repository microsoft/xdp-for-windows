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
            Checksum += (BufferChecksum >> 8) + ((BufferChecksum & 0xFF) << 8);
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
    if (Checksum != 0xffff) {
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
        TaskOffload->Header.Size > TaskOffloadSize) {
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
    // requirements. Any modern NIC should support IP, TCP (with options) and
    // UDP checksums for all protocols and directions, otherwise it probably
    // doesn't support any.
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
        "Checksum.Enabled=%!BOOLEAN! Lso.MaxOffloadSize=%u Lso.MinSegments=%u",
        Filter, NewOffload->Checksum.Enabled, NewOffload->Lso.MaxOffloadSize,
        NewOffload->Lso.MinSegments);

    OldOffload = Filter->Offload.LowerEdge.TaskOffload;
    Filter->Offload.LowerEdge.TaskOffload = NewOffload;
    NewOffload = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (OldOffload != NULL) {
        XdpLifetimeDelete(XdpLwfFreeTaskOffloadSetting, &OldOffload->DeleteEntry);
    }

    if (NewOffload != NULL) {
        ExFreePoolWithTag(NewOffload, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);
}
