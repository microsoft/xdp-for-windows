//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// eBPF XDP program: redirect UDP port 1234 traffic to an AF_XDP socket via
// XSKMAP. Non-matching traffic is passed through.
//
// User mode populates xsk_map[queue_index] with the XSK socket handle.
//
// Compile:
//   clang -g -target bpf -O2 -I <xdp_include> -c xskfwd_redirect.c -o xskfwd_redirect.o
//   Convert-BpfToNative.ps1 -FileName xskfwd_redirect -IncludeDir <ebpf_include> -Platform x64 -Configuration Release -KernelMode $true
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "net/if_ether.h"
#include "net/ip.h"
#include "xdp/ebpfhook.h"

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, uint32_t);
    __type(value, void *);
    __uint(max_entries, 64);
} xsk_map SEC(".maps");

SEC("xdp/xskfwd_redirect")
int
xskfwd_redirect(xdp_md_t *ctx)
{
    void *data = ctx->data;
    void *data_end = ctx->data_end;

    ETHERNET_HEADER *eth = (ETHERNET_HEADER *)data;
    if ((char *)eth + sizeof(*eth) > (char *)data_end) {
        return XDP_PASS;
    }

    //
    // Only process IPv4 packets.
    //
    if (eth->Type != bpf_htons(ETHERNET_TYPE_IPV4)) {
        return XDP_PASS;
    }

    IPV4_HEADER *ip = (IPV4_HEADER *)(eth + 1);
    if ((char *)ip + sizeof(*ip) > (char *)data_end) {
        return XDP_PASS;
    }

    if (ip->Protocol != 17) { // Not UDP
        return XDP_PASS;
    }

    UDP_HEADER *udp = (UDP_HEADER *)((char *)ip + (ip->HeaderLength * 4));
    if ((char *)udp + sizeof(*udp) > (char *)data_end) {
        return XDP_PASS;
    }

    //
    // Redirect UDP port 1234 traffic to the AF_XDP socket.
    //
    if (udp->DestinationPort == bpf_htons(1234)) {
        return bpf_redirect_map(&xsk_map, ctx->rx_queue_index, XDP_PASS);
    }

    return XDP_PASS;
}
