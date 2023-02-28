//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module configures XDP programs on interfaces.
//

#include "precomp.h"
#include <xdptransport.h>
#include "program.tmh"

#pragma pack(push)
#pragma pack(1)
typedef struct QUIC_HEADER_INVARIANT {
    union {
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
        } COMMON_HDR;
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UINT32 Version;
            UCHAR DestCidLength;
            UCHAR DestCid[0];
            //UCHAR SourceCidLength;
            //UCHAR SourceCid[SourceCidLength];
        } LONG_HDR;
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UCHAR DestCid[0];
        } SHORT_HDR;
    };
} QUIC_HEADER_INVARIANT;
#pragma pack(pop)

typedef struct _XDP_PROGRAM_FRAME_STORAGE {
    ETHERNET_HEADER EthHdr;
    union {
        IPV4_HEADER Ip4Hdr;
        IPV6_HEADER Ip6Hdr;
    };
    union {
        UDP_HDR UdpHdr;
        TCP_HDR TcpHdr;
    };
    UINT8 TcpHdrOptions[40]; // Up to 40B options/paddings
    // Invariant header + 1 for SourceCidLength + 2x CIDS
    UINT8 QuicStorage[
        sizeof(QUIC_HEADER_INVARIANT) +
        sizeof(UCHAR) +
        QUIC_MAX_CID_LENGTH * 2];
} XDP_PROGRAM_FRAME_STORAGE;

typedef struct _XDP_PROGRAM_PAYLOAD_CACHE {
    XDP_BUFFER *Buffer;
    UINT32 BufferDataOffset;
    UINT32 FragmentIndex;
    UINT32 FragmentCount;
    BOOLEAN IsFragmentedBuffer;
} XDP_PROGRAM_PAYLOAD_CACHE;

typedef struct _XDP_PROGRAM_FRAME_CACHE {
    union {
        struct {
            UINT32 EthCached : 1;
            UINT32 EthValid : 1;
            UINT32 Ip4Cached : 1;
            UINT32 Ip4Valid : 1;
            UINT32 Ip6Cached : 1;
            UINT32 Ip6Valid : 1;
            UINT32 UdpCached : 1;
            UINT32 UdpValid : 1;
            UINT32 TcpCached : 1;
            UINT32 TcpValid : 1;
            UINT32 TransportPayloadCached : 1;
            UINT32 TransportPayloadValid : 1;
            UINT32 QuicCached : 1;
            UINT32 QuicValid : 1;
            UINT32 QuicIsLongHeader : 1;
        };
        UINT32 Flags;
    };

    ETHERNET_HEADER *EthHdr;
    union {
        IPV4_HEADER *Ip4Hdr;
        IPV6_HEADER *Ip6Hdr;
    };
    union {
        UDP_HDR *UdpHdr;
        TCP_HDR *TcpHdr;
    };
    UINT8 *TcpHdrOptions;
    UINT8 QuicCidLength;
    CONST UINT8 *QuicCid; // Src CID for long header, Dest CID for short header
    XDP_PROGRAM_PAYLOAD_CACHE TransportPayload;
} XDP_PROGRAM_FRAME_CACHE;

typedef struct _XDP_PROGRAM {
    //
    // Storage for discontiguous headers.
    //
    XDP_PROGRAM_FRAME_STORAGE FrameStorage;

    DECLSPEC_CACHEALIGN
    UINT32 RuleCount;
    XDP_RULE Rules[0];
} XDP_PROGRAM;

typedef struct _EBPF_XDP_MD {
    xdp_md_t Base;
} EBPF_XDP_MD;

//
// Data path routines.
//

static
VOID
XdpInitializeFrameCache(
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache
    )
{
    Cache->Flags = 0;
}

static
XDP_RX_ACTION
XdpInvokeEbpf(
    _In_ HANDLE EbpfTarget,
    _In_opt_ XDP_INSPECTION_EBPF_CONTEXT *EbpfContext,
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    const EBPF_EXTENSION_CLIENT *Client = (const EBPF_EXTENSION_CLIENT *)EbpfTarget;
    const VOID *ClientBindingContext = EbpfExtensionClientGetClientContext(Client);
    XDP_BUFFER *Buffer;
    UCHAR *Va;
    EBPF_XDP_MD XdpMd;
    ebpf_result_t EbpfResult;
    XDP_RX_ACTION RxAction;
    UINT32 Result;

    UNREFERENCED_PARAMETER(FragmentIndex);

    ASSERT((FragmentRing == NULL) || (FragmentExtension != NULL));

    //
    // Fragmented frames are currently not supported by eBPF.
    //
    if (FragmentRing != NULL &&
        XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount != 0) {
        RxAction = XDP_RX_ACTION_DROP;
        goto Exit;
    }

    Buffer = &Frame->Buffer;
    Va = XdpGetVirtualAddressExtension(Buffer, VirtualAddressExtension)->VirtualAddress;
    Va += Buffer->DataOffset;

    XdpMd.Base.data = Va;
    XdpMd.Base.data_end = Va + Buffer->DataLength;
    XdpMd.Base.data_meta = 0;
    XdpMd.Base.ingress_ifindex = IFI_UNSPECIFIED;

    if (EbpfContext != NULL) {
        ebpf_invoke_program_batch_function_t EbpfInvokeProgram =
            (ebpf_invoke_program_batch_function_t)
                EbpfExtensionClientGetDispatch(Client)->function[2];
        EbpfResult = EbpfInvokeProgram(ClientBindingContext, &XdpMd.Base, &Result, EbpfContext);
    } else {
        ebpf_invoke_program_function_t EbpfInvokeProgram =
            (ebpf_invoke_program_function_t)EbpfExtensionClientGetDispatch(Client)->function[0];
        EbpfResult = EbpfInvokeProgram(ClientBindingContext, &XdpMd.Base, &Result);
    }

    if (EbpfResult != EBPF_SUCCESS) {
        EventWriteEbpfProgramFailure(&MICROSOFT_XDP_PROVIDER, ClientBindingContext, EbpfResult);
        RxAction = XDP_RX_ACTION_DROP;
        goto Exit;
    }

    switch (Result) {
    case XDP_PASS:
        RxAction = XDP_RX_ACTION_PASS;
        break;

    case XDP_TX:
        RxAction = XDP_RX_ACTION_TX;
        break;

    default:
        ASSERT(FALSE);
        __fallthrough;
    case XDP_DROP:
        RxAction = XDP_RX_ACTION_DROP;
        break;
    }

Exit:

    return RxAction;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspectEbpf(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_INSPECTION_CONTEXT *InspectionContext,
    _In_ XDP_RING *FrameRing,
    _In_ UINT32 FrameIndex,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    XDP_FRAME *Frame;

    ASSERT(XdpProgramIsEbpf(Program));
    ASSERT(FrameIndex <= FrameRing->Mask);
    ASSERT(
        (FragmentRing == NULL && FragmentIndex == 0) ||
        (FragmentRing && FragmentIndex <= FragmentRing->Mask));

    Frame = XdpRingGetElement(FrameRing, FrameIndex);

    return
        XdpInvokeEbpf(
            Program->Rules[0].Ebpf.Target, &InspectionContext->EbpfContext, Frame, FragmentRing,
            FragmentExtension, FragmentIndex, VirtualAddressExtension);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Success_(return)
BOOLEAN
XdpInspectEbpfStartBatch(
    _In_ XDP_PROGRAM *Program,
    _Inout_ XDP_INSPECTION_CONTEXT *InspectionContext
    )
{
    const EBPF_EXTENSION_CLIENT *Client;
    const VOID *ClientBindingContext;
    ebpf_result_t EbpfResult;

    ASSERT(XdpProgramIsEbpf(Program));

    Client = (const EBPF_EXTENSION_CLIENT *)Program->Rules[0].Ebpf.Target;
    ClientBindingContext = EbpfExtensionClientGetClientContext(Client);

    ebpf_invoke_batch_begin_function_t EbpfBatchBegin =
        (ebpf_invoke_batch_begin_function_t)
            EbpfExtensionClientGetDispatch(Client)->function[1];

    EbpfResult =
        EbpfBatchBegin(
            ClientBindingContext, sizeof(InspectionContext->EbpfContext),
            &InspectionContext->EbpfContext);

    return EbpfResult == EBPF_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpInspectEbpfEndBatch(
    _In_ XDP_PROGRAM *Program,
    _Inout_ XDP_INSPECTION_CONTEXT *InspectionContext
    )
{
    const EBPF_EXTENSION_CLIENT *Client;
    const VOID *ClientBindingContext;
    ebpf_result_t EbpfResult;

    UNREFERENCED_PARAMETER(InspectionContext);

    ASSERT(XdpProgramIsEbpf(Program));

    Client = (const EBPF_EXTENSION_CLIENT *)Program->Rules[0].Ebpf.Target;
    ClientBindingContext = EbpfExtensionClientGetClientContext(Client);

    ebpf_invoke_batch_end_function_t EbpfBatchEnd =
        (ebpf_invoke_batch_end_function_t)
            EbpfExtensionClientGetDispatch(Client)->function[3];

    EbpfResult = EbpfBatchEnd(ClientBindingContext);

    ASSERT(EbpfResult == EBPF_SUCCESS);
}

static
UINT32
XdpGetContiguousHeaderLength(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _In_ VOID *HeaderStorage,
    _In_ UINT32 HeaderSize,
    _Out_ VOID **Header
    )
{
    UCHAR *Hdr = HeaderStorage;

    UNREFERENCED_PARAMETER(Frame);

    while (HeaderSize > 0) {
        UINT32 CopyLength = min(HeaderSize, (*Buffer)->DataLength - *BufferDataOffset);
        UCHAR *Va = XdpGetVirtualAddressExtension(*Buffer, VirtualAddressExtension)->VirtualAddress;

        //
        // If the current buffer is depleted, advance to the next fragment.
        //
        if (CopyLength == 0) {
            if (*FragmentsRemaining == 0) {
                break;
            } else {
                *FragmentIndex = (*FragmentIndex + 1) & FragmentRing->Mask;
                *FragmentsRemaining -= 1;
                *Buffer = XdpRingGetElement(FragmentRing, *FragmentIndex);
                *BufferDataOffset = 0;
                continue;
            }
        }

        //
        // If the buffer contains the contiguous header, return it directly.
        //
        if (Hdr == HeaderStorage && *BufferDataOffset + HeaderSize <= (*Buffer)->DataLength) {
            *Header = Va + (*Buffer)->DataOffset + *BufferDataOffset;
            *BufferDataOffset += HeaderSize;
            return HeaderSize;
        }

        RtlCopyMemory(Hdr, Va + (*Buffer)->DataOffset + *BufferDataOffset, CopyLength);
        *BufferDataOffset += CopyLength;
        Hdr += CopyLength;
        HeaderSize -= CopyLength;
    }

    *Header = HeaderStorage;
    return (UINT32)(Hdr - (UCHAR*)HeaderStorage);
}

static
_Success_(return != FALSE)
BOOLEAN
XdpGetContiguousHeader(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _In_ VOID *HeaderStorage,
    _In_ UINT32 HeaderSize,
    _Out_ VOID **Header
    )
{
    ASSERT(HeaderSize != 0);
    UINT32 ReadLength =
        XdpGetContiguousHeaderLength(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining,
            FragmentRing, VirtualAddressExtension, HeaderStorage, HeaderSize, Header);
    return ReadLength == HeaderSize;
}

static
VOID
XdpCopyMemoryToFrame(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _In_ UINT32 FrameDataOffset,
    _In_ VOID *Data,
    _In_ UINT32 DataLength
    )
{
    XDP_BUFFER *Buffer = &Frame->Buffer;
    UINT32 FragmentCount;

    //
    // The first buffer is stored in the frame ring, so bias the fragment index
    // so the initial increment yields the first buffer in the fragment ring.
    //
    FragmentIndex--;
    FragmentCount = XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount;

    while (DataLength > 0) {
        if (FrameDataOffset >= Buffer->DataLength) {
            FrameDataOffset -= Buffer->DataLength;
            FragmentIndex = (FragmentIndex + 1) & FragmentRing->Mask;
            Buffer = XdpRingGetElement(FragmentRing, FragmentIndex);
            ASSERT(FragmentCount > 0);
            FragmentCount--;
        } else {
            UINT32 CopyLength;
            UCHAR *Va;

            CopyLength = min(DataLength, Buffer->DataLength - FrameDataOffset);
            Va = XdpGetVirtualAddressExtension(Buffer, VirtualAddressExtension)->VirtualAddress;
            RtlCopyMemory(Va + Buffer->DataOffset, Data, CopyLength);

            Data = RTL_PTR_ADD(Data, CopyLength);
            DataLength -= CopyLength;
            FrameDataOffset += CopyLength;
        }
    }
}

static
VOID
XdpParseFragmentedEthernet(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->EthValid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->EthHdr, sizeof(Storage->EthHdr), &Cache->EthHdr);
}

static
VOID
XdpParseFragmentedIp4(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->Ip4Valid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->Ip4Hdr, sizeof(Storage->Ip4Hdr), &Cache->Ip4Hdr);
}

static
VOID
XdpParseFragmentedIp6(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->Ip6Valid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->Ip6Hdr, sizeof(Storage->Ip6Hdr), &Cache->Ip6Hdr);
}

