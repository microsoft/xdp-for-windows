//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDP_EXTENSION_INFO_H
#define XDP_EXTENSION_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

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
    // A completion descriptor extension.
    //
    XDP_EXTENSION_TYPE_COMPLETION,
} XDP_EXTENSION_TYPE;

//
// Structure containing XDP extension information. This structure can be used to
// declare support for an XDP extension and to retrieve the data path extension
// object defined in xdp/extension.h.
//
typedef struct _XDP_EXTENSION_INFO {
    UINT32 Size;

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

//
// Initializes an XDP extension information struct with the given parameters.
//
inline
VOID
XdpInitializeExtensionInfo(
    _Out_ XDP_EXTENSION_INFO *ExtensionInfo,
    _In_z_ CONST WCHAR *ExtensionName,
    _In_ UINT32 ExtensionVersion,
    _In_ XDP_EXTENSION_TYPE ExtensionType
    )
{
    RtlZeroMemory(ExtensionInfo, sizeof(*ExtensionInfo));
    ExtensionInfo->Size = sizeof(*ExtensionInfo);

    ExtensionInfo->ExtensionName = ExtensionName;
    ExtensionInfo->ExtensionVersion = ExtensionVersion;
    ExtensionInfo->ExtensionType = ExtensionType;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
