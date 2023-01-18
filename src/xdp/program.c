//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module configures XDP programs on interfaces.
//

#include "precomp.h"
#include <netiodef.h>
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
    CONST UINT8* QuicCid; // Src CID for long header, Dest CID for short header
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
    if ((Type == XDP_MATCH_QUIC_FLOW_SRC_CID) != (QuicHeader->QuicIsLongHeader == 1)) {
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

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspect(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_REDIRECT_CONTEXT *RedirectContext,
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
                    RedirectContext, FrameIndex, FragmentIndex, Rule->Redirect.TargetType,
                    Rule->Redirect.Target);

                Action = XDP_RX_ACTION_DROP;
                break;

            case XDP_PROGRAM_ACTION_DROP:
                Action = XDP_RX_ACTION_DROP;
                break;

            case XDP_PROGRAM_ACTION_PASS:
                Action = XDP_RX_ACTION_PASS;
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
    _In_ XDP_PROGRAM *Program
    )
{
    ASSERT(XdpProgramCanXskBypass(Program));
    return Program->Rules[0].Redirect.Target;
}

//
// Control path routines.
//

typedef struct _XDP_PROGRAM_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_BINDING_HANDLE IfHandle;
    XDP_RX_QUEUE *RxQueue;
    XDP_RX_QUEUE_NOTIFICATION_ENTRY RxQueueNotificationEntry;
    LIST_ENTRY SharingLink;
    ULONG_PTR CreatedByPid;

    union {
        struct {
            UINT32 SharingEnabled : 1;
            UINT32 IsMetaProgram : 1;
        };
        UINT32 Value;
    } Flags;

    XDP_PROGRAM Program;
} XDP_PROGRAM_OBJECT;

