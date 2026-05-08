# XDP Maps

## Overview

An XDP map is a kernel-managed key/value table that XDP programs can reference on the data path. Maps are created from user mode and populated with entries that XDP rules look up at runtime.

## Map types

| `XDP_MAP_TYPE` | Description |
| -------------- | ----------- |
| `XDP_MAP_TYPE_XSKMAP` | Bounded-size array of AF_XDP sockets indexed by a `UINT32` key. |

The key has no inherent meaning to the map; it is interpreted by whichever redirect target type the program uses to look up entries. See [`XDP_MAP_TYPE`](api/XDP_MAP_TYPE.md) for details. The set of valid keys for a given map type is an implementation detail; out-of-range keys are rejected by `XdpMapInsert` / `XdpMapDelete`.

## API

| Function | Purpose |
| -------- | ------- |
| [`XdpMapCreate`](api/XdpMapCreate.md) | Create a new map of a given type. |
| [`XdpMapInsert`](api/XdpMapInsert.md) | Insert or replace an entry. |
| [`XdpMapDelete`](api/XdpMapDelete.md) | Remove an entry. |
| `CloseHandle` | Destroy the map and release all entry references. |

## Using a map from an XDP program

Maps are referenced from an [`XDP_RULE`](api/XDP_RULE.md) with `Action == XDP_PROGRAM_ACTION_REDIRECT` and an appropriate [`XDP_REDIRECT_TARGET_TYPE`](api/XDP_REDIRECT_TARGET_TYPE.md). The current map-aware target types are:

- [`XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID`](api/XDP_REDIRECT_TARGET_TYPE.md) - look up an XSK in an XSKMAP using the current receive queue ID.

The XDP program takes its own reference on the map; closing the user-mode map handle does not invalidate any program already attached to the map.

## Example

See the [xskmaprx](../samples/xskmaprx/) sample for a complete example that creates per-queue XSK sockets and uses an XSKMAP to distribute received traffic across them.