static
VOID
XdpParseFragmentedUdp(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->UdpValid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->UdpHdr, sizeof(Storage->UdpHdr), &Cache->UdpHdr);
}

static
VOID
XdpParseFragmentedTcp(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    UINT32 HeaderLengh;
    BOOLEAN Valid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->TcpHdr, sizeof(Storage->TcpHdr), &Cache->TcpHdr);
    if (!Valid) {
        return;
    }

    HeaderLengh = TCP_HDR_LEN_TO_BYTES(Cache->TcpHdr->th_len);
    if (HeaderLengh < sizeof(Storage->TcpHdr)) {
        return;
    }

    if (HeaderLengh > sizeof(Storage->TcpHdr)) {
        //
        // Attempt to read TCP options.
        //
        Valid =
            XdpGetContiguousHeader(
                Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
                VirtualAddressExtension,
                &Storage->TcpHdrOptions,
                TCP_HDR_LEN_TO_BYTES(Cache->TcpHdr->th_len) - sizeof(Storage->TcpHdr),
                &Cache->TcpHdrOptions);
    }

    Cache->TcpValid = Valid;
}

static
VOID
XdpParseFragmentedFrame(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    XDP_BUFFER *Buffer = &Frame->Buffer;
    UINT32 BufferDataOffset = 0;
    UINT32 FragmentCount;
    IPPROTO IpProto = IPPROTO_MAX;

    //
    // The first buffer is stored in the frame ring, so bias the fragment index
    // so the initial increment yields the first buffer in the fragment ring.
    //
    FragmentIndex--;
    FragmentCount = XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount;

    if (!Cache->EthValid) {
        XdpParseFragmentedEthernet(
            Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
            VirtualAddressExtension, Cache, Storage);

        if (!Cache->EthValid) {
            return;
        }
    }

    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        if (!Cache->Ip4Valid) {
            XdpParseFragmentedIp4(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->Ip4Valid) {
                return;
            }
        }
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV6)) {
        if (!Cache->Ip6Valid) {
            XdpParseFragmentedIp6(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->Ip6Valid) {
                return;
            }
        }
        IpProto = Cache->Ip6Hdr->NextHeader;
    } else {
        return;
    }

    if (IpProto == IPPROTO_UDP) {
        if (!Cache->UdpValid) {
            XdpParseFragmentedUdp(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->UdpValid) {
                return;
            }

            Cache->TransportPayload.Buffer = Buffer;
            Cache->TransportPayload.BufferDataOffset = BufferDataOffset;
            Cache->TransportPayload.FragmentCount = FragmentCount;
            Cache->TransportPayload.FragmentIndex = FragmentIndex;
            Cache->TransportPayload.IsFragmentedBuffer = TRUE;
            Cache->TransportPayloadValid = TRUE;
        }
    } else if (IpProto == IPPROTO_TCP) {
        if (!Cache->TcpValid) {
            XdpParseFragmentedTcp(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->TcpValid) {
                return;
            }

            Cache->TransportPayload.Buffer = Buffer;
            Cache->TransportPayload.BufferDataOffset = BufferDataOffset;
            Cache->TransportPayload.FragmentCount = FragmentCount;
            Cache->TransportPayload.FragmentIndex = FragmentIndex;
            Cache->TransportPayload.IsFragmentedBuffer = TRUE;
            Cache->TransportPayloadValid = TRUE;
        }
    }
}

static
VOID
XdpParseFrame(
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    XDP_BUFFER *Buffer;
    UCHAR *Va;
    IPPROTO IpProto = IPPROTO_MAX;
    UINT32 Offset = 0;

    //
    // This routine always attempts to parse Ethernet through UDP headers.
    //
    Cache->EthCached = TRUE;
    Cache->Ip4Cached = TRUE;
    Cache->Ip6Cached = TRUE;
    Cache->UdpCached = TRUE;
    Cache->TcpCached = TRUE;
    Cache->TransportPayloadCached = TRUE;

    //
    // Attempt to parse all headers in a single pass over the first buffer. If
    // there's not enough data in the first buffer, fall back to handling
    // discontiguous headers via the fragment ring.
    //

    Buffer = &Frame->Buffer;
    Va = XdpGetVirtualAddressExtension(Buffer, VirtualAddressExtension)->VirtualAddress;
    Va += Buffer->DataOffset;

    if (Buffer->DataLength < sizeof(*Cache->EthHdr)) {
        goto BufferTooSmall;
    }
    Cache->EthHdr = (ETHERNET_HEADER *)&Va[Offset];
    Cache->EthValid = TRUE;
    Offset += sizeof(*Cache->EthHdr);

    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->Ip4Hdr)) {
            goto BufferTooSmall;
        }
        Cache->Ip4Hdr = (IPV4_HEADER *)&Va[Offset];
        Cache->Ip4Valid = TRUE;
        Offset += sizeof(*Cache->Ip4Hdr);
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV6)) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->Ip6Hdr)) {
            goto BufferTooSmall;
        }
        Cache->Ip6Hdr = (IPV6_HEADER *)&Va[Offset];
        Cache->Ip6Valid = TRUE;
        Offset += sizeof(*Cache->Ip6Hdr);
        IpProto = Cache->Ip6Hdr->NextHeader;
    } else {
        return;
    }

    if (IpProto == IPPROTO_UDP) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->UdpHdr)) {
            goto BufferTooSmall;
        }
        Cache->UdpHdr = (UDP_HDR *)&Va[Offset];
        Cache->UdpValid = TRUE;
        Offset += sizeof(*Cache->UdpHdr);
        Cache->TransportPayload.Buffer = Buffer;
        Cache->TransportPayload.BufferDataOffset = Offset;
        Cache->TransportPayload.IsFragmentedBuffer = FALSE;
        Cache->TransportPayloadValid = TRUE;
    } else if (IpProto == IPPROTO_TCP) {
        UINT32 HeaderLength;
        if (Buffer->DataLength < Offset + sizeof(*Cache->TcpHdr)) {
            goto BufferTooSmall;
        }

        HeaderLength = TCP_HDR_LEN_TO_BYTES(((TCP_HDR *)&Va[Offset])->th_len);
        if (Buffer->DataLength < Offset + HeaderLength) {
            goto BufferTooSmall;
        }

        Cache->TcpHdr = (TCP_HDR *)&Va[Offset];
        Cache->TcpValid = TRUE;
        Offset += HeaderLength;
        Cache->TransportPayload.Buffer = Buffer;
        Cache->TransportPayload.BufferDataOffset = Offset;
        Cache->TransportPayload.IsFragmentedBuffer = FALSE;
        Cache->TransportPayloadValid = TRUE;
    }

    return;

