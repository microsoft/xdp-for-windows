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

**Header**  
An [`XDP_OBJECT_HEADER`](XDP_OBJECT_HEADER.md) structure that specifies the object type and size. Must be initialized to `{XDP_OBJECT_TYPE_EXTENSION_INFO, XDP_SIZEOF_EXTENSION_INFO}`.

**ExtensionName**  
A null-terminated wide character string specifying the name of the XDP extension. Only XDP-defined extensions are currently supported. Common extension names include:
- `L"XDP_EXTENSION_PACKET_LAYOUT"` - Provides packet layout information
- `L"XDP_EXTENSION_INTERFACE_CONTEXT"` - Interface-specific context data
- `L"XDP_EXTENSION_FRAGMENT"` - Fragment descriptor information

**ExtensionVersion**  
A 32-bit unsigned integer specifying the version of the extension. This allows for versioning of extension interfaces to maintain compatibility.

**ExtensionType**  
An [`XDP_EXTENSION_TYPE`](#xdp_extension_type) enumeration value that specifies the type of extension:
- `XDP_EXTENSION_TYPE_FRAME` - Extension applies to frame descriptors
- `XDP_EXTENSION_TYPE_BUFFER` - Extension applies to buffer descriptors  
- `XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION` - Extension applies to TX frame completion descriptors

## Remarks

Extensions provide a way to access additional metadata and functionality beyond the basic packet data. They are typically used by advanced applications that need access to hardware-specific features or additional packet information.

To use an extension:
1. Initialize an `XDP_EXTENSION_INFO` structure with the extension details
2. Call the appropriate XDP API to register or query the extension
3. Use the returned [`XDP_EXTENSION`](XDP_EXTENSION.md) object to access extension data

Extension support varies by network interface and driver capabilities. Applications should always check for extension availability before attempting to use them.

## See Also

[`XdpInitializeExtensionInfo`](XdpInitializeExtensionInfo.md)
