# XDP Maps

## Overview

An XDP map is a kernel-managed key/value table that XDP programs can reference
on the data path. Maps are created via `XdpMapCreate(Type, ...)` and populated
via `XdpMapInsert` / `XdpMapDelete`. The currently supported map types are:

| `XDP_MAP_TYPE` | Key | Value | Description |
| -------------- | --- | ----- | ----------- |
| `XDP_MAP_TYPE_XSKMAP` | `UINT32` | AF_XDP socket handle | Bounded-size array of AF_XDP sockets indexed by a `UINT32` key. The key has no inherent meaning to the map; how it is interpreted depends on which redirect target type the program uses to look up entries. The current key range is an implementation detail; out-of-range keys are rejected by `XdpMapInsert` / `XdpMapDelete`. |

## API

### Creating a Map

```c
#include <xdpapi.h>

HANDLE Map;
HRESULT hr = XdpMapCreate(&Map, XDP_MAP_TYPE_XSKMAP);
```

### Inserting an Entry

```c
HRESULT hr = XdpMapInsert(Map, Key, Value);
```

- `Key` must be valid for the map type. Out-of-range keys return an error.
- If an entry already exists at `Key`, it is replaced.
- For `XDP_MAP_TYPE_XSKMAP`, the map takes a reference on the XSK socket
  handle.

### Deleting an Entry

```c
HRESULT hr = XdpMapDelete(Map, Key);
```

Deleting a key that has no entry succeeds.

### Closing a Map

Close the map handle to release all resources:

```c
CloseHandle(Map);
```

All references held by the map are released when the map is destroyed.

## Using Maps with XDP Programs

### XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID

This redirect target type looks up an entry in the rule's target map (which
must be of type `XDP_MAP_TYPE_XSKMAP`) using the current receive queue ID as
the key. A hit redirects the frame to the XSK at that queue ID; a miss drops
the frame.

Note: the queue-ID interpretation of the key is a property of this target
type, not of the XSKMAP itself. The map stores a generic `UINT32` -> XSK
mapping; future target types may use the same XSKMAP with a different key
scheme.

```c
XDP_RULE Rule = {0};
Rule.Match = XDP_MATCH_ALL;
Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID;
Rule.Redirect.Target = Map;

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
single program handles all receive queues and each queue's frames are routed
to the correct entry based on the queue ID.

### Comparison with XDP_REDIRECT_TARGET_TYPE_XSK

| Feature | `TARGET_TYPE_XSK` | `TARGET_TYPE_XSKMAP_BY_QUEUEID` |
|---------|-------------------|---------------------------------|
| Target | Single XSK handle | XSKMAP (per-queue lookup) |
| Multi-queue | May require per-queue programs | Single program for all queues |
| Dynamic updates | Requires program recreation | Update map entries at any time |
| Miss behavior | N/A (always has target) | Drops frame |

## Example

See the [xskmaprx](../samples/xskmaprx/) sample for a complete example that
creates per-queue XSK sockets and uses an XSKMAP to distribute traffic across
them.

```c
// Create map.
HANDLE Map;
XdpMapCreate(&Map, XDP_MAP_TYPE_XSKMAP);

// Create and insert XSK sockets for each queue.
for (UINT32 i = 0; i < QueueCount; i++) {
    HANDLE Xsk;
    XskCreate(&Xsk);
    // ... bind and activate Xsk for queue i ...
    XdpMapInsert(Map, i, Xsk);
}

// Create program.
XDP_RULE Rule = {0};
Rule.Match = XDP_MATCH_ALL;
Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID;
Rule.Redirect.Target = Map;

HANDLE Program;
XdpCreateProgram(IfIndex, &Hook, 0,
    XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES, &Rule, 1, &Program);
```
