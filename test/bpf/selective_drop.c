//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"

// Map configured by user mode to drop packets from a specific interface.
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint32_t);
    __uint(max_entries, 1);
} interface_map SEC(".maps");

// Map to track the number of dropped packets.
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint64_t);
    __uint(max_entries, 1);
} dropped_packet_map SEC(".maps");

SEC("xdp/selective_drop")
int
drop(xdp_md_t *ctx)
{
    int action = XDP_PASS;
    int zero = 0;

    uint32_t *interface_index = bpf_map_lookup_elem(&interface_map, &zero);
    if (!interface_index) {
        bpf_printk("selective_drop: interface_map lookup failed.");
        goto Exit;
    }

    if (*interface_index == ctx->ingress_ifindex) {
        action = XDP_DROP;
        bpf_printk("selective_drop: interface_map lookup found matching interface %d", *interface_index);

        uint64_t *dropped_packet_count = bpf_map_lookup_elem(&dropped_packet_map, &zero);
        if (dropped_packet_count) {
            *dropped_packet_count += 1;
        } else {
            uint64_t dropped_packet_count = 1;
            bpf_map_update_elem(&dropped_packet_map, &zero, &dropped_packet_count, BPF_ANY);
        }
    } else {
        bpf_printk("selective_drop: interface_map lookup found non-matching interface %d, expected %d", *interface_index, ctx->ingress_ifindex);
    }

Exit:
    return action;
}
