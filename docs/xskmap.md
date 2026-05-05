# XSKMAP

## Overview

An XSKMAP is a fixed-size array that maps queue IDs (UINT32 keys) to AF_XDP
socket (XSK) handles. It is designed for use with the
`XDP_PROGRAM_ACTION_REDIRECT_XSKMAP_BY_QUEUEID` program action, which allows a
single XDP program to redirect traffic from multiple receive queues to
different XSK sockets based on the current queue ID.

The XSKMAP has a fixed capacity of 128 entries, corresponding to the maximum
number of RSS indirection table entries in NDIS.

## API

### Creating an XSKMAP

```c
#include <xdpapi.h>

HANDLE XskMap;
HRESULT hr = XdpXskMapCreate(&XskMap);
```

### Inserting an Entry

Insert an XSK socket handle at a given queue ID key:

```c
HRESULT hr = XdpXskMapInsert(XskMap, QueueId, XskSocketHandle);
```

- `Key` must be a valid queue index. Out-of-range keys return an error.
- If an entry already exists at the specified key, it is replaced.
- The XSKMAP takes a reference on the XSK socket handle.

### Deleting an Entry

Remove an entry from the XSKMAP by key:

```c
HRESULT hr = XdpXskMapDelete(XskMap, QueueId);
```

- Deleting a key that has no entry is a no-op (succeeds).

### Closing the XSKMAP

Close the XSKMAP handle to release all resources:

```c
CloseHandle(XskMap);
```

All XSK references held by the map are released when the map is destroyed.

## Using with XDP Programs

### XDP_PROGRAM_ACTION_REDIRECT_XSKMAP_BY_QUEUEID

This action type uses an XSKMAP to look up the XSK target based on the current
receive queue ID. If a matching XSK is found in the map, the packet is
redirected to that socket. If no entry exists for the current queue ID, the
packet is dropped.

```c
XDP_RULE Rule = {0};
Rule.Match = XDP_MATCH_ALL;   // or any other match type
Rule.Action = XDP_PROGRAM_ACTION_REDIRECT_XSKMAP_BY_QUEUEID;
Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSKMAP;
Rule.Redirect.Target = XskMap;

HANDLE Program;
HRESULT hr = XdpCreateProgram(
    IfIndex,
    &XdpInspectRxL2,
    0,                              // QueueId (ignored with ALL_QUEUES)
    XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES,
    &Rule,
    1,
    &Program);
```

This is particularly useful with `XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES`, where a
single program handles all receive queues. Each queue's packets are
automatically routed to the correct XSK socket based on the queue ID.

### Comparison with XDP_PROGRAM_ACTION_REDIRECT

| Feature | REDIRECT | REDIRECT_XSKMAP_QUEUE |
|---------|----------|-----------------------|
| Target | Single XSK handle | XSKMAP (per-queue XSK lookup) |
| Multi-queue | May require per-queue programs | Single program for all queues |
| Dynamic updates | Requires program recreation | Update map entries at any time |
| Miss behavior | N/A (always has target) | Drops packet |

## Example

See the [xskmaprx](../samples/xskmaprx/) sample for a complete example that
creates per-queue XSK sockets and uses an XSKMAP to distribute traffic across
them.

```c
// Create map
HANDLE XskMap;
XdpXskMapCreate(&XskMap);

// Create and insert XSK sockets for each queue
for (UINT32 i = 0; i < QueueCount; i++) {
    HANDLE Xsk;
    XskCreate(&Xsk);
    // ... bind and activate Xsk for queue i ...
    XdpXskMapInsert(XskMap, i, Xsk);
}

// Create program
XDP_RULE Rule = {0};
Rule.Match = XDP_MATCH_ALL;
Rule.Action = XDP_PROGRAM_ACTION_REDIRECT_XSKMAP_BY_QUEUEID;
Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSKMAP;
Rule.Redirect.Target = XskMap;

HANDLE Program;
XdpCreateProgram(IfIndex, &Hook, 0,
    XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES, &Rule, 1, &Program);
```