BufferTooSmall:

    if (FragmentRing != NULL) {
        ASSERT(FragmentExtension);
        XdpParseFragmentedFrame(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
            Cache, Storage);
    }
}

static
BOOLEAN
Ipv4PrefixMatch(
    _In_ CONST IN_ADDR *Ip,
    _In_ CONST IN_ADDR *Prefix,
    _In_ CONST IN_ADDR *Mask
    )
{
    return (Ip->s_addr & Mask->s_addr) == Prefix->s_addr;
}

static
BOOLEAN
Ipv6PrefixMatch(
    _In_ CONST IN6_ADDR *Ip,
    _In_ CONST IN6_ADDR *Prefix,
    _In_ CONST IN6_ADDR *Mask
    )
{
    CONST UINT64 *Ip64 = (CONST UINT64 *)Ip;
    CONST UINT64 *Prefix64 = (CONST UINT64 *)Prefix;
    CONST UINT64 *Mask64 = (CONST UINT64 *)Mask;

    return
        ((Ip64[0] & Mask64[0]) == Prefix64[0]) &
        ((Ip64[1] & Mask64[1]) == Prefix64[1]);
}

static
BOOLEAN
UdpTupleMatch(
    _In_ XDP_MATCH_TYPE Type,
    _In_ CONST XDP_PROGRAM_FRAME_CACHE *Cache,
    _In_ CONST XDP_TUPLE *Tuple
    )
{
    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        return
            Type == XDP_MATCH_IPV4_UDP_TUPLE &&
            Cache->UdpHdr->uh_sport == Tuple->SourcePort &&
            Cache->UdpHdr->uh_dport == Tuple->DestinationPort &&
            IN4_ADDR_EQUAL(&Cache->Ip4Hdr->SourceAddress, &Tuple->SourceAddress.Ipv4) &&
            IN4_ADDR_EQUAL(&Cache->Ip4Hdr->DestinationAddress, &Tuple->DestinationAddress.Ipv4);
    } else { // IPv6
        return
            Type == XDP_MATCH_IPV6_UDP_TUPLE &&
            Cache->UdpHdr->uh_sport == Tuple->SourcePort &&
            Cache->UdpHdr->uh_dport == Tuple->DestinationPort &&
            IN6_ADDR_EQUAL(&Cache->Ip6Hdr->SourceAddress, &Tuple->SourceAddress.Ipv6) &&
            IN6_ADDR_EQUAL(&Cache->Ip6Hdr->DestinationAddress, &Tuple->DestinationAddress.Ipv6);
    }
}

static
BOOLEAN
QuicCidMatch(
    _In_ XDP_MATCH_TYPE Type,
    _In_ CONST XDP_PROGRAM_FRAME_CACHE *QuicHeader,
    _In_ CONST XDP_QUIC_FLOW *Flow
    )
{
    if ((Type == XDP_MATCH_QUIC_FLOW_SRC_CID ||
         Type == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID) !=
        (QuicHeader->QuicIsLongHeader == 1)) {
        return FALSE;
    }
    ASSERT(Flow->CidOffset + Flow->CidLength <= QUIC_MAX_CID_LENGTH);
    if (QuicHeader->QuicCidLength < Flow->CidOffset + Flow->CidLength) {
        return FALSE;
    }
    return memcmp(&QuicHeader->QuicCid[Flow->CidOffset], Flow->CidData, Flow->CidLength) == 0;
}

static
_Success_(return != FALSE)
BOOLEAN
XdpParseQuicHeaderPayload(
    _In_ CONST UINT8 *Payload,
    _In_ UINT32 DataLength,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *FrameCache
    )
{
    CONST QUIC_HEADER_INVARIANT* QuicHdr = (CONST QUIC_HEADER_INVARIANT*)Payload;

    if (DataLength < RTL_SIZEOF_THROUGH_FIELD(QUIC_HEADER_INVARIANT, COMMON_HDR)) {
        return FALSE;
    }

    if (QuicHdr->COMMON_HDR.IsLongHeader) {
        if (DataLength < RTL_SIZEOF_THROUGH_FIELD(QUIC_HEADER_INVARIANT, LONG_HDR)) {
            return FALSE;
        }
        if (DataLength <
                RTL_SIZEOF_THROUGH_FIELD(QUIC_HEADER_INVARIANT, LONG_HDR) +
                QuicHdr->LONG_HDR.DestCidLength + sizeof(UCHAR)) {
            return FALSE;
        }
        FrameCache->QuicCidLength =
            QuicHdr->LONG_HDR.DestCid[QuicHdr->LONG_HDR.DestCidLength];
        if (DataLength <
                RTL_SIZEOF_THROUGH_FIELD(QUIC_HEADER_INVARIANT, LONG_HDR) +
                QuicHdr->LONG_HDR.DestCidLength +
                sizeof(UCHAR) +
                FrameCache->QuicCidLength) {
            return FALSE;
        }
        FrameCache->QuicCid =
            QuicHdr->LONG_HDR.DestCid +
            QuicHdr->LONG_HDR.DestCidLength +
            sizeof(UCHAR);
        FrameCache->QuicValid = TRUE;
        FrameCache->QuicIsLongHeader = TRUE;
        return TRUE;
    }

    FrameCache->QuicCidLength =
        (UINT8)min(
            DataLength - RTL_SIZEOF_THROUGH_FIELD(QUIC_HEADER_INVARIANT, SHORT_HDR),
            QUIC_MAX_CID_LENGTH);
    FrameCache->QuicCid = QuicHdr->SHORT_HDR.DestCid;
    FrameCache->QuicValid = TRUE;
    FrameCache->QuicIsLongHeader = FALSE;
    return FrameCache->QuicCidLength == QUIC_MAX_CID_LENGTH;
}

static
VOID
XdpParseFragmentedQuicHeader(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *FrameStore,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *FrameCache
    )
{
    XDP_BUFFER *Buffer = FrameCache->TransportPayload.Buffer;
    UINT32 BufferDataOffset = FrameCache->TransportPayload.BufferDataOffset;
    UINT32 FragmentCount = FrameCache->TransportPayload.FragmentCount;
    UINT8* QuicPayload = NULL;
    UINT32 ReadLength;

    if (FrameCache->TransportPayload.IsFragmentedBuffer) {
        FragmentIndex = FrameCache->TransportPayload.FragmentIndex;
    } else {
        //
        // The first buffer is stored in the frame ring, so bias the fragment index
        // so the initial increment yields the first buffer in the fragment ring.
        //
        FragmentIndex--;
        FragmentCount = XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount;
    }

    ReadLength =
        XdpGetContiguousHeaderLength(
            Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount,
            FragmentRing, VirtualAddressExtension, FrameStore->QuicStorage,
            ARRAYSIZE(FrameStore->QuicStorage), &QuicPayload);

    XdpParseQuicHeaderPayload(QuicPayload, ReadLength, FrameCache);
}

static
VOID
XdpParseQuicHeader(
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _In_ XDP_PROGRAM_PAYLOAD_CACHE *Payload,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *FrameStore,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *FrameCache
    )
{
    UINT32 BufferDataOffset = Payload->BufferDataOffset;
    XDP_BUFFER *Buffer = Payload->Buffer;
    UCHAR *Va = XdpGetVirtualAddressExtension(Payload->Buffer, VirtualAddressExtension)->VirtualAddress;
    Va += Buffer->DataOffset;

    FrameCache->QuicCached = TRUE;

    if (Buffer->DataLength < BufferDataOffset) {
        goto BufferTooSmall;
    }

    if (XdpParseQuicHeaderPayload(
            &Va[BufferDataOffset], Buffer->DataLength - BufferDataOffset, FrameCache)) {
        return;
    }

BufferTooSmall:

    if (FragmentRing != NULL) {
        ASSERT(FragmentExtension);
        XdpParseFragmentedQuicHeader(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
            FrameStore, FrameCache);
    }
}

static
BOOLEAN
XdpTestBit(
    _In_ CONST UINT8 *BitMap,
    _In_ UINT32 Index
    )
{
    return (ReadUCharNoFence(&BitMap[Index >> 3]) >> (Index & 0x7)) & 0x1;
}

