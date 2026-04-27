//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "xdp/ebpfhook.h"

//
// XSKMAP for AF_XDP socket redirection. User mode populates this map with
// XSK handles keyed by RX queue index.
//
struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, uint32_t);
    __type(value, void *);
    __uint(max_entries, 64);
} xsk_map SEC(".maps");

SEC("xdp/xsk_redirect")
int
xsk_redirect(xdp_md_t *ctx)
{
    uint32_t index = ctx->rx_queue_index;
    return bpf_redirect_map(&xsk_map, index, XDP_PASS);
}
