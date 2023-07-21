# XDP_BUFFER_INTERFACE_CONTEXT structure

Provides opaque per-buffer storage for XDP interface drivers.

## Syntax

```C
//
// Opaque, variable-sized context reserved for use by the XDP interface in each
// XDP buffer; the XDP platform will not read/write this extension.
//
typedef VOID XDP_BUFFER_INTERFACE_CONTEXT;

#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME L"ms_buffer_interface_context"
#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetBufferInterfaceContextExtension`](XdpGetBufferInterfaceContextExtension.md)
