//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// eBPF XDP program: drop or pass UDP packets by destination port.
//
// User mode configures the filter by writing to the BPF maps:
//   - port_map[0]   = UDP destination port (network byte order) to match.
//   - action_map[0] = xdp_action_t to apply on match (XDP_DROP, XDP_PASS, XDP_TX).
//
// Packets that do not match are passed through.
//
// Compile:
//   clang -g -target bpf -O2 -I <xdp_include> -c rxfilter_udp.c -o rxfilter_udp.o
//   Convert-BpfToNative.ps1 -FileName rxfilter_udp -IncludeDir <ebpf_include> -Platform x64 -Configuration Release -KernelMode $true
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "net/if_ether.h"
#include "net/ip.h"
#include "xdp/ebpfhook.h"

//
// Map holding the UDP destination port to match (network byte order).
//
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint16_t);
    __uint(max_entries, 1);
} port_map SEC(".maps");

//
// Map holding the XDP action to take on a matching packet.
//
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint32_t);
    __uint(max_entries, 1);
} action_map SEC(".maps");

SEC("xdp/udp_filter")
int
udp_filter(xdp_md_t *ctx)
{
    void *data = ctx->data;
    void *data_end = ctx->data_end;
    uint32_t zero = 0;

    ETHERNET_HEADER *eth = (ETHERNET_HEADER *)data;
    if ((char *)eth + sizeof(*eth) > (char *)data_end) {
        return XDP_PASS;
    }

    //
    // Support both IPv4 and IPv6 by checking the EtherType and skipping
    // the appropriate IP header to reach the UDP header.
    //
    void *transport;
    uint8_t protocol;

    if (eth->Type == bpf_htons(ETHERNET_TYPE_IPV4)) {
        IPV4_HEADER *ip = (IPV4_HEADER *)(eth + 1);
        if ((char *)ip + sizeof(*ip) > (char *)data_end) {
            return XDP_PASS;
        }
        protocol = ip->Protocol;
        transport = (char *)ip + (ip->HeaderLength * 4);
    } else if (eth->Type == bpf_htons(ETHERNET_TYPE_IPV6)) {
        IPV6_HEADER *ip6 = (IPV6_HEADER *)(eth + 1);
        if ((char *)ip6 + sizeof(*ip6) > (char *)data_end) {
            return XDP_PASS;
        }
        protocol = ip6->NextHeader;
        transport = (char *)(ip6 + 1);
    } else {
        return XDP_PASS;
    }

    if (protocol != 17) { // Not UDP
        return XDP_PASS;
    }

    UDP_HEADER *udp = (UDP_HEADER *)transport;
    if ((char *)udp + sizeof(*udp) > (char *)data_end) {
        return XDP_PASS;
    }

    //
    // Look up the configured port and action from BPF maps.
    //
    uint16_t *target_port = bpf_map_lookup_elem(&port_map, &zero);
    if (!target_port) {
        return XDP_PASS;
    }

    if (udp->DestinationPort != *target_port) {
        return XDP_PASS;
    }

    uint32_t *action = bpf_map_lookup_elem(&action_map, &zero);
    if (!action) {
        return XDP_DROP;
    }

    return *action;
}
