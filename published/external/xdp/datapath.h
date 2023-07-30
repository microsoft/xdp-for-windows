//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

DECLARE_HANDLE(XDP_RX_QUEUE_HANDLE);
DECLARE_HANDLE(XDP_TX_QUEUE_HANDLE);
DECLARE_HANDLE(XDP_INTERFACE_HANDLE);

#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier

typedef struct DECLSPEC_CACHEALIGN _XDP_RING {
    UINT32 ProducerIndex;
    UINT32 ConsumerIndex;
    UINT32 InterfaceReserved;
    UINT32 Reserved;
    UINT32 Mask;
    UINT32 ElementStride;
    //
    // Followed by power-of-two array of ring elements.
    //
} XDP_RING;

#pragma warning(pop)

C_ASSERT(sizeof(XDP_RING) == SYSTEM_CACHE_ALIGNMENT_SIZE);

inline
VOID *
XdpRingGetElement(
    _In_ XDP_RING *Ring,
    _In_ UINT32 Index
    )
{
    ASSERT(Index <= Ring->Mask);
    return (PUCHAR)&Ring[1] + (SIZE_T)Index * Ring->ElementStride;
}

inline
UINT32
XdpRingCount(
    _In_ XDP_RING *Ring
    )
{
    return Ring->ProducerIndex - Ring->ConsumerIndex;
}

inline
UINT32
XdpRingFree(
    _In_ XDP_RING *Ring
    )
{
    return Ring->Mask + 1 - XdpRingCount(Ring);
}

typedef struct _XDP_BUFFER {
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
    UINT32 Reserved;
    //
    // Followed by various XDP descriptor extensions.
    //
} XDP_BUFFER;

C_ASSERT(sizeof(XDP_BUFFER) == 16);

typedef struct _XDP_FRAME {
    XDP_BUFFER Buffer;
    //
    // Followed by various XDP descriptor extensions.
    //
} XDP_FRAME;

typedef struct _XDP_TX_FRAME_COMPLETION {
    UINT64 BufferAddress;
} XDP_TX_FRAME_COMPLETION;

typedef enum _XDP_RX_ACTION {
    XDP_RX_ACTION_DROP,
    XDP_RX_ACTION_PASS,
    XDP_RX_ACTION_TX,
} XDP_RX_ACTION;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushTransmit(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    );

typedef enum _XDP_NOTIFY_QUEUE_FLAGS {
    XDP_NOTIFY_QUEUE_FLAG_NONE      = 0x0,
    XDP_NOTIFY_QUEUE_FLAG_RX        = 0x1,
    XDP_NOTIFY_QUEUE_FLAG_TX        = 0x2,
    XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH  = 0x4,
    XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH  = 0x8,
} XDP_NOTIFY_QUEUE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XDP_NOTIFY_QUEUE_FLAGS);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XDP_INTERFACE_NOTIFY_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    );

#include <xdp/details/datapath.h>

EXTERN_C_END
