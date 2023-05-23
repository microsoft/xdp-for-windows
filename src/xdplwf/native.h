//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_LWF_NATIVE {
    XDP_LWF_FILTER *Filter;
    NDIS_HANDLE NdisFilterHandle;
    XDP_CAPABILITIES_INTERNAL Capabilities;
    NET_IFINDEX IfIndex;

    XDPIF_INTERFACE_HANDLE XdpIfInterfaceHandle;
    KEVENT InterfaceRemovedEvent;
} XDP_LWF_NATIVE;

NTSTATUS
XdpNativeAttachInterface(
    _Inout_ XDP_LWF_NATIVE *Native,
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex,
    _Out_ XDP_ADD_INTERFACE *AddIf
    );

VOID
XdpNativeDetachInterface(
    _In_ XDP_LWF_NATIVE *Native
    );

VOID
XdpNativeWaitForDetachInterfaceComplete(
    _In_ XDP_LWF_NATIVE *Native
    );
