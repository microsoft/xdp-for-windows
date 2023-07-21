# XDP driver data path

This file contains declarations for the XDP Driver API data path.

## Syntax

```C
DECLARE_HANDLE(XDP_RX_QUEUE_HANDLE);
DECLARE_HANDLE(XDP_TX_QUEUE_HANDLE);
DECLARE_HANDLE(XDP_INTERFACE_HANDLE);

//
// Represents a single-producer, single-consumer ring buffer.
//
typedef struct DECLSPEC_CACHEALIGN _XDP_RING {
    UINT32 ProducerIndex;
    UINT32 ConsumerIndex;

    //
    // The interface may use this field for any purpose.
    //
    UINT32 InterfaceReserved;

    //
    // Reserved for the XDP platform.
    //
    UINT32 Reserved;

    UINT32 Mask;
    UINT32 ElementStride;
    /* Followed by power-of-two array of ring elements */
} XDP_RING;

C_ASSERT(sizeof(XDP_RING) == SYSTEM_CACHE_ALIGNMENT_SIZE);

//
// Returns a pointer to the ring element at the specified index. The index
// must be within [0, Ring->Mask].
//
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

//
// Returns the number of non-empty elements in the ring.
//
inline
UINT32
XdpRingCount(
    _In_ XDP_RING *Ring
    )
{
    return Ring->ProducerIndex - Ring->ConsumerIndex;
}

//
// Returns the number of empty elements in the ring.
//
inline
UINT32
XdpRingFree(
    _In_ XDP_RING *Ring
    )
{
    return Ring->Mask + 1 - XdpRingCount(Ring);
}

//
// Represents a single XDP buffer.
//
typedef struct _XDP_BUFFER {
    //
    // The offset of the start of data from the start of the buffer. XDP
    // inspection may modify the data offset.
    //
    UINT32 DataOffset;

    //
    // The length of the buffer data, in bytes. XDP inspection may modify the
    // data length.
    //
    UINT32 DataLength;

    //
    // The total length of the buffer, in bytes. XDP will not modify the buffer
    // length.
    //
    UINT32 BufferLength;

    //
    // Reserved for system use.
    //
    UINT32 Reserved;
} XDP_BUFFER;

//
// Represents an XDP packet (a complete frame).
//
typedef struct _XDP_FRAME {
    //
    // The first buffer in the frame.
    //
    XDP_BUFFER Buffer;

    //
    // Followed by various XDP descriptor extensions, e.g:
    //
    //   - XDP_BUFFER_VIRTUAL_ADDRESS
    //   - XDP_BUFFER_LOGICAL_ADDRESS
    //   - XDP_FRAME_FRAGMENT (additional fragment count)
    //   - Offload metadata (e.g. Layout, Checksum, GSO, GRO)
    //
    // To retrieve extensions, use the appropriate extension helper routine.
    //
} XDP_FRAME;

//
// Represents a TX frame completion. This structure is used only by interfaces
// with out-of-order TX completion enabled.
//
typedef struct _XDP_TX_FRAME_COMPLETION {
    //
    // The address of the first buffer in the completed frame.
    //
    UINT64 BufferAddress;
} XDP_TX_FRAME_COMPLETION;

//
// Action returned by XDP receive inspection. The interface performs the action.
//
typedef enum _XDP_RX_ACTION {
    //
    // The interface must drop the frame without indicating an NBL to NDIS.
    //
    XDP_RX_ACTION_DROP,

    //
    // The interface must continue processing the frame on its RX path. For NDIS
    // interfaces, the data path should indicate an NBL to the NDIS receive API.
    //
    XDP_RX_ACTION_PASS,

    //
    // Optional. The interface must halt RX processing of the frame and inject
    // the frame onto its associated TX queue.
    //
    XDP_RX_ACTION_TX,
} XDP_RX_ACTION;

//
// XDP multi-frame receive inspection routine. This routine must be invoked from
// an exclusive execution context, i.e. not invoked concurrently with any other
// inspection on the provided XDP receive queue.
//
// For each element in the receive frame (and fragment) rings, XDP inspects the
// frame and sets the XDP action.
//
// Implicitly performs a flush, i.e. XDP will advance the RX frame (and
// fragment) ring consumer indexes to match the producer indexes, and return all
// frames to the XDP interface.
//
// Implicit input parameters:
//
//      FrameRing->ProducerIndex:
//          Frames between the consumer index and producer index are inspected.
//
// Implicit output parameters:
//
//      FrameRing->ConsumerIndex:
//          The consumer index is advanced and all frames are returned to the
//          XDP interface.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

//
// Flush an XDP receive context. XDP will advance the RX frame (and fragment)
// ring consumer indexes to match the producer indexes. This returns all frames
// to the XDP interface.
//
// Implicit input parameters:
//
//      FrameRing->ProducerIndex:
//          Frames between the consumer index and producer index are flushed.
//
// Implicit output parameters:
//
//      FrameRing->ConsumerIndex:
//          The consumer index is advanced and all previous frames are returned
//          to the XDP interface.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

//
// XDP multi-frame transmit inspection routine. This routine must be invoked
// from an exclusive execution context, i.e. not invoked concurrently with any
// other inspection on the provided XDP transmit queue.
//
// Request XDP enqueue frame descriptors to the transmit frame (and fragment)
// rings. XDP interfaces indicate TX completion by advancing the frame (and
// fragment) ring consumer indexes.
//
// If out-of-order TX is enabled, dequeues entries from the transmit queue's
// completion ring instead of using the frame (and fragment) ring consumer
// indexes.
//
// Implicit input parameters:
//
//      FrameRing->ConsumerIndex:
//          The consumer index indicates TX frames that have been completed.
//
// Implicit output parameters:
//
//      FrameRing->ProducerIndex:
//          The producer index indicates frames available for TX.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushTransmit(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    );

typedef enum _XDP_NOTIFY_QUEUE_FLAGS {
    XDP_NOTIFY_QUEUE_FLAG_NONE      = 0x0,

    //
    // The interface is ready for RX. This flag is currently unused.
    //
    XDP_NOTIFY_QUEUE_FLAG_RX        = 0x1,

    //
    // The interface is ready for TX. The interface should request an NDIS poll.
    // From the poll routine, the interface should invoke XdpFlushTransmit and
    // examine the head of the XDP frame ring for new entries.
    //
    XDP_NOTIFY_QUEUE_FLAG_TX        = 0x2,

    //
    // The interface must indicate an RX flush, even if no new RX frames are
    // available. This flag is used for synchronization of the XDP platform.
    // Implementation of this flag is mandatory.
    //
    XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH  = 0x4,

    //
    // The interface must indicate an TX flush. This flag is used for
    // synchronization of the XDP platform. Implementation of this flag is
    // mandatory.
    //
    XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH  = 0x8,
} XDP_NOTIFY_QUEUE_FLAGS;

//
// Notifies the interface of pending data path operations.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XDP_INTERFACE_NOTIFY_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    );
```
