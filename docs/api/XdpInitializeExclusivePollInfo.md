# XdpInitializeExclusivePollInfo function

Initializes an [`XDP_POLL_INFO](XDP_POLL_INFO.md) poll information object with an exclusive NDIS polling handle. A single RX/TX queue pair using one NDIS polling handle is considered exclusive.

## Syntax

```C
inline
VOID
XdpInitializeExclusivePollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
```

## Parameters

TODO

## Remarks

TODO
