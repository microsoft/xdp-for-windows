//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef AFXDP_HELPER_H
#define AFXDP_HELPER_H

#include <afxdp.h>
#include <xdp/rtl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XSK_RING {
    UINT32 *SharedProducer;
    UINT32 *SharedConsumer;
    UINT32 *SharedFlags;
    VOID *SharedElements;
    UINT32 Mask;
    UINT32 Size;
    UINT32 ElementStride;
    UINT32 CachedProducer;
    UINT32 CachedConsumer;
} XSK_RING;

inline
VOID
XskRingInitialize(
    _Out_ XSK_RING *Ring,
    _In_ const XSK_RING_INFO *RingInfo
    )
{
    RtlZeroMemory(Ring, sizeof(*Ring));

    Ring->SharedProducer = (UINT32 *)(RingInfo->Ring + RingInfo->ProducerIndexOffset);
    Ring->SharedConsumer = (UINT32 *)(RingInfo->Ring + RingInfo->ConsumerIndexOffset);
    Ring->SharedFlags = (UINT32 *)(RingInfo->Ring + RingInfo->FlagsOffset);
    Ring->SharedElements = RingInfo->Ring + RingInfo->DescriptorsOffset;

    Ring->Mask = RingInfo->Size - 1;
    Ring->Size = RingInfo->Size;
    Ring->ElementStride = RingInfo->ElementStride;
    Ring->CachedProducer = ReadUInt32Acquire(Ring->SharedProducer);
    Ring->CachedConsumer = ReadUInt32Acquire(Ring->SharedConsumer);
}

inline
VOID *
XskRingGetElement(
    _In_ const XSK_RING *Ring,
    _In_ UINT32 Index
    )
{
    return (UCHAR *)Ring->SharedElements + (Index & Ring->Mask) * (SIZE_T)Ring->ElementStride;
}

inline
UINT32
XskRingGetFlags(
    _In_ const XSK_RING *Ring
    )
{
    return ReadUInt32Acquire(Ring->SharedFlags);
}

inline
UINT32
XskRingConsumerReserve(
    _In_ XSK_RING *Ring,
    _In_ UINT32 MaxCount,
    _Out_ UINT32 *Index
    )
{
    UINT32 Consumer = *Ring->SharedConsumer;
    UINT32 Available;

    *Index = Consumer;
    Available =  Ring->CachedProducer - Consumer;
    if (Available >= MaxCount) {
        return MaxCount;
    }

    Ring->CachedProducer = ReadUInt32Acquire(Ring->SharedProducer);
    Available =  Ring->CachedProducer - Consumer;
    return Available < MaxCount ? Available : MaxCount;
}

inline
VOID
XskRingConsumerRelease(
    _Inout_ XSK_RING *Ring,
    _In_ UINT32 Count
    )
{
    WriteUInt32Release(Ring->SharedConsumer, *Ring->SharedConsumer + Count);
}

inline
UINT32
XskRingProducerReserve(
    _In_ XSK_RING *Ring,
    _In_ UINT32 MaxCount,
    _Out_ UINT32 *Index
    )
{
    UINT32 Producer = *Ring->SharedProducer;
    UINT32 Available;

    *Index = Producer;
    Available = Ring->Size - (Producer - Ring->CachedConsumer);
    if (Available >= MaxCount) {
        return MaxCount;
    }

    Ring->CachedConsumer = ReadUInt32Acquire(Ring->SharedConsumer);
    Available = Ring->Size - (Producer - Ring->CachedConsumer);
    return Available < MaxCount ? Available : MaxCount;
}

inline
VOID
XskRingProducerSubmit(
    _Inout_ XSK_RING *Ring,
    _In_ UINT32 Count
    )
{
    WriteUInt32Release(Ring->SharedProducer, *Ring->SharedProducer + Count);
}

inline
BOOLEAN
XskRingError(
    _In_ const XSK_RING *Ring
    )
{
    return !!(XskRingGetFlags(Ring) & XSK_RING_FLAG_ERROR);
}

inline
BOOLEAN
XskRingProducerNeedPoke(
    _In_ const XSK_RING *Ring
    )
{
    return !!(XskRingGetFlags(Ring) & XSK_RING_FLAG_NEED_POKE);
}

inline
BOOLEAN
XskRingAffinityChanged(
    _In_ const XSK_RING *Ring
    )
{
    return !!(XskRingGetFlags(Ring) & XSK_RING_FLAG_AFFINITY_CHANGED);
}

inline
BOOLEAN
XskRingOffloadChanged(
    _In_ const XSK_RING *Ring
    )
{
    return !!(XskRingGetFlags(Ring) & XSK_RING_FLAG_OFFLOAD_CHANGED);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
