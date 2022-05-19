//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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

#define XDP_EXTENSION_INFO_REVISION_1 1

#define XDP_SIZEOF_EXTENSION_INFO_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_EXTENSION_INFO, ExtensionType)

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
    ExtensionInfo->Header.Revision = XDP_EXTENSION_INFO_REVISION_1;
    ExtensionInfo->Header.Size = XDP_SIZEOF_EXTENSION_INFO_REVISION_1;

    ExtensionInfo->ExtensionName = ExtensionName;
    ExtensionInfo->ExtensionVersion = ExtensionVersion;
    ExtensionInfo->ExtensionType = ExtensionType;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
