//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"

SEC("xdp/drop")
int
drop(xdp_md_t *ctx)
{
    return XDP_DROP;
}
