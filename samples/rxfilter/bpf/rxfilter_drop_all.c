//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// eBPF XDP program: drop all packets.
//
// Compile:
//   clang -g -target bpf -O2 -c rxfilter_drop_all.c -o rxfilter_drop_all.o
//   Convert-BpfToNative.ps1 -FileName rxfilter_drop_all -IncludeDir <ebpf_include> -Platform x64 -Configuration Release -KernelMode $true
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "xdp/ebpfhook.h"

SEC("xdp/drop_all")
int
drop_all(xdp_md_t *ctx)
{
    return XDP_DROP;
}