static
XDP_RX_ACTION
XdpL2Fwd(
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    DL_EUI48 TempDlAddress;

    if (!Cache->EthCached) {
        XdpParseFrame(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
            Cache, Storage);
    }

    if (!Cache->EthValid) {
        return XDP_RX_ACTION_DROP;
    }

    TempDlAddress = Cache->EthHdr->Destination;
    Cache->EthHdr->Destination = Cache->EthHdr->Source;
    Cache->EthHdr->Source = TempDlAddress;

    if (Frame->Buffer.DataLength < sizeof(*Cache->EthHdr)) {
        ASSERT(FragmentRing != NULL);
        ASSERT(FragmentExtension != NULL);
        XdpCopyMemoryToFrame(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension, 0,
            Cache->EthHdr, sizeof(*Cache->EthHdr));
    }

    return XDP_RX_ACTION_TX;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspect(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_INSPECTION_CONTEXT *InspectionContext,
    _In_ XDP_RING *FrameRing,
    _In_ UINT32 FrameIndex,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    XDP_RX_ACTION Action = XDP_RX_ACTION_PASS;
    XDP_PROGRAM_FRAME_CACHE FrameCache;
    XDP_FRAME *Frame;
    BOOLEAN Matched = FALSE;

    ASSERT(FrameIndex <= FrameRing->Mask);
    ASSERT(
        (FragmentRing == NULL && FragmentIndex == 0) ||
        (FragmentRing && FragmentIndex <= FragmentRing->Mask));

    XdpInitializeFrameCache(&FrameCache);
    Frame = XdpRingGetElement(FrameRing, FrameIndex);

    for (ULONG RuleIndex = 0; RuleIndex < Program->RuleCount; RuleIndex++) {
        XDP_RULE *Rule = &Program->Rules[RuleIndex];

        //
        // Check the match conditions.
        //

        switch (Rule->Match) {
        case XDP_MATCH_ALL:
            Matched = TRUE;
            break;

        case XDP_MATCH_UDP:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_UDP_DST:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                FrameCache.UdpHdr->uh_dport == Rule->Pattern.Port) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_DST_MASK:
            if (!FrameCache.Ip4Cached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip4Valid &&
                Ipv4PrefixMatch(
                    &FrameCache.Ip4Hdr->DestinationAddress, &Rule->Pattern.IpMask.Address.Ipv4,
                    &Rule->Pattern.IpMask.Mask.Ipv4)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV6_DST_MASK:
            if (!FrameCache.Ip6Cached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip6Valid &&
                Ipv6PrefixMatch(
                    &FrameCache.Ip6Hdr->DestinationAddress,
                    &Rule->Pattern.IpMask.Address.Ipv6,
                    &Rule->Pattern.IpMask.Mask.Ipv6)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_QUIC_FLOW_SRC_CID:
        case XDP_MATCH_QUIC_FLOW_DST_CID:
            if (!FrameCache.UdpCached || !FrameCache.TransportPayloadCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }

            if (!FrameCache.UdpValid || !FrameCache.TransportPayloadValid ||
                FrameCache.UdpHdr->uh_dport != Rule->Pattern.QuicFlow.UdpPort) {
                break;
            }

            if (!FrameCache.QuicCached) {
                XdpParseQuicHeader(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache.TransportPayload, &Program->FrameStorage, &FrameCache);
            }

            if (FrameCache.QuicValid &&
                QuicCidMatch(
                    Rule->Match,
                    &FrameCache,
                    &Rule->Pattern.QuicFlow)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_UDP_TUPLE:
        case XDP_MATCH_IPV6_UDP_TUPLE:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                UdpTupleMatch(
                    Rule->Match,
                    &FrameCache,
                    &Rule->Pattern.Tuple)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_UDP_PORT_SET:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                XdpTestBit(Rule->Pattern.PortSet.PortSet, FrameCache.UdpHdr->uh_dport)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_UDP_PORT_SET:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip4Valid &&
                IN4_ADDR_EQUAL(
                    &FrameCache.Ip4Hdr->DestinationAddress,
                    &Rule->Pattern.IpPortSet.Address.Ipv4) &&
                FrameCache.UdpValid &&
                XdpTestBit(Rule->Pattern.IpPortSet.PortSet.PortSet, FrameCache.UdpHdr->uh_dport)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV6_UDP_PORT_SET:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip6Valid &&
                IN6_ADDR_EQUAL(
                    &FrameCache.Ip6Hdr->DestinationAddress,
                    &Rule->Pattern.IpPortSet.Address.Ipv6) &&
                FrameCache.UdpValid &&
                XdpTestBit(Rule->Pattern.IpPortSet.PortSet.PortSet, FrameCache.UdpHdr->uh_dport)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_TCP_PORT_SET:
            if (!FrameCache.TcpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip4Valid &&
                IN4_ADDR_EQUAL(
                    &FrameCache.Ip4Hdr->DestinationAddress,
                    &Rule->Pattern.IpPortSet.Address.Ipv4) &&
                FrameCache.TcpValid &&
                XdpTestBit(Rule->Pattern.IpPortSet.PortSet.PortSet, FrameCache.TcpHdr->th_dport)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV6_TCP_PORT_SET:
            if (!FrameCache.TcpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip6Valid &&
                IN6_ADDR_EQUAL(
                    &FrameCache.Ip6Hdr->DestinationAddress,
                    &Rule->Pattern.IpPortSet.Address.Ipv6) &&
                FrameCache.TcpValid &&
                XdpTestBit(Rule->Pattern.IpPortSet.PortSet.PortSet, FrameCache.TcpHdr->th_dport)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_TCP_DST:
            if (!FrameCache.TcpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.TcpValid &&
                FrameCache.TcpHdr->th_dport == Rule->Pattern.Port) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_TCP_QUIC_FLOW_SRC_CID:
        case XDP_MATCH_TCP_QUIC_FLOW_DST_CID:
            if (!FrameCache.TcpCached || !FrameCache.TransportPayloadCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }

            if (!FrameCache.TcpValid || !FrameCache.TransportPayloadValid ||
                FrameCache.TcpHdr->th_dport != Rule->Pattern.QuicFlow.UdpPort) {
                break;
            }

            if (!FrameCache.QuicCached) {
                XdpParseQuicHeader(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache.TransportPayload, &Program->FrameStorage, &FrameCache);
            }

            if (FrameCache.QuicValid &&
                QuicCidMatch(
                    Rule->Match,
                    &FrameCache,
                    &Rule->Pattern.QuicFlow)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_TCP_CONTROL_DST:
            if (!FrameCache.TcpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.TcpValid &&
                FrameCache.TcpHdr->th_dport == Rule->Pattern.Port &&
                (FrameCache.TcpHdr->th_flags & (TH_SYN | TH_FIN | TH_RST)) != 0) {
                Matched = TRUE;
            }
            break;

        default:
            ASSERT(FALSE);
            break;
        }

        if (Matched) {
            //
            // Apply the action.
            //
            switch (Rule->Action) {

            case XDP_PROGRAM_ACTION_REDIRECT:
                XdpRedirect(
                    &InspectionContext->RedirectContext, FrameIndex, FragmentIndex,
                    Rule->Redirect.TargetType, Rule->Redirect.Target);

                Action = XDP_RX_ACTION_DROP;
                break;

            case XDP_PROGRAM_ACTION_DROP:
                Action = XDP_RX_ACTION_DROP;
                break;

            case XDP_PROGRAM_ACTION_PASS:
                Action = XDP_RX_ACTION_PASS;
                break;

            case XDP_PROGRAM_ACTION_L2FWD:
                Action =
                    XdpL2Fwd(
                        Frame, FragmentRing, FragmentExtension, FragmentIndex,
                        VirtualAddressExtension, &FrameCache, &Program->FrameStorage);
                break;

            case XDP_PROGRAM_ACTION_EBPF:
                Action =
                    XdpInvokeEbpf(
                        Rule->Ebpf.Target, NULL, Frame, FragmentRing, FragmentExtension,
                        FragmentIndex, VirtualAddressExtension);
                break;

            default:
                ASSERT(FALSE);
                break;
            }

            break;
        }
    }

    return Action;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID *
XdpProgramGetXskBypassTarget(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    DBG_UNREFERENCED_PARAMETER(RxQueue);

    ASSERT(XdpProgramCanXskBypass(Program, RxQueue));
    return Program->Rules[0].Redirect.Target;
}

//
// Control path routines.
//
typedef struct _XDP_PROGRAM_OBJECT XDP_PROGRAM_OBJECT;

typedef struct _XDP_PROGRAM_BINDING {
    LIST_ENTRY Link;
    XDP_RX_QUEUE *RxQueue;
    LIST_ENTRY RxQueueEntry;
    XDP_RX_QUEUE_NOTIFICATION_ENTRY RxQueueNotificationEntry;
    XDP_PROGRAM_OBJECT *OwningProgram;
} XDP_PROGRAM_BINDING;

typedef struct _XDP_PROGRAM_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_BINDING_HANDLE IfHandle;
    LIST_ENTRY ProgramBindings;
    ULONG_PTR CreatedByPid;

    XDP_PROGRAM Program;
} XDP_PROGRAM_OBJECT;

typedef struct _XDP_PROGRAM_WORKITEM {
    XDP_BINDING_WORKITEM Bind;
    XDP_HOOK_ID HookId;
    UINT32 IfIndex;
    UINT32 QueueId;
    XDP_PROGRAM_OBJECT *ProgramObject;
    BOOLEAN BindToAllQueues;

    KEVENT CompletionEvent;
    NTSTATUS CompletionStatus;
} XDP_PROGRAM_WORKITEM;

static XDP_FILE_IRP_ROUTINE XdpIrpProgramClose;
static XDP_FILE_DISPATCH XdpProgramFileDispatch = {
    .Close = XdpIrpProgramClose,
};

static
VOID
XdpProgramTraceObject(
    _In_ CONST XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    TraceInfo(
        TRACE_CORE, "Program=%p CreatedByPid=%Iu",
        ProgramObject, ProgramObject->CreatedByPid);

    for (UINT32 i = 0; i < ProgramObject->Program.RuleCount; i++) {
        CONST XDP_RULE *Rule = &ProgramObject->Program.Rules[i];

        switch (Rule->Match) {
        case XDP_MATCH_ALL:
            TraceInfo(TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_ALL", ProgramObject, i);
            break;

        case XDP_MATCH_UDP:
            TraceInfo(TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_UDP", ProgramObject, i);
            break;

        case XDP_MATCH_UDP_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_UDP_DST Port=%u",
                ProgramObject, i, ntohs(Rule->Pattern.Port));
            break;

        case XDP_MATCH_IPV4_DST_MASK:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_DST_MASK Ip=%!IPADDR! Mask=%!IPADDR!",
                ProgramObject, i, Rule->Pattern.IpMask.Address.Ipv4.s_addr,
                Rule->Pattern.IpMask.Mask.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_DST_MASK:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_DST_MASK Ip=%!IPV6ADDR! Mask=%!IPV6ADDR!",
                ProgramObject, i, Rule->Pattern.IpMask.Address.Ipv6.u.Byte,
                Rule->Pattern.IpMask.Mask.Ipv6.u.Byte);
            break;

        case XDP_MATCH_QUIC_FLOW_SRC_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_QUIC_FLOW_SRC_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                ProgramObject, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_TCP_QUIC_FLOW_SRC_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_TCP_QUIC_FLOW_SRC_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                ProgramObject, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_QUIC_FLOW_DST_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_QUIC_FLOW_DST_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                ProgramObject, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_TCP_QUIC_FLOW_DST_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_TCP_QUIC_FLOW_DST_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                ProgramObject, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_IPV4_UDP_TUPLE:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_UDP_TUPLE "
                "Source=%!IPADDR!:%u Destination=%!IPADDR!:%u",
                ProgramObject, i, Rule->Pattern.Tuple.SourceAddress.Ipv4.s_addr,
                ntohs(Rule->Pattern.Tuple.SourcePort),
                Rule->Pattern.Tuple.DestinationAddress.Ipv4.s_addr,
                ntohs(Rule->Pattern.Tuple.DestinationPort));
            break;

        case XDP_MATCH_IPV6_UDP_TUPLE:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_UDP_TUPLE "
                "Source=[%!IPV6ADDR!]:%u Destination=[%!IPV6ADDR!]:%u",
                ProgramObject, i, Rule->Pattern.Tuple.SourceAddress.Ipv6.u.Byte,
                ntohs(Rule->Pattern.Tuple.SourcePort),
                Rule->Pattern.Tuple.DestinationAddress.Ipv6.u.Byte,
                ntohs(Rule->Pattern.Tuple.DestinationPort));
            break;

        case XDP_MATCH_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_UDP_PORT_SET PortSet=?",
                ProgramObject, i);
            break;

        case XDP_MATCH_IPV4_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_UDP_PORT_SET "
                "Destination=%!IPADDR! PortSet=?",
                ProgramObject, i, Rule->Pattern.IpPortSet.Address.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_UDP_PORT_SET "
                "Destination=%!IPV6ADDR! PortSet=?",
                ProgramObject, i, Rule->Pattern.IpPortSet.Address.Ipv6.u.Byte);
            break;

        case XDP_MATCH_IPV4_TCP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_TCP_PORT_SET "
                "Destination=%!IPADDR! PortSet=?",
                ProgramObject, i, Rule->Pattern.IpPortSet.Address.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_TCP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_TCP_PORT_SET "
                "Destination=%!IPV6ADDR! PortSet=?",
                ProgramObject, i, Rule->Pattern.IpPortSet.Address.Ipv6.u.Byte);
            break;

        case XDP_MATCH_TCP_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_TCP_DST Port=%u",
                ProgramObject, i, ntohs(Rule->Pattern.Port));
            break;

        case XDP_MATCH_TCP_CONTROL_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_TCP_CONTROL_DST Port=%u",
                ProgramObject, i, ntohs(Rule->Pattern.Port));
            break;

        default:
            ASSERT(FALSE);
            break;
        }

        switch (Rule->Action) {
        case XDP_PROGRAM_ACTION_DROP:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_DROP",
                ProgramObject, i);
            break;

        case XDP_PROGRAM_ACTION_PASS:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_PASS",
                ProgramObject, i);
            break;

        case XDP_PROGRAM_ACTION_REDIRECT:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_REDIRECT "
                "TargetType=%!REDIRECT_TARGET_TYPE! Target=%p",
                ProgramObject, i, Rule->Redirect.TargetType, Rule->Redirect.Target);
            break;

        case XDP_PROGRAM_ACTION_L2FWD:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_L2FWD",
                ProgramObject, i);
            break;

        default:
            ASSERT(FALSE);
            break;
        }
    }
}

