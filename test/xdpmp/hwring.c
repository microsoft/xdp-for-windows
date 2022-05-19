//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

NTSTATUS
HwRingAllocateRing(
    _In_ UINT32 ElementSize,
    _In_ UINT32 ElementCount,
    _In_ UINT8 Alignment,
    _Out_ HW_RING **Ring
    )
{
    SIZE_T RingSize;
    UINT64 Padding;
    NTSTATUS Status;

    Padding = ALIGN_UP_BY((UINT64)ElementSize, Alignment) - ElementSize;
    Status = RtlUInt32Add(ElementSize, (UINT32)Padding, &ElementSize);
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

    //
    // TODO: Implement alignment. The cache aligned pool should be good enough
    // for now.
    //
    *Ring = ExAllocatePoolZero(NonPagedPoolNxCacheAligned, RingSize, POOLTAG_HWRING);
    if (*Ring == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    (*Ring)->ElementStride = ElementSize;
    (*Ring)->Mask = ElementCount - 1;

Exit:

    return Status;
}

VOID
HwRingFreeRing(
    _In_ HW_RING *Ring
    )
{
    ExFreePoolWithTag(Ring, POOLTAG_HWRING);
}