typedef struct _XDP_PROGRAM_WORKITEM {
    XDP_BINDING_WORKITEM Bind;
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
    XDP_PROGRAM_OBJECT *ProgramObject;

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
        TRACE_CORE, "Program=%p CreatedByPid=%Iu Flags=0x%x",
        ProgramObject, ProgramObject->CreatedByPid, ProgramObject->Flags.Value);

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

        case XDP_MATCH_QUIC_FLOW_DST_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_QUIC_FLOW_DST_CID "
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

        default:
            ASSERT(FALSE);
            break;
        }
    }
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpProgramPopulateMetaProgram(
    _Inout_ XDP_PROGRAM_OBJECT *MetaProgramObject
    )
{
    XDP_PROGRAM *MetaProgram = &MetaProgramObject->Program;
    LIST_ENTRY *Entry = &MetaProgramObject->SharingLink;

    TraceEnter(TRACE_CORE, "MetaProgram=%p", MetaProgramObject);

    //
    // Traverse the shared programs and concatenate their rulesets into the
    // shared metaprogram. Note that we may be reusing a previously-populated
    // metaprogram if a shared program is being detached.
    //
    MetaProgram->RuleCount = 0;

    while ((Entry = Entry->Flink) != &MetaProgramObject->SharingLink) {
        CONST XDP_PROGRAM_OBJECT *SharedProgramObject =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_OBJECT, SharingLink);
        CONST XDP_PROGRAM *SharedProgram = &SharedProgramObject->Program;

        for (UINT32 i = 0; i < SharedProgram->RuleCount; i++) {
            MetaProgram->Rules[MetaProgram->RuleCount++] = SharedProgram->Rules[i];
        }

        ASSERT(MetaProgram->RuleCount != 0);
        ASSERT(SharedProgramObject->Flags.SharingEnabled);

        TraceInfo(
            TRACE_CORE, "Merged SharedProgram=%p into MetaProgram=%p",
            SharedProgramObject, MetaProgramObject);
        XdpProgramTraceObject(SharedProgramObject);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpProgramDetachRxQueue(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    XDP_RX_QUEUE *RxQueue = ProgramObject->RxQueue;

    TraceEnter(TRACE_CORE, "Program=%p", ProgramObject);

    ASSERT(RxQueue != NULL);
    ASSERT(!ProgramObject->Flags.IsMetaProgram);

    if (XdpRxQueueGetProgram(RxQueue) == &ProgramObject->Program) {
        ASSERT(!ProgramObject->Flags.SharingEnabled);
        XdpRxQueueDeregisterNotifications(RxQueue, &ProgramObject->RxQueueNotificationEntry);
        XdpRxQueueSetProgram(RxQueue, NULL);
    } else if (ProgramObject->Flags.SharingEnabled && !IsListEmpty(&ProgramObject->SharingLink)) {
        XDP_PROGRAM *MetaProgram = XdpRxQueueGetProgram(RxQueue);
        XDP_PROGRAM_OBJECT *MetaProgramObject =
            CONTAINING_RECORD(MetaProgram, XDP_PROGRAM_OBJECT, Program);

        //
        // This program isn't directly attached to the RX queue, but a meta
        // program is. Remove this program from the metaprogram, and either
        // update the program ruleset (in-place) or remove the meta-program if
        // this was the final shared program.
        //
        ASSERT(MetaProgram != NULL);
        ASSERT(MetaProgramObject->Flags.IsMetaProgram);

        XdpRxQueueDeregisterNotifications(RxQueue, &ProgramObject->RxQueueNotificationEntry);
        RemoveEntryList(&ProgramObject->SharingLink);
        InitializeListHead(&ProgramObject->SharingLink);

        if (IsListEmpty(&MetaProgramObject->SharingLink)) {
            XdpRxQueueSetProgram(RxQueue, NULL);
            ExFreePoolWithTag(MetaProgramObject, XDP_POOLTAG_PROGRAM);
            TraceInfo(
                TRACE_CORE, "Detached metaprogram RxQueue=%p Program=%p",
                RxQueue, MetaProgramObject);
        } else {
            XdpRxQueueSync(RxQueue, XdpProgramPopulateMetaProgram, MetaProgramObject);
            TraceInfo(
                TRACE_CORE, "Updated metaprogram RxQueue=%p Program=%p",
                RxQueue, MetaProgramObject);
        }
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
    TraceEnter(TRACE_CORE, "Program=%p", ProgramObject);

    ASSERT(!ProgramObject->Flags.IsMetaProgram);

    //
    // Detach the XDP program from the RX queue.
    //
    if (ProgramObject->RxQueue != NULL) {
        XdpProgramDetachRxQueue(ProgramObject);
        XdpRxQueueDereference(ProgramObject->RxQueue);
        ProgramObject->RxQueue = NULL;
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

    TraceVerbose(TRACE_CORE, "Deleted Program=%p", ProgramObject);
    ExFreePoolWithTag(ProgramObject, XDP_POOLTAG_PROGRAM);
    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpProgramRxQueueNotify(
    XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject =
        CONTAINING_RECORD(NotificationEntry, XDP_PROGRAM_OBJECT, RxQueueNotificationEntry);

    switch (NotificationType) {

    case XDP_RX_QUEUE_NOTIFICATION_DELETE:
        XdpProgramDetachRxQueue(ProgramObject);
        break;

    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpProgramCanXskBypass(
    _In_ XDP_PROGRAM *Program
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = CONTAINING_RECORD(Program, XDP_PROGRAM_OBJECT, Program);

    return
        ProgramObject->Flags.SharingEnabled == FALSE &&
        Program->RuleCount == 1 &&
        Program->Rules[0].Match == XDP_MATCH_ALL &&
        Program->Rules[0].Action == XDP_PROGRAM_ACTION_REDIRECT &&
        Program->Rules[0].Redirect.TargetType == XDP_REDIRECT_TARGET_TYPE_XSK;
}

static
NTSTATUS
XdpProgramAllocate(
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

    ProgramObject = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
    if (ProgramObject == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ProgramObject->CreatedByPid = (ULONG_PTR)PsGetCurrentProcessId();
    InitializeListHead(&ProgramObject->SharingLink);

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

    Status = XdpProgramAllocate(RuleCount, &ProgramObject);
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

        if (UserRule.Match < XDP_MATCH_ALL || UserRule.Match > XDP_MATCH_TCP_DST) {
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
            UserRule.Action > XDP_PROGRAM_ACTION_REDIRECT) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        ValidatedRule->Action = UserRule.Action;

        //
        // Capture object handle references in the context of the calling thread.
        // The handle will be validated further on the control path.
        //
        if (UserRule.Action == XDP_PROGRAM_ACTION_REDIRECT) {

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
VOID
XdpProgramAttach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;
    XDP_PROGRAM_OBJECT *ProgramObject = Item->ProgramObject;
    XDP_PROGRAM *Program = &ProgramObject->Program;
    XDP_PROGRAM *ExistingProgram;
    XDP_PROGRAM_OBJECT *ExistingProgramObject = NULL;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "Program=%p", ProgramObject);

    if (Item->HookId.SubLayer != XDP_HOOK_INSPECT) {
        //
        // Only RX queue programs are currently supported.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status =
        XdpRxQueueFindOrCreate(
            Item->Bind.BindingHandle, &Item->HookId, Item->QueueId, &ProgramObject->RxQueue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Perform further rule validation that require the interface work queue.
    //
    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status = XskValidateDatapathHandle(Rule->Redirect.Target, ProgramObject->RxQueue);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }

                break;

            default:
                break;
            }
        }
    }

    //
    // Query the existing top-level program on the queue, if any.
    //
    ExistingProgram = XdpRxQueueGetProgram(ProgramObject->RxQueue);
    if (ExistingProgram != NULL) {
        ExistingProgramObject = CONTAINING_RECORD(ExistingProgram, XDP_PROGRAM_OBJECT, Program);
    }

    if (ProgramObject->Flags.SharingEnabled) {
        UINT32 MetaRuleCount = Program->RuleCount;
        XDP_PROGRAM_OBJECT *NewMetaProgramObject = NULL;

        if (ExistingProgramObject != NULL && !ExistingProgramObject->Flags.SharingEnabled) {
            Status = STATUS_SHARING_VIOLATION;
            goto Exit;
        }

        //
        // Calculate the new total rule count and allocate a new metaprogram.
        //
        if (ExistingProgram != NULL) {
            Status = RtlUInt32Add(MetaRuleCount, ExistingProgram->RuleCount, &MetaRuleCount);
            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }
        }

        Status = XdpProgramAllocate(MetaRuleCount, &NewMetaProgramObject);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        NewMetaProgramObject->Flags.SharingEnabled = TRUE;
        NewMetaProgramObject->Flags.IsMetaProgram = TRUE;

        //
        // Migrate the list of shared programs from the old metaprogram (if
        // present) onto the new metaprogram and add our new program to the
        // list.
        //
        if (ExistingProgramObject != NULL) {
            ASSERT(ExistingProgramObject->Flags.IsMetaProgram);
            ASSERT(IsListEmpty(&NewMetaProgramObject->SharingLink));
            ASSERT(!IsListEmpty(&ExistingProgramObject->SharingLink));
            AppendTailList(&NewMetaProgramObject->SharingLink, &ExistingProgramObject->SharingLink);
            RemoveEntryList(&ExistingProgramObject->SharingLink);
            InitializeListHead(&ExistingProgramObject->SharingLink);
        }

        ASSERT(IsListEmpty(&ProgramObject->SharingLink));
        InsertTailList(&NewMetaProgramObject->SharingLink, &ProgramObject->SharingLink);

        //
        // Merge all shared programs into the shared metaprogram ruleset.
        //
        XdpProgramPopulateMetaProgram(NewMetaProgramObject);

        //
        // Synchronize with data path and replace old metaprogram with new
        // metaprogram. This is guaranteed to succeed when a program is already
        // attached.
        //
        Status = XdpRxQueueSetProgram(ProgramObject->RxQueue, &NewMetaProgramObject->Program);
        if (NT_SUCCESS(Status)) {
            TraceInfo(
                TRACE_CORE, "Attached metaprogram RxQueue=%p Program=%p OldProgram=%p",
                ProgramObject->RxQueue, ProgramObject, ExistingProgramObject);
            XdpProgramTraceObject(ProgramObject);

            if (ExistingProgramObject != NULL) {
                ExFreePoolWithTag(ExistingProgramObject, XDP_POOLTAG_PROGRAM);
                ExistingProgramObject = NULL;
            }
        } else {
            //
            // Revert changes to the program, scrap the metaprogram, and bail.
            // This can happen only if no metaprogram was previously attached.
            //
            ASSERT(ExistingProgramObject == NULL);

            RemoveEntryList(&ProgramObject->SharingLink);
            InitializeListHead(&ProgramObject->SharingLink);

            ASSERT(IsListEmpty(&NewMetaProgramObject->SharingLink));
            ExFreePoolWithTag(NewMetaProgramObject, XDP_POOLTAG_PROGRAM);
            goto Exit;
        }
    } else {
        //
        // The new program has not enabled sharing; directly attach this program
        // to the queue if it is empty.
        //

        if (ExistingProgramObject != NULL) {
            Status = STATUS_DUPLICATE_OBJECTID;
            goto Exit;
        }


        Status = XdpRxQueueSetProgram(ProgramObject->RxQueue, Program);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    TraceInfo(
        TRACE_CORE, "Attached program RxQueue=%p Program=%p",
        ProgramObject->RxQueue, ProgramObject);
    XdpProgramTraceObject(ProgramObject);

    //
    // Register for interface/queue removal notifications.
    //
    XdpRxQueueRegisterNotifications(
        ProgramObject->RxQueue, &ProgramObject->RxQueueNotificationEntry, XdpProgramRxQueueNotify);

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramDelete(ProgramObject);
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
    XDP_INTERFACE_MODE InterfaceMode;
    XDP_INTERFACE_MODE *RequiredMode = NULL;
    XDP_BINDING_HANDLE BindingHandle = NULL;
    XDP_PROGRAM_WORKITEM WorkItem = {0};
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;
    CONST UINT32 ValidFlags =
        XDP_CREATE_PROGRAM_FLAG_GENERIC | XDP_CREATE_PROGRAM_FLAG_NATIVE |
        XDP_CREATE_PROGRAM_FLAG_SHARE;

    if (Disposition != FILE_CREATE || InputBufferLength < sizeof(*Params)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Params = InputBuffer;

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

    BindingHandle = XdpIfFindAndReferenceBinding(Params->IfIndex, &Params->HookId, 1, RequiredMode);
    if (BindingHandle == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status =
        XdpCaptureProgram(Params->Rules, Params->RuleCount, Irp->RequestorMode, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (Params->Flags & XDP_CREATE_PROGRAM_FLAG_SHARE) {
        ProgramObject->Flags.SharingEnabled = TRUE;
    }

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.QueueId = Params->QueueId;
    WorkItem.HookId = Params->HookId;
    WorkItem.ProgramObject = ProgramObject;
    WorkItem.Bind.BindingHandle = BindingHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramAttach;

    //
    // Attach the program using the inteface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);

    Status = WorkItem.CompletionStatus;

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->Header.ObjectType = XDP_OBJECT_TYPE_PROGRAM;
        ProgramObject->Header.Dispatch = &XdpProgramFileDispatch;
        ProgramObject->IfHandle = BindingHandle, BindingHandle = NULL;
        IrpSp->FileObject->FsContext = ProgramObject;
    }

    if (BindingHandle != NULL) {
        XdpIfDereferenceBinding(BindingHandle);
    }

    if (Params != NULL) {
        TraceInfo(
            TRACE_CORE,
            "Program=%p IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x Status=%!STATUS!",
            ProgramObject, Params->IfIndex, Params->HookId.Layer,
            Params->HookId.Direction, Params->HookId.SubLayer, Params->QueueId,
            Params->Flags, Status);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpProgramClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_PROGRAM_OBJECT *Program = IrpSp->FileObject->FsContext;
    XDP_PROGRAM_WORKITEM WorkItem = {0};

    UNREFERENCED_PARAMETER(Irp);

    TraceEnter(TRACE_CORE, "Program=%p", Program);
    TraceInfo(TRACE_CORE, "Closing Program=%p", Program);

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.ProgramObject = Program;
    WorkItem.Bind.BindingHandle = Program->IfHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramDetach;

    //
    // Perform the detach on the interface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(NT_SUCCESS(WorkItem.CompletionStatus));

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}
