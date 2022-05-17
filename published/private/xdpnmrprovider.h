//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

DECLARE_HANDLE(XDP_PROVIDER_HANDLE);

typedef
VOID
XDP_PROVIDER_DETACH_HANDLER(
    _In_ VOID *ProviderContext
    );

typedef
NTSTATUS
XDP_GET_INTERFACE_DISPATCH(
    _In_ CONST XDP_VERSION *Version,
    _In_ VOID *GetInterfaceContext,
    _Out_ VOID **InterfaceContext,
    _Out_ CONST VOID **InterfaceDispatch
    );

VOID
XdpCloseProvider(
    _In_ XDP_PROVIDER_HANDLE ProviderHandle
    );

VOID
XdpCleanupProvider(
    _In_ XDP_PROVIDER_HANDLE ProviderHandle
    );

NTSTATUS
XdpOpenProvider(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST GUID *ClientGuid,
    _In_ VOID *ProviderContext,
    _In_ XDP_PROVIDER_DETACH_HANDLER *DetachHandler,
    _Out_ VOID **InterfaceContext,
    _Out_ XDP_GET_INTERFACE_DISPATCH  **GetInterfaceDispatch,
    _Out_ XDP_PROVIDER_HANDLE *ProviderHandle
    );
