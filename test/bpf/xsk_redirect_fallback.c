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

//
// Diagnostic map. The program records the values it is about to pass to
// bpf_redirect_map so the test can read them back and confirm the eBPF
// program (and JIT) computed them correctly. This isolates an eBPF-side
// miscomputation from an XDP-side helper issue when the redirect fallback
// path misbehaves (e.g. observed arm64-only failures).
//
//  results_map[0] = rx_queue_index passed as the map key.
//  results_map[1] = fallback action passed as the flags argument.
//  results_map[2] = value returned by bpf_redirect_map (the resulting action).
//
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint64_t);
    __uint(max_entries, 3);
} results_map SEC(".maps");

SEC("xdp/xsk_redirect_fallback")
int
xsk_redirect_fallback(xdp_md_t *ctx)
{
    uint32_t index = ctx->rx_queue_index;
    uint32_t zero = 0;
    uint32_t one = 1;
    uint32_t two = 2;
    uint64_t fallback = XDP_PASS;

    uint32_t *fb = bpf_map_lookup_elem(&fallback_map, &zero);
    if (fb != NULL) {
        fallback = *fb;
    }

    uint64_t index64 = index;
    bpf_map_update_elem(&results_map, &zero, &index64, BPF_ANY);
    bpf_map_update_elem(&results_map, &one, &fallback, BPF_ANY);

    long action = bpf_redirect_map(&xsk_map, index, fallback);

    uint64_t action64 = action;
    bpf_map_update_elem(&results_map, &two, &action64, BPF_ANY);

    return action;
}
