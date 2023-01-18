//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static
UINT16
PktChecksumFold(
    _In_ UINT32 Checksum
    )
{
    Checksum = (UINT16)Checksum + (Checksum >> 16);
    Checksum = (UINT16)Checksum + (Checksum >> 16);

    return (UINT16)Checksum;
}

static
UINT16
PktPartialChecksum(
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    )
{
    UINT32 Checksum = 0;
    CONST UINT16 *Buffer16 = Buffer;

    while (BufferLength >= sizeof(*Buffer16)) {
        Checksum += *Buffer16++;
        BufferLength -= sizeof(*Buffer16);
    }

    if (BufferLength > 0) {
        Checksum += *(UCHAR *)Buffer16;
    }

    return PktChecksumFold(Checksum);
}

UINT16
PktPseudoHeaderChecksum(
    _In_ CONST VOID *SourceAddress,
    _In_ CONST VOID *DestinationAddress,
    _In_ UINT8 AddressLength,
    _In_ UINT16 DataLength,
    _In_ UINT8 NextHeader
    )
{
    UINT32 Checksum = 0;

    Checksum += PktPartialChecksum(SourceAddress, AddressLength);
    Checksum += PktPartialChecksum(DestinationAddress, AddressLength);
    DataLength = htons(DataLength);
    Checksum += PktPartialChecksum(&DataLength, sizeof(DataLength));
    Checksum += (NextHeader << 8);

    return PktChecksumFold(Checksum);
}

UINT16
PktChecksum(
    _In_ UINT16 InitialChecksum,
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    )
{
    UINT32 Checksum = InitialChecksum;

    Checksum += PktPartialChecksum(Buffer, BufferLength);

    return ~PktChecksumFold(Checksum);
}

