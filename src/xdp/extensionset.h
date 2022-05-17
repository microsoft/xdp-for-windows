//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_EXTENSION_SET XDP_EXTENSION_SET;

typedef struct _XDP_EXTENSION_REGISTRATION {
    XDP_EXTENSION_INFO Info;
    UINT8 Size;
    UINT8 Alignment;
} XDP_EXTENSION_REGISTRATION;

NTSTATUS
XdpExtensionSetAssignLayout(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ UINT32 BaseOffset,
    _In_ UINT8 BaseAlignment,
    _Out_ UINT32 *Size,
    _Out_ UINT8 *Alignment
    );

VOID
XdpExtensionSetRegisterEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ XDP_EXTENSION_INFO *Info
    );

VOID
XdpExtensionSetEnableEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    );

VOID
XdpExtensionSetSetInternalEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    );

VOID
XdpExtensionSetResizeEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName,
    _In_ UINT8 Size,
    _In_ UINT8 Alignment
    );

VOID
XdpExtensionSetGetExtension(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

BOOLEAN
XdpExtensionSetIsExtensionEnabled(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    );

NTSTATUS
XdpExtensionSetCreate(
    _In_ XDP_EXTENSION_TYPE Type,
    _In_opt_count_(ReservedExtensionCount) CONST XDP_EXTENSION_REGISTRATION *ReservedExtensions,
    _In_ UINT16 ReservedExtensionCount,
    _Out_ XDP_EXTENSION_SET **ExtensionSet
    );

VOID
XdpExtensionSetCleanup(
    _In_ XDP_EXTENSION_SET *ExtensionSet
    );
