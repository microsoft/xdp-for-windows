# XDP_MAP_TYPE enumeration

Identifies the type of an XDP map. The type is fixed at map creation and determines the size of the keys and values stored in the map, as well as how XDP programs may reference the map at runtime.

## Syntax

```C
typedef enum _XDP_MAP_TYPE {
    XDP_MAP_TYPE_XSKMAP = 0,
} XDP_MAP_TYPE;
```

## Constants

`XDP_MAP_TYPE_XSKMAP`

A bounded-size array of AF_XDP socket handles indexed by `UINT32` keys. Values are XSK socket `HANDLE`s; the map takes a reference on each inserted socket.

## Remarks

The map itself does not interpret the meaning of the key. Key semantics are imposed by the program redirect target type that consumes the map (for example, [`XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID`](XDP_REDIRECT_TARGET_TYPE.md) interprets the key as the current receive queue ID).

## See Also

[XDP Maps](../maps.md)
[`XdpMapCreate`](XdpMapCreate.md)
