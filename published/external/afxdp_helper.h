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
    UINT32 *sharedProducer;
    UINT32 *sharedConsumer;
    UINT32 *sharedFlags;
    VOID *sharedElements;
    UINT32 mask;
    UINT32 size;
    UINT32 elementStride;
} XSK_RING;

inline
VOID
XskRingInitialize(
    _Out_ XSK_RING *ring,
    _In_ CONST XSK_RING_INFO *ringInfo
    )
{
    RtlZeroMemory(ring, sizeof(*ring));

    ring->sharedProducer = (UINT32 *)(ringInfo->ring + ringInfo->producerIndexOffset);
    ring->sharedConsumer = (UINT32 *)(ringInfo->ring + ringInfo->consumerIndexOffset);
    ring->sharedFlags = (UINT32 *)(ringInfo->ring + ringInfo->flagsOffset);
    ring->sharedElements = ringInfo->ring + ringInfo->descriptorsOffset;

    ring->mask = ringInfo->size - 1;
    ring->size = ringInfo->size;
    ring->elementStride = ringInfo->elementStride;
}

inline
VOID *
XskRingGetElement(
    _In_ CONST XSK_RING *ring,
    _In_ UINT32 index
    )
{
    return (UCHAR *)ring->sharedElements + (index & ring->mask) * (SIZE_T)ring->elementStride;
}

inline
UINT32
XskRingGetFlags(
    _In_ CONST XSK_RING *ring
    )
{
    return ReadUInt32Acquire(ring->sharedFlags);
}

inline
UINT32
XskRingConsumerReserve(
    _In_ XSK_RING *ring,
    _In_ UINT32 maxCount,
    _Out_ UINT32 *index
    )
{
    UINT32 consumer = *ring->sharedConsumer;
    UINT32 available = ReadUInt32Acquire(ring->sharedProducer) - consumer;
    *index = consumer;
    return available < maxCount ? available : maxCount;
}

inline
VOID
XskRingConsumerRelease(
    _Inout_ XSK_RING *ring,
    _In_ UINT32 count
    )
{
    *ring->sharedConsumer += count;
}

inline
UINT32
XskRingProducerReserve(
    _In_ XSK_RING *ring,
    _In_ UINT32 maxCount,
    _Out_ UINT32 *index
    )
{
    UINT32 producer = *ring->sharedProducer;
    UINT32 available = ring->size - (producer - ReadUInt32Acquire(ring->sharedConsumer));
    *index = producer;
    return available < maxCount ? available : maxCount;
}

inline
VOID
XskRingProducerSubmit(
    _Inout_ XSK_RING *ring,
    _In_ UINT32 count
    )
{
    WriteUInt32Release(ring->sharedProducer, *ring->sharedProducer + count);
}

inline
BOOLEAN
XskRingError(
    _In_ CONST XSK_RING *ring
    )
{
    return !!(XskRingGetFlags(ring) & XSK_RING_FLAG_ERROR);
}

inline
BOOLEAN
XskRingProducerNeedPoke(
    _In_ CONST XSK_RING *ring
    )
{
    return !!(XskRingGetFlags(ring) & XSK_RING_FLAG_NEED_POKE);
}

inline
BOOLEAN
XskRingAffinityChanged(
    _In_ CONST XSK_RING *ring
    )
{
    return !!(XskRingGetFlags(ring) & XSK_RING_FLAG_AFFINITY_CHANGED);
}

inline
VOID
XskDescriptorSetOffset(
    _Inout_ UINT64 *descriptor,
    _In_ UINT16 offset
    )
{
    *descriptor |= ((UINT64)offset << XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT);
}

inline
UINT64
XskDescriptorGetAddress(
    _In_ UINT64 descriptor
    )
{
    return descriptor & ~XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MASK;
}

inline
UINT16
XskDescriptorGetOffset(
    _In_ UINT64 descriptor
    )
{
    return (UINT16)(descriptor >> XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
