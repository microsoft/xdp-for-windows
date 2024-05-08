//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module handles XDP rule parsing and configuration.
//

#include "precomp.h"
#include "programinspect.h"

#define TCP_HDR_LEN_TO_BYTES(x) (((UINT64)(x)) * 4)

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
            VirtualAddressExtension, &Storage->Ip4Hdr, sizeof(Storage->Ip4Hdr), &Cache->Ip4Hdr) &&
        (((UINT64)Cache->Ip4Hdr->HeaderLength) << 2) == sizeof(*Cache->Ip4Hdr);
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
    _In_ UINT32 BufferDataOffset,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    XDP_BUFFER *Buffer = &Frame->Buffer;
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

    if (Cache->EthHdr->Type == htons(ETHERNET_TYPE_IPV4)) {
        if (!Cache->Ip4Valid) {
            XdpParseFragmentedIp4(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->Ip4Valid) {
                return;
            }
        }
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == htons(ETHERNET_TYPE_IPV6)) {
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

    if (Cache->EthHdr->Type == htons(ETHERNET_TYPE_IPV4)) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->Ip4Hdr)) {
            goto BufferTooSmall;
        }
        Cache->Ip4Hdr = (IPV4_HEADER *)&Va[Offset];
        if ((((UINT64)Cache->Ip4Hdr->HeaderLength) << 2) != sizeof(*Cache->Ip4Hdr)) {
            return;
        }
        Cache->Ip4Valid = TRUE;
        Offset += sizeof(*Cache->Ip4Hdr);
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == htons(ETHERNET_TYPE_IPV6)) {
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
            Offset, Cache, Storage);
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
    if (Cache->EthHdr->Type == htons(ETHERNET_TYPE_IPV4)) {
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
    ASSERT(Flow->CidOffset + Flow->CidLength <= XDP_QUIC_MAX_CID_LENGTH);
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
            XDP_QUIC_MAX_CID_LENGTH);
    FrameCache->QuicCid = QuicHdr->SHORT_HDR.DestCid;
    FrameCache->QuicValid = TRUE;
    FrameCache->QuicIsLongHeader = FALSE;
    return FrameCache->QuicCidLength == XDP_QUIC_MAX_CID_LENGTH;
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
    UCHAR *Va =
        XdpGetVirtualAddressExtension(Payload->Buffer, VirtualAddressExtension)->VirtualAddress;
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
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage,
    _Inout_ XDP_PCW_RX_QUEUE *RxQueueStats
    )
{
    DL_EUI48 TempDlAddress;

    if (!Cache->EthCached) {
        XdpParseFrame(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
            Cache, Storage);
    }

    if (!Cache->EthValid) {
        STAT_INC(RxQueueStats, InspectFramesDropped);

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

    STAT_INC(RxQueueStats, InspectFramesForwarded);

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
    XDP_PCW_RX_QUEUE *RxQueueStats = XdpRxQueueGetStatsFromInspectionContext(InspectionContext);

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
                STAT_INC(RxQueueStats, InspectFramesRedirected);
                break;

            case XDP_PROGRAM_ACTION_EBPF:
                //
                // Programs containing an eBPF action are expected to use the
                // XdpInspectEbpf routine instead of XdpInspect.
                //
                ASSERT(FALSE);
                __fallthrough;

            case XDP_PROGRAM_ACTION_DROP:
                Action = XDP_RX_ACTION_DROP;
                STAT_INC(RxQueueStats, InspectFramesDropped);
                break;

            case XDP_PROGRAM_ACTION_PASS:
                Action = XDP_RX_ACTION_PASS;
                STAT_INC(RxQueueStats, InspectFramesPassed);
                break;

            case XDP_PROGRAM_ACTION_L2FWD:
                Action =
                    XdpL2Fwd(
                        Frame, FragmentRing, FragmentExtension, FragmentIndex,
                        VirtualAddressExtension, &FrameCache, &Program->FrameStorage, RxQueueStats);
                break;


            default:
                ASSERT(FALSE);
                break;
            }

            goto Done;
        }
    }

    //
    // No match resulted in a terminating action; perform the default action.
    //
    ASSERT(Action == XDP_RX_ACTION_PASS);
    STAT_INC(RxQueueStats, InspectFramesPassed);

