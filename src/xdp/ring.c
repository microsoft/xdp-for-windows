//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module handles XDP ring allocation and configuration.
//

#include "precomp.h"
#include "ring.tmh"

NTSTATUS
XdpRingAllocate(
    _In_ UINT32 ElementSize,
    _In_ UINT32 ElementCount,
    _In_ UINT8 Alignment,
    _Out_ XDP_RING **Ring
    )
{
    SIZE_T RingSize;
    UINT64 Padding;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "ElementSize=%u ElementCount=%u Alignment=%u",
        ElementSize, ElementCount, Alignment);

    Padding = ALIGN_UP_BY((UINT64)ElementSize, Alignment) - ElementSize;
    Status = RtlUInt32Add(ElementSize, (UINT32)Padding, &ElementSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlUInt32RoundUpToPowerOfTwo(ElementCount, &ElementCount);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTMult(ElementSize, ElementCount, &RingSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTAdd(RingSize, sizeof(**Ring), &RingSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ASSERT(Alignment <= SYSTEM_CACHE_ALIGNMENT_SIZE);
    *Ring = ExAllocatePoolZero(NonPagedPoolNxCacheAligned, RingSize, XDP_POOLTAG_RING);
    if (*Ring == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    (*Ring)->ElementStride = ElementSize;
    (*Ring)->Mask = ElementCount - 1;

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpRingFreeRing(
    _In_ XDP_RING *Ring
    )
{
    ExFreePoolWithTag(Ring, XDP_POOLTAG_RING);
}
