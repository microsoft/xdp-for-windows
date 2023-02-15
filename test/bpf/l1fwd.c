//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"

SEC("xdp/l1fwd")
int
l1fwd(xdp_md_t *ctx)
{
    //
    // For lack of a better name, this forwards completely unmodified packets.
    //
    return XDP_TX;
}
