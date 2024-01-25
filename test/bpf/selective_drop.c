//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"

// Map configured by user mode to dictate if packets from a specific interface should be dropped.
struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, uint32_t);
    __type(value, uint32_t);
    __uint(max_entries, 1);
} interface_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, uint32_t);
    __type(value, uint64_t);
    __uint(max_entries, 1);
} dropped_packet_map SEC(".maps");

SEC("xdp/selective_drop")
int
drop(xdp_md_t *ctx)
{
    int action = XDP_PASS;
    uint32_t interface_index = ctx->ingress_ifindex;
    uint32_t *interface_value = bpf_map_lookup_elem(&interface_map, &interface_index);
    if (interface_value) {
        action = XDP_DROP;
        uint64_t *dropped_packet_count = bpf_map_lookup_elem(&dropped_packet_map, &interface_index);
        if (dropped_packet_count) {
            *dropped_packet_count += 1;
        } else {
            uint64_t dropped_packet_count = 1;
            bpf_map_update_elem(&dropped_packet_map, &interface_index, &dropped_packet_count, BPF_ANY);
        }
    }

    return action;
}
