//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This BPF program attempts to call bpf_map_lookup_elem, bpf_map_update_elem,
// and bpf_map_delete_elem on an XSKMAP from within a BPF program. All three
// should fail at runtime because the XSKMAP has updates_original_value set.
// Results are stored in a separate array map for the test to read back.
//
// results_map[0] = bpf_map_lookup_elem result (1 if NULL, 0 if non-NULL)
// results_map[1] = bpf_map_update_elem result (the return code)
// results_map[2] = bpf_map_delete_elem result (the return code)
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

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, int64_t);
    __uint(max_entries, 3);
} results_map SEC(".maps");

SEC("xdp/xsk_map_crud")
int
xsk_map_crud(xdp_md_t *ctx)
{
    uint32_t key = 0;
    int64_t result;

    //
    // Test 1: bpf_map_lookup_elem should return NULL.
    //
    void *value = bpf_map_lookup_elem(&xsk_map, &key);
    result = (value == NULL) ? 1 : 0;
    uint32_t idx = 0;
    bpf_map_update_elem(&results_map, &idx, &result, 0);

    //
    // Test 2: bpf_map_update_elem should fail (return non-zero).
    //
    uint32_t dummy_value = 0;
    result = bpf_map_update_elem(&xsk_map, &key, &dummy_value, 0);
    idx = 1;
    bpf_map_update_elem(&results_map, &idx, &result, 0);

    //
    // Test 3: bpf_map_delete_elem should fail (return non-zero).
    //
    result = bpf_map_delete_elem(&xsk_map, &key);
    idx = 2;
    bpf_map_update_elem(&results_map, &idx, &result, 0);

    return XDP_PASS;
}