static const ebpf_context_descriptor_t EbpfXdpContextDescriptor = {
    .size = sizeof(xdp_md_t),
    .data = FIELD_OFFSET(xdp_md_t, data),
    .end = FIELD_OFFSET(xdp_md_t, data_end),
    .meta = FIELD_OFFSET(xdp_md_t, data_meta),
};

#define XDP_EXT_HELPER_FUNCTION_START EBPF_MAX_GENERAL_HELPER_FUNCTION

// XDP helper function prototype descriptors.
static const ebpf_helper_function_prototype_t EbpfXdpHelperFunctionPrototype[] = {
    {
        .helper_id = XDP_EXT_HELPER_FUNCTION_START + 1,
        .name = "bpf_xdp_adjust_head",
        .return_type = EBPF_RETURN_TYPE_INTEGER,
        .arguments = {
            EBPF_ARGUMENT_TYPE_PTR_TO_CTX,
            EBPF_ARGUMENT_TYPE_ANYTHING,
        },
    },
};

#pragma warning(suppress:4090) // 'initializing': different 'const' qualifiers
static const ebpf_program_info_t EbpfXdpProgramInfo = {
#pragma warning(suppress:4090) // 'initializing': different 'const' qualifiers
    .program_type_descriptor = {
        .name = "xdp",
        .context_descriptor = &EbpfXdpContextDescriptor,
        .program_type = EBPF_PROGRAM_TYPE_XDP_INIT,
        BPF_PROG_TYPE_XDP,
    },
    .count_of_program_type_specific_helpers = RTL_NUMBER_OF(EbpfXdpHelperFunctionPrototype),
    .program_type_specific_helper_prototype = EbpfXdpHelperFunctionPrototype,
};

static
int
EbpfXdpAdjustHead(
    _Inout_ xdp_md_t *Context,
    _In_ int Delta
    )
{
    //
    // Not implemented. Any return < 0 is an error.
    //
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Delta);
    return -1;
}

static const VOID *EbpfXdpHelperFunctions[] = {
    (VOID *)EbpfXdpAdjustHead,
};

static const ebpf_helper_function_addresses_t XdpHelperFunctionAddresses = {
    .helper_function_count = RTL_NUMBER_OF(EbpfXdpHelperFunctions),
    .helper_function_address = (UINT64 *)EbpfXdpHelperFunctions
};

#pragma warning(suppress:4090) // 'initializing': different 'const' qualifiers
static const ebpf_program_data_t EbpfXdpProgramData = {
    .program_info = &EbpfXdpProgramInfo,
    .program_type_specific_helper_function_addresses = &XdpHelperFunctionAddresses,
    .required_irql = DISPATCH_LEVEL,
};

#pragma warning(suppress:4090) // 'initializing': different 'const' qualifiers
static const ebpf_extension_data_t EbpfXdpProgramInfoProviderData = {
    .version = 0, // Review: versioning?
    .size = sizeof(EbpfXdpProgramData),
    .data = &EbpfXdpProgramData,
};

static const NPI_MODULEID EbpfXdpProgramInfoProviderModuleId = {
    .Length = sizeof(NPI_MODULEID),
    .Type = MIT_GUID,
    .Guid = EBPF_PROGRAM_TYPE_XDP_INIT,
};

static const ebpf_attach_provider_data_t EbpfXdpHookAttachProviderData = {
    .supported_program_type = EBPF_PROGRAM_TYPE_XDP_INIT,
    .bpf_attach_type = BPF_XDP,
    .link_type = BPF_LINK_TYPE_XDP,
};

#pragma warning(suppress:4090) // 'initializing': different 'const' qualifiers
static const ebpf_extension_data_t EbpfXdpHookProviderData = {
    .version = EBPF_ATTACH_PROVIDER_DATA_VERSION,
    .size = sizeof(EbpfXdpHookAttachProviderData),
    .data = &EbpfXdpHookAttachProviderData,
};

static const NPI_MODULEID EbpfXdpHookProviderModuleId = {
    .Length = sizeof(NPI_MODULEID),
    .Type = MIT_GUID,
    .Guid = EBPF_ATTACH_TYPE_XDP_INIT,
};

static EBPF_EXTENSION_PROVIDER *EbpfXdpProgramInfoProvider;
static EBPF_EXTENSION_PROVIDER *EbpfXdpProgramHookProvider;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpProgramUpdateCompiledProgram(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    LIST_ENTRY *BindingListHead = XdpRxQueueGetProgramBindingList(RxQueue);
    XDP_PROGRAM *Program = XdpRxQueueGetProgram(RxQueue);
    LIST_ENTRY *Entry = BindingListHead->Flink;
    UINT32 RuleIndex = 0;

    TraceEnter(TRACE_CORE, "Updating program on RxQueue=%p", RxQueue);

    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING* ProgramBinding =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        CONST XDP_PROGRAM_OBJECT *BoundProgramObject = ProgramBinding->OwningProgram;

        for (UINT32 i = 0; i < BoundProgramObject->Program.RuleCount; i++) {
            Program->Rules[RuleIndex++] = BoundProgramObject->Program.Rules[i];
        }

        TraceInfo(TRACE_CORE, "Updated ProgramObject=%p", BoundProgramObject);
        XdpProgramTraceObject(BoundProgramObject);
        Entry = Entry->Flink;
    }

    //
    // If the program compiled for this newly added binding failed to be added
    // to the RX queue, we will end up having Program->RuleCount == RuleIndex.
    //
    ASSERT(Program->RuleCount >= RuleIndex);
    Program->RuleCount = RuleIndex;
    TraceExitSuccess(TRACE_CORE);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
