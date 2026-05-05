//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// eBPF XDP program: L2 forward (bounce) all packets back to the sender.
//
// This swaps the source and destination MAC addresses, then returns XDP_TX
// so the NIC transmits the frame back out.
//
// Compile:
//   clang -g -target bpf -O2 -I <xdp_include> -c rxfilter_l2fwd.c -o rxfilter_l2fwd.o
//   Convert-BpfToNative.ps1 -FileName rxfilter_l2fwd -IncludeDir <ebpf_include> -Platform x64 -Configuration Release -KernelMode $true
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "net/if_ether.h"
#include "xdp/ebpfhook.h"

SEC("xdp/l2fwd")
int
l2fwd(xdp_md_t *ctx)
{
    void *data = ctx->data;
    void *data_end = ctx->data_end;

    ETHERNET_HEADER *eth = (ETHERNET_HEADER *)data;
    if ((char *)eth + sizeof(*eth) > (char *)data_end) {
        return XDP_PASS;
    }

    //
    // Swap source and destination MAC addresses.
    //
    uint8_t tmp[6];
    __builtin_memcpy(tmp, eth->Destination, 6);
    __builtin_memcpy(eth->Destination, eth->Source, 6);
    __builtin_memcpy(eth->Source, tmp, 6);

    return XDP_TX;
}
