//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_EXTENSION_INFO_H
#define XDP_EXTENSION_INFO_H

#include <xdp/objectheader.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _XDP_EXTENSION_TYPE {
    XDP_EXTENSION_TYPE_FRAME,
    XDP_EXTENSION_TYPE_BUFFER,
    XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION,
} XDP_EXTENSION_TYPE;

typedef struct _XDP_EXTENSION_INFO {
    XDP_OBJECT_HEADER Header;
    _Null_terminated_ CONST WCHAR *ExtensionName;
    UINT32 ExtensionVersion;
    XDP_EXTENSION_TYPE ExtensionType;
} XDP_EXTENSION_INFO;

#define XDP_EXTENSION_INFO_REVISION_1 1

#define XDP_SIZEOF_EXTENSION_INFO_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_EXTENSION_INFO, ExtensionType)

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
