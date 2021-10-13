//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

NTSTATUS
XdpRingAllocate(
    _In_ UINT32 ElementSize,
    _In_ UINT32 ElementCount,
    _In_ UINT8 Alignment,
    _Out_ XDP_RING **Ring
    );

VOID
XdpRingFreeRing(
    _In_ XDP_RING *Ring
    );
