//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef __NET_IF_ETHER_H
#define __NET_IF_ETHER_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Ethernet protocol definitions for XDP eBPF programs.
//

#ifndef ETH_ALEN
#define ETH_ALEN 6  // Ethernet address length
#endif

#ifndef ETH_HLEN
#define ETH_HLEN 14 // Ethernet header length
#endif

#ifndef ETH_ZLEN
#define ETH_ZLEN 60 // Minimum frame size
#endif

#ifndef ETH_DATA_LEN
#define ETH_DATA_LEN 1500 // Maximum data length
#endif

#ifndef ETH_FRAME_LEN
#define ETH_FRAME_LEN 1514 // Maximum frame length (header + data)
#endif

#ifndef ETH_FCS_LEN
#define ETH_FCS_LEN 4 // Frame check sequence length
#endif

// Ethernet protocol types (EtherType values)
#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_TYPE_IPV6 0x86DD
#define ETHERNET_TYPE_VLAN 0x8100
#define ETHERNET_TYPE_8021Q 0x8100
#define ETHERNET_TYPE_8021AD 0x88A8

// Legacy names for compatibility
#define ETH_P_IP   0x0800
#define ETH_P_ARP  0x0806
#define ETH_P_IPV6 0x86DD
#define ETH_P_8021Q 0x8100
#define ETH_P_8021AD 0x88A8

#pragma pack(push, 1)

// Ethernet header structure
typedef struct _ETHERNET_HEADER {
    unsigned char Destination[ETH_ALEN];
    unsigned char Source[ETH_ALEN];
    unsigned short Type;
} ETHERNET_HEADER;

// Alternative names for compatibility
typedef ETHERNET_HEADER ethernet_header_t;
typedef ETHERNET_HEADER ethhdr;

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __NET_IF_ETHER_H
