# XDP_REDIRECT_TARGET_TYPE enumeration

Identifies the type of target for an `XDP_PROGRAM_ACTION_REDIRECT` rule action. The target type determines how the rule's `Target` handle is interpreted at runtime.

## Syntax

```C
typedef enum _XDP_REDIRECT_TARGET_TYPE {
    XDP_REDIRECT_TARGET_TYPE_XSK,
    XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID,
} XDP_REDIRECT_TARGET_TYPE;
```

## Constants

`XDP_REDIRECT_TARGET_TYPE_XSK`

The `Target` is an AF_XDP socket `HANDLE`. Matching frames are redirected directly to that socket.

`XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID`

The `Target` is a handle to an XDP map of type [`XDP_MAP_TYPE_XSKMAP`](XDP_MAP_TYPE.md). For each matching frame, XDP looks up the entry whose key equals the current receive queue ID. If a matching entry is present, the frame is redirected to that XSK; otherwise, the frame is dropped.

The queue-ID interpretation of the key is a property of this target type, not of the XSKMAP itself.

## Remarks

The `XDP_REDIRECT_TARGET_TYPE` is set in [`XDP_REDIRECT_PARAMS`](XDP_RULE_ACTION.md) when constructing an [`XDP_RULE`](XDP_RULE.md) with `Action == XDP_PROGRAM_ACTION_REDIRECT`.

## See Also

[XDP Maps](../maps.md)
[`XDP_RULE`](XDP_RULE.md)
[`XDP_RULE_ACTION`](XDP_RULE_ACTION.md)
[`XDP_MAP_TYPE`](XDP_MAP_TYPE.md)
