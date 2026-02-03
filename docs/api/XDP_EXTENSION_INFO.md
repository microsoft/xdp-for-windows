# XDP_EXTENSION structure

A structure containing XDP extension information. This structure can be used to declare support for an XDP extension and to retrieve the data path extension object [`XDP_EXTENSION`](XDP_EXTENSION.md).

## Syntax

```C
//
// Enumeration of XDP extension types.
//
typedef enum _XDP_EXTENSION_TYPE {
    //
    // A frame descriptor extension.
    //
    XDP_EXTENSION_TYPE_FRAME,

    //
    // A buffer descriptor extension.
    //
    XDP_EXTENSION_TYPE_BUFFER,

    //
    // A TX frame completion descriptor extension.
    //
    XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION,
} XDP_EXTENSION_TYPE;

//
// Structure containing XDP extension information. This structure can be used to
// declare support for an XDP extension and to retrieve the data path extension
// object defined in xdp/extension.h.
//
typedef struct _XDP_EXTENSION_INFO {
    XDP_OBJECT_HEADER Header;

    //
    // The extension name. Only XDP-defined extensions are currently supported.
    //
    _Null_terminated_ const WCHAR *ExtensionName;

    //
    // The extension version.
    //
    UINT32 ExtensionVersion;

    //
    // The extension type.
    //
    XDP_EXTENSION_TYPE ExtensionType;
} XDP_EXTENSION_INFO;
```

## Members

### Header

An [`XDP_OBJECT_HEADER`](XDP_OBJECT_HEADER.md) that identifies the structure type and version. Must be initialized using [`XdpInitializeExtensionInfo`](XdpInitializeExtensionInfo.md).

### ExtensionName

A null-terminated wide-character string specifying the name of the extension. This name identifies the extension to the XDP platform. Only XDP-defined extensions (with names starting with `ms_`) are currently supported.

Examples:
- `ms_frame_fragment` - Frame fragment extension
- `ms_frame_layout` - Frame layout extension
- `ms_buffer_virtual_address` - Buffer virtual address extension

See [Descriptor Extensions](../descriptor-extensions.md#available-extensions) for a complete list of available extension names.

### ExtensionVersion

A 32-bit unsigned integer specifying the version of the extension. Each extension defines its own versioning scheme (typically starting at version 1). Applications should specify the version they were built against.

### ExtensionType

An [`XDP_EXTENSION_TYPE`](#xdp_extension_type) value specifying the type of descriptor this extension applies to:
- `XDP_EXTENSION_TYPE_FRAME` - Frame descriptor extension
- `XDP_EXTENSION_TYPE_BUFFER` - Buffer descriptor extension  
- `XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION` - TX frame completion descriptor extension

## Remarks

The `XDP_EXTENSION_INFO` structure is used to declare support for and query XDP descriptor extensions. Extensions enable variable-sized metadata to be attached to frame, buffer, or TX completion descriptors.

### Extension Registration

Applications and drivers use this structure to inform the XDP platform which extensions they need:

**For AF_XDP applications:**
```c
XDP_EXTENSION_INFO extensionInfo;
XdpInitializeExtensionInfo(
    &extensionInfo,
    XDP_FRAME_EXTENSION_FRAGMENT_NAME,
    XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1,
    XDP_EXTENSION_TYPE_FRAME);

// Register the extension
XskSetSockopt(socket, XSK_SOCKOPT_RX_EXTENSION, &extensionInfo, sizeof(extensionInfo));
```

**For XDP drivers:**
Drivers use the XDP Driver API to register and query extensions during queue creation.

### Extension Negotiation

The XDP platform negotiates extensions between sockets/programs and drivers:
1. Applications/programs declare which extensions they want to use
2. Drivers declare which extensions they support
3. XDP enables the intersection of requested and supported extensions
4. Both parties retrieve [`XDP_EXTENSION`](XDP_EXTENSION.md) handles to access enabled extensions

If an extension is not supported by the underlying driver, the registration may fail or succeed with the extension disabled, depending on whether the extension is mandatory or optional for the requester.

### Extension Versioning

Each extension has independent versioning. When requesting an extension, specify the version you support. The XDP platform will:
- Grant the request if the exact version is available
- May succeed with a compatible version (implementation-defined)
- Fail if no compatible version exists

### Available Extensions

XDP for Windows defines several built-in extensions for common use cases such as multi-buffer frames, checksum offload, timestamps, and memory mappings. See [Descriptor Extensions](../descriptor-extensions.md) for detailed information about the extension model and a complete list of available extensions.

## See Also

[`XdpInitializeExtensionInfo`](XdpInitializeExtensionInfo.md)
