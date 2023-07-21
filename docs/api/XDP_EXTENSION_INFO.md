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
    _Null_terminated_ CONST WCHAR *ExtensionName;

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

TODO

## Remarks

TODO

## See Also

[`XdpInitializeExtensionInfo`](XdpInitializeExtensionInfo.md)
