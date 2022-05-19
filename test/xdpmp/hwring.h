//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Represents a multi-producer, single-consumer ring buffer. Elements are
// enqueued to the hardware at the producer index, the hardware completes
// elements at the hardware completion index, and elements are dequeued at the
// consumer index.
//
// [-----CccccccHppppppppPrrrrrrrR--------]
//
// C = ConsumerIndex
// H = HardwareCompletionIndex
// P = ProducerIndex
// R = ProducerReserved (MP enqueue)
//
typedef struct _HW_RING {
    UINT32 ProducerIndex;
    UINT32 ProducerReserved;
    UINT32 ConsumerIndex;
    UINT32 HardwareCompletionIndex;
    UINT32 Mask;
    UINT32 ElementStride;
    /* Followed by power-of-two array of ring elements */
} HW_RING;

//
// Returns a pointer to the ring element at the specified index. The index
// must be within [0, Ring->Mask].
//
inline
VOID *
HwRingGetElement(
    _In_ HW_RING *Ring,
    _In_ UINT32 Index
    )
{
    ASSERT(Index <= Ring->Mask);
    return (PUCHAR)&Ring[1] + (SIZE_T)Index * Ring->ElementStride;
}

//
// Returns TRUE if there are no empty elements for the producer.
//
inline
BOOLEAN
HwRingProdIsFull(
    _In_ HW_RING *Ring
    )
{
    return (Ring->ProducerIndex - Ring->ConsumerIndex) == (Ring->Mask + 1);
}

//
// Returns the element at the consumer index and advances the consumer index.
//
inline
VOID *
HwRingConsPopElement(
    _In_ HW_RING *Ring
    )
{
    UINT32 Index = Ring->ConsumerIndex;
    VOID *Element = HwRingGetElement(Ring, Index & Ring->Mask);
    WriteUInt32Release(&Ring->ConsumerIndex, Index + 1);

    return Element;
}

//
// Advances the consumer index.
//
inline
VOID
HwRingConsPopElements(
    _In_ HW_RING *Ring,
    _In_ UINT32 Count
    )
{
    WriteUInt32Release(&Ring->ConsumerIndex, Ring->ConsumerIndex + Count);
}

//
// Returns the number of non-empty elements in the ring that have not yet been consumed.
//
inline
UINT32
HwRingConsPeek(
    _In_ HW_RING *Ring
    )
{
    return Ring->HardwareCompletionIndex - Ring->ConsumerIndex;
}

//
// The hardware "completes" up to the specified number of elements.
//
inline
UINT32
HwRingHwComplete(
    _In_ HW_RING *Ring,
    _In_ UINT32 Count
    )
{
    UINT32 ProducerIndex = (UINT32)ReadNoFence((LONG*)&Ring->ProducerIndex);
    UINT32 HardwareAvailable = ProducerIndex - Ring->HardwareCompletionIndex;

    Count = min(Count, HardwareAvailable);
    Ring->HardwareCompletionIndex += Count;

    return Count;
}

_IRQL_raises_(DISPATCH_LEVEL)
inline
VOID
HwRingBoundedMpReserve(
    _In_ HW_RING *Ring,
    _In_ UINT32 Count,
    _Out_ UINT32 *Head,
    _Out_ _At_(*OldIrql, _IRQL_saves_) KIRQL *OldIrql
    )
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
    *Head = InterlockedAdd((LONG *)&Ring->ProducerReserved, Count) - Count;
}

//
// Returns the number of elements reserved for the producer. The caller must
// call HwRingMpCommit only if > 0 elements have been reserved.
//
_When_(return != 0, _IRQL_raises_(DISPATCH_LEVEL) _IRQL_saves_)
inline
UINT32
HwRingBestEffortMpReserve(
    _In_ HW_RING *Ring,
    _In_ UINT32 MaxCount,
    _When_(return != 0, _Out_) UINT32 *Head,
    _Out_ _At_(*OldIrql, _IRQL_saves_) KIRQL *OldIrql
    )
{
    UINT32 Count;
    UINT32 OldProducerReserved;

    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
    do {
        OldProducerReserved = Ring->ProducerReserved;
        Count = Ring->Mask + 1 - (OldProducerReserved - Ring->ConsumerIndex);
        Count = min(MaxCount, Count);
    } while (InterlockedCompareExchange(
                (LONG *)&Ring->ProducerReserved, OldProducerReserved + Count, OldProducerReserved)
            != (LONG)OldProducerReserved);

    if (Count == 0) {
        KeLowerIrql(*OldIrql);
    } else {
        *Head = OldProducerReserved;
    }

    return Count;
}

_IRQL_requires_(DISPATCH_LEVEL)
inline
VOID
HwRingMpCommit(
    _In_ HW_RING *Ring,
    _In_ UINT32 Count,
    _In_ UINT32 Head,
    _In_ _IRQL_restores_ KIRQL OldIrql
    )
{
    while ((UINT32)ReadNoFence((LONG*)&Ring->ProducerIndex) != Head);
    WriteUInt32Release(&Ring->ProducerIndex, Head + Count);
    KeLowerIrql(OldIrql);
}

NTSTATUS
HwRingAllocateRing(
    _In_ UINT32 ElementSize,
    _In_ UINT32 ElementCount,
    _In_ UINT8 Alignment,
    _Out_ HW_RING **Ring
    );

VOID
HwRingFreeRing(
    _In_ HW_RING *Ring
    );
