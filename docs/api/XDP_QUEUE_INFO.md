# XDP_QUEUE_INFO structure

A structure to uniquely identify an interface queue.

## Syntax

```C
//
// Enumeration of XDP queue types.
//
typedef enum _XDP_QUEUE_TYPE {
    //
    // The default RSS queue(s) on the interface, or if RSS is not supported,
    // the default queue. QueueId specifies the zero-based index of the RSS
    // queue.
    //
    // If the interface is configured with 4 default RSS queues
    // (NumberOfReceiveQueues in NDIS_RECEIVE_SCALE_CAPABILITIES), the XDP
    // platform identifies these queues by IDs {0, 1, 2, 3}.
    //
    XDP_QUEUE_TYPE_DEFAULT_RSS,
} XDP_QUEUE_TYPE;

//
// The XDP queue information structure uniquely identifies an interface queue.
//
typedef struct _XDP_QUEUE_INFO {
    XDP_OBJECT_HEADER Header;

    //
    // Specifies the type of queue.
    //
    XDP_QUEUE_TYPE QueueType;

    //
    // A queue identifier. The meaning of this identifier depends on the queue
    // type.
    //
    UINT32 QueueId;
} XDP_QUEUE_INFO;
```

## Members

TODO

## Remarks

TODO
