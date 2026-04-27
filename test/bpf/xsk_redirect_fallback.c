//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "xdp/ebpfhook.h"

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, uint32_t);
    __type(value, void *);
    __uint(max_entries, 64);
} xsk_map SEC(".maps");

//
// A single-element array map that controls the fallback action passed to
// bpf_redirect_map. The test populates index 0 with the desired xdp_action
// value (XDP_PASS, XDP_DROP, or XDP_TX).
//
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint32_t);
    __uint(max_entries, 1);
} fallback_map SEC(".maps");

SEC("xdp/xsk_redirect_fallback")
int
xsk_redirect_fallback(xdp_md_t *ctx)
{
    uint32_t index = ctx->rx_queue_index;
    uint32_t zero = 0;
    uint64_t fallback = XDP_PASS;

    uint32_t *fb = bpf_map_lookup_elem(&fallback_map, &zero);
    if (fb != NULL) {
        fallback = *fb;
    }

    return bpf_redirect_map(&xsk_map, index, fallback);
}
