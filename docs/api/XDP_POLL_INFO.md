# XDP_OBJECT_HEADER structure

A structure containing NDIS poll API information for an XDP queue.

## Syntax

```C
typedef struct _XDP_POLL_INFO {
    XDP_OBJECT_HEADER Header;

    //
    // A handle to the interface's NDIS polling context for a queue.
    //
    NDIS_HANDLE PollHandle;

    //
    // Indicates whether the polling context is shared with any other queues of
    // the same direction. A single RX/TX queue pair is not considered shared.
    //
    BOOLEAN Shared;
} XDP_POLL_INFO;
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpInitializeSharedPollInfo`](XdpInitializeSharedPollInfo.md)  
[`XdpInitializeExclusivePollInfo`](XdpInitializeExclusivePollInfo.md)  
