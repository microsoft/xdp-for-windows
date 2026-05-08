# XdpMapInsert function

Inserts or replaces an entry in an XDP map.

## Syntax

```C
XDP_STATUS
XdpMapInsert(
    _In_ HANDLE Map,
    _In_ const VOID *Key,
    _In_ const VOID *Value
    );
```

## Parameters

`Map`

A handle returned by [`XdpMapCreate`](XdpMapCreate.md).

`Key`

A pointer to a key buffer. The size and layout of the buffer are determined by the map's [`XDP_MAP_TYPE`](XDP_MAP_TYPE.md):

| Map type | Key |
| -------- | --- |
| `XDP_MAP_TYPE_XSKMAP` | `UINT32` (must be a valid index for the map). |

`Value`

A pointer to a value buffer. The size and layout of the buffer are determined by the map's [`XDP_MAP_TYPE`](XDP_MAP_TYPE.md):

| Map type | Value |
| -------- | ----- |
| `XDP_MAP_TYPE_XSKMAP` | `HANDLE` to an AF_XDP socket. The map takes a reference on the socket. |

## Remarks

If an entry already exists at `Key`, it is replaced and any prior reference held by the map is released. If `Key` is out of range for the map type, the call fails with an invalid-parameter error.

## See Also

[XDP Maps](../maps.md)
[`XdpMapCreate`](XdpMapCreate.md)
[`XdpMapDelete`](XdpMapDelete.md)
