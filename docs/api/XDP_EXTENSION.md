# XDP_EXTENSION structure

A structure defining an XDP data path extension.

## Syntax

```C
//
// Structure defining an XDP data path extension.
//
typedef struct _XDP_EXTENSION {
    //
    // This field is reserved for XDP platform use.
    //
    UINT16 Reserved;
} XDP_EXTENSION;
```

## Members

### Reserved

A 16-bit field reserved for XDP platform use. This field contains the byte offset from the descriptor base to the extension data. Applications and drivers should not access this field directly; instead use the [`XdpGetExtensionData`](../descriptor-extensions.md#accessing-extensions) helper function or extension-specific getter functions.

## Remarks

The `XDP_EXTENSION` structure serves as an opaque handle to an XDP descriptor extension. It is obtained by querying for a specific extension using [`XDP_EXTENSION_INFO`](XDP_EXTENSION_INFO.md) via socket options (for AF_XDP applications) or driver APIs (for XDP drivers).

### Usage Pattern

1. **Initialize extension info**: Use [`XdpInitializeExtensionInfo`](XdpInitializeExtensionInfo.md) to create an [`XDP_EXTENSION_INFO`](XDP_EXTENSION_INFO.md) structure describing the desired extension
2. **Query for extension**: Register the extension with XskSetSockopt (AF_XDP) or driver APIs
3. **Retrieve extension handle**: After binding/queue creation, query to get the populated `XDP_EXTENSION` structure
4. **Access extension data**: Use `XdpGetExtensionData()` or extension-specific helper functions like `XdpGetFragmentExtension()` to access extension data for each descriptor

### Thread Safety

Once obtained, an `XDP_EXTENSION` handle can be safely accessed from multiple threads concurrently as long as the associated queue/socket remains active. The extension offset is immutable after initialization.

### Performance Considerations

Extension handles should be cached during initialization rather than queried per-packet. The small size (2 bytes) makes extension handles suitable for stack allocation or embedding in context structures.

For more information about the extension model, see [Descriptor Extensions](../descriptor-extensions.md).

## See Also

[`XDP_EXTENSION_INFO`](XDP_EXTENSION_INFO.md)
