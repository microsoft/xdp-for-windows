# XdpInitializeSharedPollInfo function

Initializes an [`XDP_POLL_INFO`](XDP_POLL_INFO.md) poll information object with a shared NDIS polling handle. An NDIS poll handle is considered shared if multiple interface queues use the same handle in the same data path direction. A single RX/TX queue pair is not considered shared.

## Syntax

```C
inline
VOID
XdpInitializeSharedPollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
```

## Parameters

TODO

## Remarks

TODO
