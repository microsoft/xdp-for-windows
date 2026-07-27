//
// Copyright (c) Microsoft Corporation.
//

#pragma once

//
// Common definitions shared by all eBPF extensible map providers implemented by
// XDP.
//
// The eBPF runtime hands raw map pointers to helpers such as bpf_redirect_map
// without any compile-time type information. Every XDP map context therefore
// begins with an XDP_EBPF_MAP_HEADER so that a raw map pointer can be resolved
// to its concrete map type and validated before the context is dereferenced.
//

typedef enum _XDP_EBPF_MAP_TYPE {
    //
    // BPF_MAP_TYPE_XSKMAP: AF_XDP socket redirect map. See ebpfxskmap.c.
    //
    XdpEbpfMapTypeXsk = 1,
} XDP_EBPF_MAP_TYPE;

typedef struct _XDP_EBPF_MAP_HEADER {
    XDP_EBPF_MAP_TYPE Type;
} XDP_EBPF_MAP_HEADER;
