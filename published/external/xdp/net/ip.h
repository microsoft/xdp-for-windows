//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef __NET_IP_H
#define __NET_IP_H

#ifdef __cplusplus
extern "C" {
#endif

//
// IP protocol definitions for XDP eBPF programs.
//

// IP protocol numbers
#define IPPROTO_IP      0   // Dummy protocol
#define IPPROTO_ICMP    1   // Internet Control Message Protocol
#define IPPROTO_IGMP    2   // Internet Group Management Protocol
#define IPPROTO_IPIP    4   // IPIP tunneling
#define IPPROTO_TCP     6   // Transmission Control Protocol
#define IPPROTO_UDP     17  // User Datagram Protocol
#define IPPROTO_IPV6    41  // IPv6 encapsulation
#define IPPROTO_ICMPV6  58  // ICMPv6
#define IPPROTO_SCTP    132 // Stream Control Transmission Protocol
#define IPPROTO_RAW     255 // Raw IP packets

#pragma pack(push, 1)

// IPv4 header structure
typedef struct _IPV4_HEADER {
    unsigned char VersionAndHeaderLength; // Version (4 bits) + Header Length (4 bits)
    unsigned char TypeOfService;
    unsigned short TotalLength;
    unsigned short Identification;
    unsigned short FlagsAndFragmentOffset;
    unsigned char TimeToLive;
    unsigned char Protocol;
    unsigned short HeaderChecksum;
    unsigned int SourceAddress;
    unsigned int DestinationAddress;
} IPV4_HEADER;

// Alternative names for compatibility
typedef IPV4_HEADER ipv4_header_t;
typedef IPV4_HEADER iphdr;

// IPv6 header structure
typedef struct _IPV6_HEADER {
    union {
        struct {
            unsigned int VersionClassFlow; // Version (4 bits) + Traffic Class (8 bits) + Flow Label (20 bits)
        };
        struct {
            unsigned char Version : 4;
            unsigned char TrafficClass : 8;
            unsigned int FlowLabel : 20;
        };
    };
    unsigned short PayloadLength;
    unsigned char NextHeader;
    unsigned char HopLimit;
    unsigned char SourceAddress[16];
    unsigned char DestinationAddress[16];
} IPV6_HEADER;

// Alternative names for compatibility
typedef IPV6_HEADER ipv6_header_t;
typedef IPV6_HEADER ipv6hdr;

// UDP header structure
typedef struct _UDP_HDR {
    unsigned short SourcePort;
    unsigned short DestinationPort;
    unsigned short Length;
    unsigned short Checksum;
} UDP_HDR;

// Alternative names for compatibility
typedef UDP_HDR udp_header_t;
typedef UDP_HDR udphdr;

// TCP header structure
typedef struct _TCP_HDR {
    unsigned short SourcePort;
    unsigned short DestinationPort;
    unsigned int SequenceNumber;
    unsigned int AcknowledgmentNumber;
    unsigned char DataOffsetAndReserved; // Data Offset (4 bits) + Reserved (4 bits)
    unsigned char Flags;
    unsigned short Window;
    unsigned short Checksum;
    unsigned short UrgentPointer;
} TCP_HDR;

// Alternative names for compatibility
typedef TCP_HDR tcp_header_t;
typedef TCP_HDR tcphdr;

// TCP flags
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20
#define TCP_FLAG_ECE  0x40
#define TCP_FLAG_CWR  0x80

// ICMP header structure
typedef struct _ICMP_HEADER {
    unsigned char Type;
    unsigned char Code;
    unsigned short Checksum;
    union {
        struct {
            unsigned short Identifier;
            unsigned short SequenceNumber;
        } Echo;
        unsigned int Gateway;
        struct {
            unsigned short Unused;
            unsigned short NextHopMtu;
        } FragNeeded;
    };
} ICMP_HEADER;

// Alternative names for compatibility
typedef ICMP_HEADER icmp_header_t;
typedef ICMP_HEADER icmphdr;

// ICMPv6 header structure
typedef struct _ICMPV6_HEADER {
    unsigned char Type;
    unsigned char Code;
    unsigned short Checksum;
    union {
        struct {
            unsigned short Identifier;
            unsigned short SequenceNumber;
        } Echo;
        unsigned int Mtu;
        unsigned int Pointer;
        unsigned int Unused;
    };
} ICMPV6_HEADER;

// Alternative names for compatibility
typedef ICMPV6_HEADER icmpv6_header_t;
typedef ICMPV6_HEADER icmp6hdr;

#pragma pack(pop)

// Helper macros for IPv4 header
#define IPV4_GET_VERSION(iph) (((iph)->VersionAndHeaderLength >> 4) & 0x0F)
#define IPV4_GET_HEADER_LENGTH(iph) (((iph)->VersionAndHeaderLength & 0x0F) * 4)
#define IPV4_SET_VERSION_AND_HEADER_LENGTH(iph, ver, hlen) \
    ((iph)->VersionAndHeaderLength = (((ver) & 0x0F) << 4) | (((hlen) / 4) & 0x0F))

// Helper macros for IPv6 header
#define IPV6_GET_VERSION(ip6h) (((ip6h)->Version) & 0x0F)

// Helper macros for TCP header
#define TCP_GET_DATA_OFFSET(tcph) (((tcph)->DataOffsetAndReserved >> 4) & 0x0F)
#define TCP_GET_HEADER_LENGTH(tcph) (TCP_GET_DATA_OFFSET(tcph) * 4)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __NET_IP_H
