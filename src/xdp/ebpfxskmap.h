//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

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