Done:

    return Action;
}

//
// Control path routines.
//

VOID
XdpProgramDeleteRule(
    _Inout_ XDP_RULE *Rule
    )
{
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
                Rule->Redirect.Target = NULL;
            }
            break;

        default:
            ASSERT(FALSE);
        }
    }
}

NTSTATUS
XdpProgramValidateQuicFlow(
    _Out_ XDP_QUIC_FLOW *ValidatedFlow,
    _In_ const XDP_QUIC_FLOW *UserFlow
    )
{
    NTSTATUS Status;
    UINT32 TotalSize;

    RtlZeroMemory(ValidatedFlow, sizeof(*ValidatedFlow));

    Status = RtlUInt32Add(UserFlow->CidOffset, UserFlow->CidLength, &TotalSize);
    if (!NT_SUCCESS(Status)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (TotalSize > RTL_FIELD_SIZE(XDP_QUIC_FLOW, CidData)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    *ValidatedFlow = *UserFlow;

Exit:

    return Status;
}

NTSTATUS
XdpProgramValidateRule(
    _Out_ XDP_RULE *ValidatedRule,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const XDP_RULE *UserRule,
    _In_ UINT32 RuleCount,
    _In_ UINT32 RuleIndex
    )
{
    NTSTATUS Status;

    //
    // Initialize the trusted kernel rule buffer and increment the count of
    // validated rules. The error path will not attempt to clean up
    // unvalidated rules.
    //
    RtlZeroMemory(ValidatedRule, sizeof(*ValidatedRule));

    if (UserRule->Match < XDP_MATCH_ALL || UserRule->Match > XDP_MATCH_TCP_CONTROL_DST) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ValidatedRule->Match = UserRule->Match;

    //
    // Validate each match condition. Many match conditions support all
    // possible input pattern values.
    //
    switch (ValidatedRule->Match) {
    case XDP_MATCH_QUIC_FLOW_SRC_CID:
    case XDP_MATCH_QUIC_FLOW_DST_CID:
    case XDP_MATCH_TCP_QUIC_FLOW_SRC_CID:
    case XDP_MATCH_TCP_QUIC_FLOW_DST_CID:
        Status =
            XdpProgramValidateQuicFlow(
                &ValidatedRule->Pattern.QuicFlow, &UserRule->Pattern.QuicFlow);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        break;
    case XDP_MATCH_UDP_PORT_SET:
        Status =
            XdpProgramCapturePortSet(
                &UserRule->Pattern.PortSet, RequestorMode, &ValidatedRule->Pattern.PortSet);
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
                &UserRule->Pattern.IpPortSet.PortSet, RequestorMode,
                &ValidatedRule->Pattern.IpPortSet.PortSet);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        ValidatedRule->Pattern.IpPortSet.Address = UserRule->Pattern.IpPortSet.Address;
        break;
    default:
        ValidatedRule->Pattern = UserRule->Pattern;
        break;
    }

    if (UserRule->Action < XDP_PROGRAM_ACTION_DROP ||
        UserRule->Action > XDP_PROGRAM_ACTION_EBPF) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ValidatedRule->Action = UserRule->Action;

    //
    // Capture object handle references in the context of the calling thread.
    // The handle will be validated further on the control path.
    //
    switch (UserRule->Action) {
    case XDP_PROGRAM_ACTION_REDIRECT:
        switch (UserRule->Redirect.TargetType) {

        case XDP_REDIRECT_TARGET_TYPE_XSK:
            Status =
                XskReferenceDatapathHandle(
                    RequestorMode, &UserRule->Redirect.Target, TRUE,
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

        DBG_UNREFERENCED_PARAMETER(RuleIndex);
        ASSERT(RuleIndex == 0);
        ValidatedRule->Ebpf.Target = UserRule->Ebpf.Target;

        break;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramDeleteRule(ValidatedRule);
    }

    return Status;
}
