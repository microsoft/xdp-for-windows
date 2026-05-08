//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// XSKMAP: a fixed-size array mapping queue IDs (UINT32 keys) to AF_XDP
// socket handles. The XSKMAP is plugged into the common XDP map abstraction
// (see map.h) via XdpXskMapTypeDispatch.
//

//
// Allocation size for an XSKMAP, used by map.c when creating a new map.
//
extern const SIZE_T XdpXskMapAllocationSize;

//
// Type dispatch table registered with the common map code.
//
extern const XDP_MAP_TYPE_DISPATCH XdpXskMapTypeDispatch;

//
// Look up the XSK kernel handle stored at Key. Caller must hold the global
// map read lock (see XdpMapAcquireRead). Returns NULL if no entry exists or
// if Key is out of range.
//
_IRQL_requires_(DISPATCH_LEVEL)
VOID *
XdpXskMapLookup(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key
    );