_Success_(return != FALSE)
BOOLEAN
PktBuildUdpFrame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource,
    _In_ UINT16 PortDestination,
    _In_ UINT16 PortSource
    )
{
    CONST UINT32 TotalLength = UDP_HEADER_BACKFILL(AddressFamily) + PayloadLength;
    if (*BufferSize < TotalLength) {
        return FALSE;
    }

    UINT16 UdpLength = sizeof(UDP_HDR) + PayloadLength;
    UINT8 AddressLength;

    ETHERNET_HEADER *EthernetHeader = Buffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type =
        htons(AddressFamily == AF_INET ? ETHERNET_TYPE_IPV4 : ETHERNET_TYPE_IPV6);
    Buffer = EthernetHeader + 1;

    if (AddressFamily == AF_INET) {
        IPV4_HEADER *IpHeader = Buffer;

        if (UdpLength + (UINT16)sizeof(*IpHeader) < UdpLength) {
            return FALSE;
        }

        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV4_VERSION;
        IpHeader->HeaderLength = sizeof(*IpHeader) >> 2;
        IpHeader->TotalLength = htons(sizeof(*IpHeader) + UdpLength);
        IpHeader->TimeToLive = 1;
        IpHeader->Protocol = IPPROTO_UDP;
        AddressLength = sizeof(IN_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
        IpHeader->HeaderChecksum = PktChecksum(0, IpHeader, sizeof(*IpHeader));

        Buffer = IpHeader + 1;
    } else {
        IPV6_HEADER *IpHeader = Buffer;
        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV6_VERSION;
        IpHeader->PayloadLength = htons(UdpLength);
        IpHeader->NextHeader = IPPROTO_UDP;
        IpHeader->HopLimit = 1;
        AddressLength = sizeof(IN6_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);

        Buffer = IpHeader + 1;
    }

    UDP_HDR *UdpHeader = Buffer;
    UdpHeader->uh_sport = PortSource;
    UdpHeader->uh_dport = PortDestination;
    UdpHeader->uh_ulen = htons(UdpLength);
    UdpHeader->uh_sum =
        PktPseudoHeaderChecksum(IpSource, IpDestination, AddressLength, UdpLength, IPPROTO_UDP);
    if (UdpHeader->uh_sum == 0 && AddressFamily == AF_INET6) {
        //
        // UDPv6 requires a non-zero checksum field.
        //
        UdpHeader->uh_sum = (UINT16)~0;
    }

    Buffer = UdpHeader + 1;

    RtlCopyMemory(Buffer, Payload, PayloadLength);
    UdpHeader->uh_sum = PktChecksum(0, UdpHeader, UdpLength);
    *BufferSize = TotalLength;

    return TRUE;
}

_Success_(return != FALSE)
BOOLEAN
PktBuildTcpFrame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_opt_ UINT8 *TcpOptions,
    _In_ UINT16 TcpOptionsLength,
    _In_ UINT32 ThSeq, // host order
    _In_ UINT32 ThAck, // host order
    _In_ UINT8 ThFlags,
    _In_ UINT16 ThWin, // host order
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource,
    _In_ UINT16 PortDestination,
    _In_ UINT16 PortSource
    )
{
    CONST UINT32 TotalLength =
        TCP_HEADER_BACKFILL(AddressFamily) + PayloadLength + TcpOptionsLength;
    if (*BufferSize < TotalLength || TcpOptionsLength > TCP_MAX_OPTION_LEN) {
        return FALSE;
    }

    UINT16 TcpLength = sizeof(TCP_HDR) + TcpOptionsLength + PayloadLength;
    UINT8 AddressLength;

    ETHERNET_HEADER *EthernetHeader = Buffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type =
        htons(AddressFamily == AF_INET ? ETHERNET_TYPE_IPV4 : ETHERNET_TYPE_IPV6);
    Buffer = EthernetHeader + 1;

    if (AddressFamily == AF_INET) {
        IPV4_HEADER *IpHeader = Buffer;

        if (TcpLength + (UINT16)sizeof(*IpHeader) < TcpLength) {
            return FALSE;
        }

        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV4_VERSION;
        IpHeader->HeaderLength = sizeof(*IpHeader) >> 2;
        IpHeader->TotalLength = htons(sizeof(*IpHeader) + TcpLength);
        IpHeader->TimeToLive = 1;
        IpHeader->Protocol = IPPROTO_TCP;
        AddressLength = sizeof(IN_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
        IpHeader->HeaderChecksum = PktChecksum(0, IpHeader, sizeof(*IpHeader));

        Buffer = IpHeader + 1;
    } else {
        IPV6_HEADER *IpHeader = Buffer;
        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV6_VERSION;
        IpHeader->PayloadLength = htons(TcpLength);
        IpHeader->NextHeader = IPPROTO_TCP;
        IpHeader->HopLimit = 1;
        AddressLength = sizeof(IN6_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);

        Buffer = IpHeader + 1;
    }

    TCP_HDR *TcpHeader = Buffer;
    TcpHeader->th_sport = PortSource;
    TcpHeader->th_dport = PortDestination;
    TcpHeader->th_len = (UINT8)((sizeof(TCP_HDR) + TcpOptionsLength) / 4);
    TcpHeader->th_x2 = 0;
    TcpHeader->th_urp = 0;
    TcpHeader->th_seq = htonl(ThSeq);
    TcpHeader->th_ack = htonl(ThAck);
    TcpHeader->th_win = htons(ThWin);
    TcpHeader->th_flags = ThFlags;
    TcpHeader->th_sum =
        PktPseudoHeaderChecksum(IpSource, IpDestination, AddressLength, TcpLength, IPPROTO_TCP);

    Buffer = TcpHeader + 1;
    if (TcpOptions != NULL) {
        RtlCopyMemory(Buffer, TcpOptions, TcpOptionsLength);
    }

    Buffer = (UINT8 *)Buffer + TcpOptionsLength;
    if (Payload != NULL) {
        RtlCopyMemory(Buffer, Payload, PayloadLength);
    }
    TcpHeader->th_sum = PktChecksum(0, TcpHeader, TcpLength);
    *BufferSize = TotalLength;

    return TRUE;
}

_Success_(return != FALSE)
BOOLEAN
PktParseTcpFrame(
    _In_ UCHAR *Frame,
    _In_ UINT32 FrameSize,
    _Out_ TCP_HDR **TcpHdr,
    _Outptr_opt_result_maybenull_ VOID **Payload,
    _Out_opt_ UINT32 *PayloadLength
    )
{
    UINT16 IpProto = IPPROTO_MAX;
    ETHERNET_HEADER *EthHdr;
    UINT16 IPPayloadLength;
    UINT32 Offset = 0;

    if (FrameSize < sizeof(*EthHdr)) {
        return FALSE;
    }

    EthHdr = (ETHERNET_HEADER *)Frame;
    Offset += sizeof(*EthHdr);
    if (EthHdr->Type == htons(ETHERNET_TYPE_IPV4)) {
        if (FrameSize < Offset + sizeof(IPV4_HEADER)) {
            return FALSE;
        }
        IPV4_HEADER *Ip = (IPV4_HEADER *)&Frame[Offset];
        Offset += sizeof(IPV4_HEADER);
        IpProto = Ip->Protocol;
        IPPayloadLength = ntohs(Ip->TotalLength) - sizeof(IPV4_HEADER);
    } else if (EthHdr->Type == htons(ETHERNET_TYPE_IPV6)) {
        if (FrameSize < Offset + sizeof(IPV6_HEADER)) {
            return FALSE;
        }
        IPV6_HEADER *Ip = (IPV6_HEADER *)&Frame[Offset];
        Offset += sizeof(IPV6_HEADER);
        IpProto = (Ip)->NextHeader;
        IPPayloadLength = ntohs(Ip->PayloadLength);
    } else {
        return FALSE;
    }

    if (IpProto == IPPROTO_TCP) {
        if (FrameSize < Offset + sizeof(TCP_HDR)) {
            return FALSE;
        }

        *TcpHdr = (TCP_HDR *)&Frame[Offset];
        UINT8 TcpHeaderLen = (*TcpHdr)->th_len * 4;
        if (FrameSize >= Offset + IPPayloadLength) {
            if (Payload != NULL) {
                Offset += TcpHeaderLen;
                *Payload = &Frame[Offset];                
            }

            if (PayloadLength != NULL) {
                *PayloadLength = IPPayloadLength - TcpHeaderLen;
            }
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
PktStringToInetAddressA(
    _Out_ INET_ADDR *InetAddr,
    _Out_ ADDRESS_FAMILY *AddressFamily,
    _In_ CONST CHAR *String
    )
{
    NTSTATUS Status;
    CONST CHAR *Terminator;

    //
    // Attempt to parse the target as an IPv4 literal.
    //
    *AddressFamily = AF_INET;
    Status = RtlIpv4StringToAddressA(String, TRUE, &Terminator, &InetAddr->Ipv4);

    if (Status != STATUS_SUCCESS) {
        //
        // Attempt to parse the target as an IPv6 literal.
        //
        *AddressFamily = AF_INET6;
        Status = RtlIpv6StringToAddressA(String, &Terminator, &InetAddr->Ipv6);

        if (Status != STATUS_SUCCESS) {
            //
            // No luck, bail.
            //
            return FALSE;
        }
    }

    return TRUE;
}
