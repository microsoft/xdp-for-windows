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
