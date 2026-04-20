//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// BPF_MAP_TYPE_XSKMAP for AF_XDP socket redirection.
// This map type is not defined in the eBPF-for-Windows headers.
// The value matches the Linux kernel BPF_MAP_TYPE_XSKMAP.
//
#define BPF_MAP_TYPE_XSKMAP 17

NTSTATUS
XdpXskmapStart(
    VOID
    );

VOID
XdpXskmapStop(
    VOID
    );

ebpf_result_t
XdpXskmapFindElement(
    _In_ const void *Map,
    _In_ const VOID *Key,
    _Outptr_ VOID **Value
    );
