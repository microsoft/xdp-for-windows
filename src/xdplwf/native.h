//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _XDP_LWF_NATIVE {
    NDIS_HANDLE NdisFilterHandle;
    XDP_CAPABILITIES_INTERNAL Capabilities;
    NET_IFINDEX IfIndex;

    XDP_IF_BINDING_HANDLE BindingHandle;
    KEVENT *BindingDeletedEvent;
} XDP_LWF_NATIVE;

NTSTATUS
XdpNativeCreateBinding(
    _Inout_ XDP_LWF_NATIVE *Native,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex,
    _Out_ XDP_REGISTER_IF *RegisterIf
    );

VOID
XdpNativeDeleteBinding(
    _In_ XDP_LWF_NATIVE *Native
    );
