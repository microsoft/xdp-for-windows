# XdpMapCreate function

Creates a new XDP map of the specified type.

## Syntax

```C
XDP_STATUS
XdpMapCreate(
    _Out_ HANDLE *Map,
    _In_ XDP_MAP_TYPE Type
    );
```

## Parameters

`Map`

If the map is created successfully, returns a Windows handle to the map. The map is destroyed by XDP when the handle is closed via `CloseHandle`; all entry references held by the map are released at that time.

`Type`

The [`XDP_MAP_TYPE`](XDP_MAP_TYPE.md) of map to create. The type determines the key and value formats accepted by [`XdpMapInsert`](XdpMapInsert.md) and [`XdpMapDelete`](XdpMapDelete.md).

## Remarks

The map handle returned by `XdpMapCreate` may be passed as a redirect target in an [`XDP_RULE`](XDP_RULE.md) using a map-aware [`XDP_REDIRECT_TARGET_TYPE`](XDP_REDIRECT_TARGET_TYPE.md). The XDP program takes its own reference on the map; closing the handle from user mode does not invalidate any program already attached to the map.

## See Also

[XDP Maps](../maps.md)
[`XdpMapInsert`](XdpMapInsert.md)
[`XdpMapDelete`](XdpMapDelete.md)
[`XDP_MAP_TYPE`](XDP_MAP_TYPE.md)
