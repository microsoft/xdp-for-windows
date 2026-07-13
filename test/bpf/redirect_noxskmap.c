//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This BPF program calls bpf_redirect_map with a non-XSKMAP map. The XDP
// redirect helper only supports XSKMAPs, so the redirect must fail and the
// program must return the fallback action encoded in the flags (XDP_PASS)
// rather than crashing.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "xdp/ebpfhook.h"

//
// A plain array map, deliberately NOT an XSKMAP, passed to bpf_redirect_map.
//
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, uint32_t);
    __type(value, uint32_t);
    __uint(max_entries, 1);
} not_an_xskmap SEC(".maps");

SEC("xdp/redirect_noxskmap")
int
redirect_noxskmap(xdp_md_t *ctx)
{
    (void)ctx;

    //
    // bpf_redirect_map with a non-XSKMAP must fail and fall back to XDP_PASS.
    //
    return bpf_redirect_map(&not_an_xskmap, 0, XDP_PASS);
}
