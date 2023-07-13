//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ws2def.h>
#include <ws2ipdef.h>

EXTERN_C_START

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_IPV6 0x86dd

typedef union _DL_EUI48 {
    UINT8 Bytes[6];
} DL_EUI48;

typedef DL_EUI48 ETHERNET_ADDRESS;

typedef struct _ETHERNET_HEADER {
    ETHERNET_ADDRESS Destination;
    ETHERNET_ADDRESS Source;
    union {
        UINT16 Type;            // Ethernet
        UINT16 Length;          // IEEE 802
    };
} ETHERNET_HEADER;

#define IPV4_VERSION 4

typedef struct _IPV4_HEADER {
    union {
        UINT8 VersionAndHeaderLength;   // Version and header length.
        struct {
            UINT8 HeaderLength : 4;
            UINT8 Version : 4;
        };
    };
    union {
        UINT8 TypeOfServiceAndEcnField; // Type of service & ECN (RFC 3168).
        struct {
            UINT8 EcnField : 2;
            UINT8 TypeOfService : 6;
        };
    };
    UINT16 TotalLength;                 // Total length of datagram.
    UINT16 Identification;
    union {
        UINT16 FlagsAndOffset;          // Flags and fragment offset.
        struct {
            UINT16 DontUse1 : 5;        // High bits of fragment offset.
            UINT16 MoreFragments : 1;
            UINT16 DontFragment : 1;
            UINT16 Reserved : 1;
            UINT16 DontUse2 : 8;        // Low bits of fragment offset.
        };
    };
    UINT8 TimeToLive;
    UINT8 Protocol;
    UINT16 HeaderChecksum;
    IN_ADDR SourceAddress;
    IN_ADDR DestinationAddress;
} IPV4_HEADER;

#define IPV6_VERSION 6

typedef struct _IPV6_HEADER {
    union {
        UINT32 VersionClassFlow; // 4 bits Version, 8 Traffic Class, 20 Flow Label.
        struct { // Convenience structure to access Version field only.
            UINT32 : 4;
            UINT32 Version : 4;
            UINT32 : 24;
        };
    };
    UINT16 PayloadLength;   // Zero indicates Jumbo Payload hop-by-hop option.
    UINT8 NextHeader;       // Values are superset of IPv4's Protocol field.
    UINT8 HopLimit;
    IN6_ADDR SourceAddress;
    IN6_ADDR DestinationAddress;
} IPV6_HEADER;

typedef struct _UDP_HDR {
    UINT16 uh_sport;
    UINT16 uh_dport;
    UINT16 uh_ulen;
    UINT16 uh_sum;
} UDP_HDR;

typedef struct _TCP_HDR {
    UINT16 th_sport;
    UINT16 th_dport;
    UINT32 th_seq;
    UINT32 th_ack;
    UINT8 th_x2 : 4;
    UINT8 th_len : 4;
    UINT8 th_flags;
    UINT16 th_win;
    UINT16 th_sum;
    UINT16 th_urp;
} TCP_HDR;

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80

typedef union {
    IN_ADDR Ipv4;
    IN6_ADDR Ipv6;
} INET_ADDR;

UINT16
PktChecksum(
    _In_ UINT16 InitialChecksum,
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    );

UINT16
PktPseudoHeaderChecksum(
    _In_ CONST VOID *SourceAddress,
    _In_ CONST VOID *DestinationAddress,
    _In_ UINT8 AddressLength,
    _In_ UINT16 DataLength,
    _In_ UINT8 NextHeader
    );

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
    );

_Success_(return != FALSE)
BOOLEAN
PktBuildTcpFrame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_opt_ UINT8 *TcpOptions,
    _In_ UINT16 TcpOptionsLength,
    _In_ UINT32 ThSeq,
    _In_ UINT32 ThAck,
    _In_ UINT8 ThFlags,
    _In_ UINT16 ThWin,
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource,
    _In_ UINT16 PortDestination,
    _In_ UINT16 PortSource
    );

_Success_(return != FALSE)
BOOLEAN
PktParseTcpFrame(
    _In_ UCHAR *Frame,
    _In_ UINT32 FrameSize,
    _Out_ TCP_HDR **TcpHdr,
    _Outptr_opt_result_maybenull_ VOID **Payload,
    _Out_opt_ UINT32 *PayloadLength
    );

#define UDP_HEADER_BACKFILL(AddressFamily) \
    (sizeof(ETHERNET_HEADER) + sizeof(UDP_HDR) + \
        ((AddressFamily == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER)))

#define TCP_HEADER_BACKFILL(AddressFamily) \
    (sizeof(ETHERNET_HEADER) + sizeof(TCP_HDR) + \
        ((AddressFamily == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER)))

#define TCP_MAX_OPTION_LEN 40
#define UDP_HEADER_STORAGE UDP_HEADER_BACKFILL(AF_INET6)
#define TCP_HEADER_STORAGE (TCP_HEADER_BACKFILL(AF_INET6) + TCP_MAX_OPTION_LEN)

BOOLEAN
PktStringToInetAddressA(
    _Out_ INET_ADDR *InetAddr,
    _Out_ ADDRESS_FAMILY *AddressFamily,
    _In_ CONST CHAR *String
    );

EXTERN_C_END