XdpProgramCompileNewProgram(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Out_ XDP_PROGRAM **Program
    )
{
    NTSTATUS Status;
    LIST_ENTRY *BindingListHead = XdpRxQueueGetProgramBindingList(RxQueue);
    LIST_ENTRY *Entry = BindingListHead->Flink;
    UINT32 RuleCount = 0;
    XDP_PROGRAM *NewProgram;
    SIZE_T AllocationSize;

    TraceEnter(TRACE_CORE, "Compiling new program on RxQueue=%p", RxQueue);

    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING* ProgramBinding = CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        Status =
            RtlUInt32Add(
                RuleCount, ProgramBinding->OwningProgram->Program.RuleCount, &RuleCount);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        Entry = Entry->Flink;
    }

    if (RuleCount == 0) {
        //
        // No program bindings on the RX queue.
        //
        *Program = NULL;
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTAdd(sizeof(*NewProgram), AllocationSize, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    NewProgram = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
    if (NewProgram == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Entry = BindingListHead->Flink;
    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING* ProgramBinding =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        CONST XDP_PROGRAM_OBJECT *BoundProgramObject = ProgramBinding->OwningProgram;

        for (UINT32 i = 0; i < BoundProgramObject->Program.RuleCount; i++) {
            NewProgram->Rules[NewProgram->RuleCount++] = BoundProgramObject->Program.Rules[i];
        }

        Entry = Entry->Flink;
    }

    ASSERT(NewProgram->RuleCount == RuleCount);

    *Program = NewProgram;

Exit:
    TraceExitSuccess(TRACE_CORE);
    return Status;
}

static
VOID
XdpProgramDetachRxQueue(
    _In_ XDP_PROGRAM_BINDING *ProgramBinding
    )
{
    XDP_RX_QUEUE *RxQueue = ProgramBinding->RxQueue;

    TraceEnter(
        TRACE_CORE,
        "Detach ProgramBinding=%p on ProgramObject=%p from RxQueue=%p",
        ProgramBinding, ProgramBinding->OwningProgram, ProgramBinding->RxQueue);

    ASSERT(RxQueue != NULL);
    ASSERT(!IsListEmpty(&ProgramBinding->RxQueueEntry));

    //
    // Remove the binding from the RX queue and recompile bound programs.
    //
    RemoveEntryList(&ProgramBinding->RxQueueEntry);
    InitializeListHead(&ProgramBinding->RxQueueEntry);

    XdpRxQueueDeregisterNotifications(RxQueue, &ProgramBinding->RxQueueNotificationEntry);
    if (IsListEmpty(XdpRxQueueGetProgramBindingList(ProgramBinding->RxQueue))) {
        XDP_PROGRAM *OldCompiledProgram = XdpRxQueueGetProgram(RxQueue);
        XdpRxQueueSetProgram(RxQueue, NULL, NULL, NULL);
        if (OldCompiledProgram != NULL) {
            ExFreePoolWithTag(OldCompiledProgram, XDP_POOLTAG_PROGRAM);
        }
    } else {
        //
        // Update the program in-place because we are down sizing the program bindings.
        //
        XdpRxQueueSync(RxQueue, XdpProgramUpdateCompiledProgram, RxQueue);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpProgramReleasePortSet(
    _Inout_ XDP_PORT_SET *PortSet
    )
{
    PortSet->PortSet = NULL;

    if (PortSet->Reserved != NULL) {
        MDL *Mdl = PortSet->Reserved;
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED) {
            MmUnlockPages(Mdl);
        }
        IoFreeMdl(Mdl);
        PortSet->Reserved = NULL;
    }
}

static
NTSTATUS
XdpProgramCapturePortSet(
    _In_ CONST XDP_PORT_SET *UserPortSet,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Inout_ XDP_PORT_SET *KernelPortSet
    )
{
    NTSTATUS Status;

    __try {
        if (UserPortSet->Reserved != NULL) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        KernelPortSet->Reserved =
            IoAllocateMdl(UserPortSet->PortSet, XDP_PORT_SET_BUFFER_SIZE, FALSE, FALSE, NULL);
        if (KernelPortSet->Reserved == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        MmProbeAndLockPages(KernelPortSet->Reserved, RequestorMode, IoReadAccess);

        KernelPortSet->PortSet =
            MmGetSystemAddressForMdlSafe(KernelPortSet->Reserved, LowPagePriority);
        if (KernelPortSet->PortSet == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        Status = STATUS_SUCCESS;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramReleasePortSet(KernelPortSet);
    }

    return Status;
}

static
VOID
XdpProgramDelete(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    while (!IsListEmpty(&ProgramObject->ProgramBindings)) {
        XDP_PROGRAM_BINDING *ProgramBinding =
            (XDP_PROGRAM_BINDING *)ProgramObject->ProgramBindings.Flink;

        //
        // Detach the XDP program from the RX queue.
        // The binding might have already been detached during interface tear-down.
        //
        if (!IsListEmpty(&ProgramBinding->RxQueueEntry)) {
            XdpProgramDetachRxQueue(ProgramBinding);
        }

        if (ProgramBinding->RxQueue != NULL) {
            XdpRxQueueDereference(ProgramBinding->RxQueue);
        }

        RemoveEntryList(&ProgramBinding->Link);

        TraceInfo(
            TRACE_CORE, "Deleted ProgramBinding %p", ProgramBinding);
        ExFreePoolWithTag(ProgramBinding, XDP_POOLTAG_PROGRAM_BINDING);
    }

    //
    // Clean up the XDP program after data path references are dropped.
    //

    for (ULONG Index = 0; Index < ProgramObject->Program.RuleCount; Index++) {
        XDP_RULE *Rule = &ProgramObject->Program.Rules[Index];

        if (Rule->Match == XDP_MATCH_IPV4_UDP_PORT_SET ||
            Rule->Match == XDP_MATCH_IPV6_UDP_PORT_SET ||
            Rule->Match == XDP_MATCH_IPV4_TCP_PORT_SET ||
            Rule->Match == XDP_MATCH_IPV6_TCP_PORT_SET) {
            XdpProgramReleasePortSet(&Rule->Pattern.IpPortSet.PortSet);
        }

        if (Rule->Match == XDP_MATCH_UDP_PORT_SET) {
            XdpProgramReleasePortSet(&Rule->Pattern.PortSet);
        }

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                if (Rule->Redirect.Target != NULL) {
                    XskDereferenceDatapathHandle(Rule->Redirect.Target);
                }
                break;

            default:
                ASSERT(FALSE);
            }
        }
    }

    TraceVerbose(TRACE_CORE, "Deleted ProgramObject=%p", ProgramObject);
    ExFreePoolWithTag(ProgramObject, XDP_POOLTAG_PROGRAM_OBJECT);
    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpProgramRxQueueNotify(
    XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XDP_PROGRAM_BINDING *ProgramBinding =
        CONTAINING_RECORD(NotificationEntry, XDP_PROGRAM_BINDING, RxQueueNotificationEntry);

    switch (NotificationType) {

    case XDP_RX_QUEUE_NOTIFICATION_DELETE:
        XdpProgramDetachRxQueue(ProgramBinding);
        break;

    }
}

BOOLEAN
XdpProgramIsEbpf(
    _In_ XDP_PROGRAM *Program
    )
{
    return Program->RuleCount == 1 && Program->Rules[0].Action == XDP_PROGRAM_ACTION_EBPF;
}

BOOLEAN
XdpProgramCanXskBypass(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    return
        Program->RuleCount == 1 &&
        Program->Rules[0].Match == XDP_MATCH_ALL &&
        Program->Rules[0].Action == XDP_PROGRAM_ACTION_REDIRECT &&
        Program->Rules[0].Redirect.TargetType == XDP_REDIRECT_TARGET_TYPE_XSK &&
        XskCanBypass(Program->Rules[0].Redirect.Target, RxQueue);
}

static
NTSTATUS
XdpProgramObjectAllocate(
    _In_ UINT32 RuleCount,
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    SIZE_T AllocationSize;
    NTSTATUS Status;

    Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTAdd(sizeof(*ProgramObject), AllocationSize, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ProgramObject = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM_OBJECT);
    if (ProgramObject == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ProgramObject->CreatedByPid = (ULONG_PTR)PsGetCurrentProcessId();
    InitializeListHead(&ProgramObject->ProgramBindings);

Exit:

    *NewProgramObject = ProgramObject;
    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpCaptureProgram(
    _In_ CONST XDP_RULE *Rules,
    _In_ ULONG RuleCount,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject
    )
{
    NTSTATUS Status;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    XDP_PROGRAM *Program = NULL;

    TraceEnter(TRACE_CORE, "-");

    Status = XdpProgramObjectAllocate(RuleCount, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    TraceVerbose(TRACE_CORE, "Allocated Program=%p", ProgramObject);

    Program = &ProgramObject->Program;

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)Rules, sizeof(*Rules) * RuleCount, PROBE_ALIGNMENT(XDP_RULE));
        }
        RtlCopyMemory(ProgramObject->Program.Rules, Rules, sizeof(*Rules) * RuleCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    for (ULONG Index = 0; Index < RuleCount; Index++) {
        XDP_RULE UserRule = Program->Rules[Index];
        XDP_RULE *ValidatedRule = &Program->Rules[Index];

        //
        // Initialize the trusted kernel rule buffer and increment the count of
        // validated rules. The error path will not attempt to clean up
        // unvalidated rules.
        //
        RtlZeroMemory(ValidatedRule, sizeof(*ValidatedRule));
        Program->RuleCount++;

        if (UserRule.Match < XDP_MATCH_ALL || UserRule.Match > XDP_MATCH_TCP_CONTROL_DST) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        ValidatedRule->Match = UserRule.Match;

        //
        // Validate each match condition. Many match conditions support all
        // possible input pattern values.
        //
        switch (ValidatedRule->Match) {
        case XDP_MATCH_QUIC_FLOW_SRC_CID:
        case XDP_MATCH_QUIC_FLOW_DST_CID:
        case XDP_MATCH_TCP_QUIC_FLOW_SRC_CID:
        case XDP_MATCH_TCP_QUIC_FLOW_DST_CID:
            if (UserRule.Pattern.QuicFlow.CidLength > RTL_FIELD_SIZE(XDP_QUIC_FLOW, CidData)) {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            ValidatedRule->Pattern.QuicFlow = UserRule.Pattern.QuicFlow;
            break;
        case XDP_MATCH_UDP_PORT_SET:
            Status =
                XdpProgramCapturePortSet(
                    &UserRule.Pattern.PortSet, RequestorMode, &ValidatedRule->Pattern.PortSet);
            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }
            break;
        case XDP_MATCH_IPV4_UDP_PORT_SET:
        case XDP_MATCH_IPV6_UDP_PORT_SET:
        case XDP_MATCH_IPV4_TCP_PORT_SET:
        case XDP_MATCH_IPV6_TCP_PORT_SET:
            Status =
                XdpProgramCapturePortSet(
                    &UserRule.Pattern.IpPortSet.PortSet, RequestorMode,
                    &ValidatedRule->Pattern.IpPortSet.PortSet);
            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }
            ValidatedRule->Pattern.IpPortSet.Address = UserRule.Pattern.IpPortSet.Address;
            break;
        default:
            ValidatedRule->Pattern = UserRule.Pattern;
            break;
        }

        if (UserRule.Action < XDP_PROGRAM_ACTION_DROP ||
            UserRule.Action > XDP_PROGRAM_ACTION_EBPF) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        ValidatedRule->Action = UserRule.Action;

        //
        // Capture object handle references in the context of the calling thread.
        // The handle will be validated further on the control path.
        //
        switch (UserRule.Action) {
        case XDP_PROGRAM_ACTION_REDIRECT:
            switch (UserRule.Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status =
                    XskReferenceDatapathHandle(
                        RequestorMode, &UserRule.Redirect.Target, TRUE,
                        &ValidatedRule->Redirect.Target);
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }

            break;

        case XDP_PROGRAM_ACTION_EBPF:
            if (RequestorMode != KernelMode) {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }

            //
            // eBPF programs must be the sole, unconditional action.
            //
            if (RuleCount != 1 || ValidatedRule->Match != XDP_MATCH_ALL) {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }

            ASSERT(Index == 0);
            ValidatedRule->Ebpf.Target = UserRule.Ebpf.Target;

            break;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    if (NT_SUCCESS(Status)) {
        ASSERT(ProgramObject != NULL);
        *NewProgramObject = ProgramObject;
    } else {
        if (ProgramObject != NULL) {
            XdpProgramDelete(ProgramObject);
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
XdpProgramValidateIfQueue(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_opt_ VOID *ValidationContext
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = ValidationContext;
    XDP_PROGRAM *Program = &ProgramObject->Program;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "Program=%p", ProgramObject);

    //
    // Perform further rule validation that requires an interface RX queue.
    //

    //
    // Since we don't know what an eBPF program will return, assume it will
    // return all statuses.
    //
    if (XdpProgramIsEbpf(Program) && !XdpRxQueueIsTxActionSupported(XdpRxQueueGetConfig(RxQueue))) {
        TraceError(
            TRACE_CORE, "Program=%p RX queue does not support TX action", ProgramObject);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_L2FWD) {
            if (!XdpRxQueueIsTxActionSupported(XdpRxQueueGetConfig(RxQueue))) {
                TraceError(
                    TRACE_CORE, "Program=%p RX queue does not support TX action", ProgramObject);
                Status = STATUS_NOT_SUPPORTED;
                goto Exit;
            }
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

static
NTSTATUS
XdpProgramBindingAllocate(
    _Out_ XDP_PROGRAM_BINDING **NewProgramBinding,
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    XDP_PROGRAM_BINDING *ProgramBinding = NULL;
    NTSTATUS Status;

    ProgramBinding =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*ProgramBinding), XDP_POOLTAG_PROGRAM_BINDING);
    if (ProgramBinding == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    InitializeListHead(&ProgramBinding->RxQueueEntry);
    InitializeListHead(&ProgramBinding->Link);
    XdpRxQueueInitializeNotificationEntry(&ProgramBinding->RxQueueNotificationEntry);

    ProgramBinding->OwningProgram = ProgramObject;
    InsertTailList(&ProgramObject->ProgramBindings, &ProgramBinding->Link);

    Status = STATUS_SUCCESS;

Exit:
    *NewProgramBinding = ProgramBinding;
    return Status;
}

static
NTSTATUS
XdpProgramBindingAttach(
    _In_ XDP_BINDING_HANDLE XdpBinding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Inout_ XDP_PROGRAM_OBJECT *ProgramObject,
    _In_ UINT32 QueueId
    )
{
    XDP_PROGRAM *Program = &ProgramObject->Program;
    XDP_PROGRAM_BINDING* ProgramBinding = NULL;
    XDP_PROGRAM *CompiledProgram = NULL;
    NTSTATUS Status;

    Status = XdpProgramBindingAllocate(&ProgramBinding, ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpRxQueueFindOrCreate(
            XdpBinding, HookId, QueueId, &ProgramBinding->RxQueue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status = XskValidateDatapathHandle(Rule->Redirect.Target);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }

                break;

            default:
                break;
            }
        }
    }

    XDP_PROGRAM *OldCompiledProgram = XdpRxQueueGetProgram(ProgramBinding->RxQueue);

    //
    // Do not allow eBPF programs to be replaced or to replace existing
    // programs.
    //
    if (OldCompiledProgram != NULL &&
        (XdpProgramIsEbpf(OldCompiledProgram) || XdpProgramIsEbpf(Program))) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    InsertTailList(
        XdpRxQueueGetProgramBindingList(ProgramBinding->RxQueue), &ProgramBinding->RxQueueEntry);
    Status = XdpProgramCompileNewProgram(ProgramBinding->RxQueue, &CompiledProgram);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Register for interface/queue removal notifications.
    //
    XdpRxQueueRegisterNotifications(
        ProgramBinding->RxQueue, &ProgramBinding->RxQueueNotificationEntry, XdpProgramRxQueueNotify);

    ASSERT(
        !IsListEmpty(&ProgramBinding->RxQueueEntry) &&
        !IsListEmpty(&ProgramBinding->Link));

    Status =
        XdpRxQueueSetProgram(
            ProgramBinding->RxQueue, CompiledProgram, XdpProgramValidateIfQueue,
            ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    CompiledProgram = NULL;

    if (OldCompiledProgram != NULL) {
        //
        // We just swapped in a new compiled program. Delete the old one.
        //
        ExFreePoolWithTag(OldCompiledProgram, XDP_POOLTAG_PROGRAM);
        OldCompiledProgram = NULL;
    }

    TraceInfo(
        TRACE_CORE, "Attached ProgramBinding %p RxQueue=%p ProgramObject=%p",
        ProgramBinding, ProgramBinding->RxQueue, ProgramObject);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (CompiledProgram != NULL) {
            ExFreePoolWithTag(CompiledProgram, XDP_POOLTAG_PROGRAM);
        }
    }

    return Status;
}

static
VOID
XdpProgramAttach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;
    XDP_PROGRAM_OBJECT *ProgramObject = Item->ProgramObject;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    UINT32 QueueIdStart = Item->QueueId;
    UINT32 QueueIdEnd = Item->QueueId + 1;
    XDP_IFSET_HANDLE IfSetHandle = NULL;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    if (Item->HookId.SubLayer != XDP_HOOK_INSPECT) {
        //
        // Only RX queue programs are currently supported.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (Item->BindToAllQueues) {
        VOID *InterfaceOffloadHandle;
        XDP_RSS_CAPABILITIES RssCapabilities;
        UINT32 RssCapabilitiesSize = sizeof(RssCapabilities);
        IfSetHandle = XdpIfFindAndReferenceIfSet(Item->IfIndex, &Item->HookId, 1, NULL);
        if (IfSetHandle == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        Status =
            XdpIfOpenInterfaceOffloadHandle(
                IfSetHandle, &Item->HookId, &InterfaceOffloadHandle);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            XdpIfGetInterfaceOffloadCapabilities(
                IfSetHandle, InterfaceOffloadHandle,
                XdpOffloadRss, &RssCapabilities, &RssCapabilitiesSize);
        XdpIfCloseInterfaceOffloadHandle(IfSetHandle, InterfaceOffloadHandle);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        TraceInfo(
            TRACE_CORE, "Attaching ProgramObject=%p to all %u queues",
            ProgramObject, RssCapabilities.NumberOfReceiveQueues);
        QueueIdStart = 0;
        QueueIdEnd = RssCapabilities.NumberOfReceiveQueues;
    }

    for (UINT32 QueueId = QueueIdStart; QueueId < QueueIdEnd; ++QueueId) {
        Status =
            XdpProgramBindingAttach(
                Item->Bind.BindingHandle, &Item->HookId, ProgramObject, QueueId);
        if (!NT_SUCCESS(Status)) {
            //
            // Failed to attach to one of the RX queues.
            //
            // TODO: should we allow it to succeed partially?
            //
            goto Exit;
        }
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramDelete(ProgramObject);
    }

    if (IfSetHandle != NULL) {
        XdpIfDereferenceIfSet(IfSetHandle);
    }

    Item->CompletionStatus = Status;
    KeSetEvent(&Item->CompletionEvent, 0, FALSE);

    TraceExitStatus(TRACE_CORE);
}

static
VOID
XdpProgramDetach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;

    TraceEnter(TRACE_CORE, "Program=%p", Item->ProgramObject);

    XdpIfDereferenceBinding(Item->ProgramObject->IfHandle);
    XdpProgramDelete(Item->ProgramObject);
    TraceInfo(TRACE_CORE, "Detached Program=%p", Item->ProgramObject);

    Item->CompletionStatus = STATUS_SUCCESS;
    KeSetEvent(&Item->CompletionEvent, 0, FALSE);

    TraceExitSuccess(TRACE_CORE);
}

static
NTSTATUS
XdpProgramCreate(
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject,
    _In_ const XDP_PROGRAM_OPEN *Params,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    XDP_INTERFACE_MODE InterfaceMode;
    XDP_INTERFACE_MODE *RequiredMode = NULL;
    XDP_BINDING_HANDLE BindingHandle = NULL;
    XDP_PROGRAM_WORKITEM WorkItem = {0};
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;
    CONST UINT32 ValidFlags =
        XDP_CREATE_PROGRAM_FLAG_GENERIC |
        XDP_CREATE_PROGRAM_FLAG_NATIVE |
        XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;

    TraceEnter(
        TRACE_CORE,
        "IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x",
        Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction, Params->HookId.SubLayer,
        Params->QueueId, Params->Flags);

    if ((Params->Flags & ~ValidFlags) ||
        !RTL_IS_CLEAR_OR_SINGLE_FLAG(
            Params->Flags, XDP_CREATE_PROGRAM_FLAG_GENERIC | XDP_CREATE_PROGRAM_FLAG_NATIVE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Params->Flags & XDP_CREATE_PROGRAM_FLAG_GENERIC) {
        InterfaceMode = XDP_INTERFACE_MODE_GENERIC;
        RequiredMode = &InterfaceMode;
    }

    if (Params->Flags & XDP_CREATE_PROGRAM_FLAG_NATIVE) {
        InterfaceMode = XDP_INTERFACE_MODE_NATIVE;
        RequiredMode = &InterfaceMode;
    }

RetryBinding:

    BindingHandle = XdpIfFindAndReferenceBinding(Params->IfIndex, &Params->HookId, 1, RequiredMode);
    if (BindingHandle == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status =
        XdpCaptureProgram(Params->Rules, Params->RuleCount, RequestorMode, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.QueueId = Params->QueueId;
    WorkItem.HookId = Params->HookId;
    WorkItem.IfIndex = Params->IfIndex;
    WorkItem.BindToAllQueues = !!(Params->Flags & XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES);
    WorkItem.ProgramObject = ProgramObject;
    WorkItem.Bind.BindingHandle = BindingHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramAttach;

    //
    // Attach the program using the inteface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);

    Status = WorkItem.CompletionStatus;

    if (!NT_SUCCESS(Status) &&
        XdpIfGetCapabilities(BindingHandle)->Mode == XDP_INTERFACE_MODE_NATIVE &&
        RequiredMode == NULL) {
        //
        // The program failed to attach to the native interface. Since the
        // application did not require native mode, attempt to fall back to
        // generic mode.
        //
        TraceInfo(
            TRACE_CORE,
            "IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Status=%!STATUS! native mode failed, trying generic mode",
            Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction,
            Params->HookId.SubLayer, Params->QueueId, Status);

        ProgramObject = NULL;
        XdpIfDereferenceBinding(BindingHandle);
        BindingHandle = NULL;

        InterfaceMode = XDP_INTERFACE_MODE_GENERIC;
        RequiredMode = &InterfaceMode;
        goto RetryBinding;
    }

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->IfHandle = BindingHandle, BindingHandle = NULL;
        *NewProgramObject = ProgramObject;
    }

    if (BindingHandle != NULL) {
        XdpIfDereferenceBinding(BindingHandle);
    }

    TraceInfo(
        TRACE_CORE,
        "Program=%p IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x Status=%!STATUS!",
        ProgramObject, Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction,
        Params->HookId.SubLayer, Params->QueueId, Params->Flags, Status);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateProgram(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    CONST XDP_PROGRAM_OPEN *Params = NULL;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "Irp=%p", Irp);

    if (Disposition != FILE_CREATE || InputBufferLength < sizeof(*Params)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Params = InputBuffer;

    Status = XdpProgramCreate(&ProgramObject, Params, Irp->RequestorMode);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->Header.ObjectType = XDP_OBJECT_TYPE_PROGRAM;
        ProgramObject->Header.Dispatch = &XdpProgramFileDispatch;
        IrpSp->FileObject->FsContext = ProgramObject;
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
VOID
XdpProgramClose(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    XDP_PROGRAM_WORKITEM WorkItem = {0};

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);
    TraceInfo(TRACE_CORE, "Closing ProgramObject=%p", ProgramObject);

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.ProgramObject = ProgramObject;
    WorkItem.Bind.BindingHandle = ProgramObject->IfHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramDetach;

    //
    // Perform the detach on the interface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(NT_SUCCESS(WorkItem.CompletionStatus));

    TraceExitSuccess(TRACE_CORE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpProgramClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = IrpSp->FileObject->FsContext;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    UNREFERENCED_PARAMETER(Irp);

    XdpProgramClose(ProgramObject);

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}

static
NTSTATUS
EbpfProgramOnClientAttach(
    _In_ const EBPF_EXTENSION_CLIENT *AttachingClient,
    _In_ const EBPF_EXTENSION_PROVIDER *AttachingProvider
    )
{
    NTSTATUS Status;
    const ebpf_extension_data_t *ClientData = EbpfExtensionClientGetClientData(AttachingClient);
    const ebpf_extension_dispatch_table_t *ClientDispatch =
        EbpfExtensionClientGetDispatch(AttachingClient);
    UINT32 IfIndex;
    XDP_PROGRAM_OPEN OpenParams = {0};
    XDP_RULE XdpRule = {0};
    XDP_PROGRAM_OBJECT *ProgramObject;
    ULONG RequiredMode;

    TraceEnter(
        TRACE_CORE, "AttachingProvider=%p AttachingClient=%p", AttachingProvider, AttachingClient);

    if (ClientData == NULL || ClientData->size != sizeof(IfIndex) || ClientData->data == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    IfIndex = *(UINT32 *)ClientData->data;

    if (IfIndex == IFI_UNSPECIFIED) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (ClientDispatch->version < 1 || ClientDispatch->size < 4) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    OpenParams.IfIndex = IfIndex;
    OpenParams.HookId.Layer = XDP_HOOK_L2;
    OpenParams.HookId.Direction = XDP_HOOK_RX;
    OpenParams.HookId.SubLayer = XDP_HOOK_INSPECT;
    OpenParams.Flags = XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;
    OpenParams.RuleCount = 1;
    OpenParams.Rules = &XdpRule;

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpEbpfMode", &RequiredMode);
    if (NT_SUCCESS(Status)) {
        switch (RequiredMode) {
        case XDP_INTERFACE_MODE_GENERIC:
            OpenParams.Flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
            break;

        case XDP_INTERFACE_MODE_NATIVE:
            OpenParams.Flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
    }

    XdpRule.Match = XDP_MATCH_ALL;
    XdpRule.Action = XDP_PROGRAM_ACTION_EBPF;
    XdpRule.Ebpf.Target = (HANDLE)AttachingClient;

    Status = XdpProgramCreate(&ProgramObject, &OpenParams, KernelMode);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    EbpfExtensionClientSetProviderData(AttachingClient, ProgramObject);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
EbpfProgramOnClientDetach(
    _In_ const EBPF_EXTENSION_CLIENT *DetachingClient
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = EbpfExtensionClientGetProviderData(DetachingClient);

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    ASSERT(ProgramObject != NULL);

    XdpProgramClose(ProgramObject);

    TraceExitSuccess(TRACE_CORE);
    return STATUS_SUCCESS;
}

NTSTATUS
XdpProgramStart(
    VOID
    )
{
    const EBPF_EXTENSION_PROVIDER_PARAMETERS EbpfProgramInfoProviderParameters = {
        .ProviderModuleId = &EbpfXdpProgramInfoProviderModuleId,
        .ProviderData = &EbpfXdpProgramInfoProviderData,
    };
    const EBPF_EXTENSION_PROVIDER_PARAMETERS EbpfHookProviderParameters = {
        .ProviderModuleId = &EbpfXdpHookProviderModuleId,
        .ProviderData = &EbpfXdpHookProviderData,
    };
    DWORD EbpfEnabled;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "-");

    //
    // eBPF is disabled by default while reliability bugs are outstanding.
    //

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpEbpfEnabled", &EbpfEnabled);
    if (NT_SUCCESS(Status) && EbpfEnabled) {
        Status =
            EbpfExtensionProviderRegister(
                &EBPF_PROGRAM_INFO_EXTENSION_IID, &EbpfProgramInfoProviderParameters, NULL, NULL, NULL,
                &EbpfXdpProgramInfoProvider);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            EbpfExtensionProviderRegister(
                &EBPF_HOOK_EXTENSION_IID, &EbpfHookProviderParameters, EbpfProgramOnClientAttach,
                EbpfProgramOnClientDetach, NULL, &EbpfXdpProgramHookProvider);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpProgramStop(
    VOID
    )
{
    TraceEnter(TRACE_CORE, "-");

    if (EbpfXdpProgramHookProvider != NULL) {
        EbpfExtensionProviderUnregister(EbpfXdpProgramHookProvider);
        EbpfXdpProgramHookProvider = NULL;
    }

    if (EbpfXdpProgramInfoProvider != NULL) {
        EbpfExtensionProviderUnregister(EbpfXdpProgramInfoProvider);
        EbpfXdpProgramInfoProvider = NULL;
    }

    TraceExitSuccess(TRACE_CORE);
}
