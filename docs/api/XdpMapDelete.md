# XdpMapDelete function

Removes any entry at the specified key from an XDP map.

## Syntax

```C
XDP_STATUS
XdpMapDelete(
    _In_ HANDLE Map,
    _In_ const VOID *Key
    );
```

## Parameters

`Map`

A handle returned by [`XdpMapCreate`](XdpMapCreate.md).

`Key`

A pointer to a key buffer. The size and layout of the buffer are determined by the map's [`XDP_MAP_TYPE`](XDP_MAP_TYPE.md). See [`XdpMapInsert`](XdpMapInsert.md) for the per-type key format.

## Remarks

If `Key` does not currently have an entry in the map, the call succeeds and is a no-op. If `Key` is out of range for the map type, the call fails with an invalid-parameter error.

When an entry is removed, any reference held by the map on the prior value (e.g. an XSK socket reference for `XDP_MAP_TYPE_XSKMAP`) is released.

## See Also

[XDP Maps](../maps.md)
[`XdpMapCreate`](XdpMapCreate.md)
[`XdpMapInsert`](XdpMapInsert.md)
