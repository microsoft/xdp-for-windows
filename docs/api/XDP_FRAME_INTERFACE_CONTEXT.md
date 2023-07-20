# XDP_FRAME_INTERFACE_CONTEXT structure

Provides opaque per-frame storage for XDP interface drivers.

## Syntax

```C
//
// Opaque, variable-sized context reserved for use by the XDP interface in each
// XDP frame; the XDP platform will not read/write this extension.
//
typedef VOID XDP_FRAME_INTERFACE_CONTEXT;

#define XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME L"ms_frame_interface_context"
#define XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetFrameInterfaceContextExtension`](XdpGetFrameInterfaceContextExtension.md)
