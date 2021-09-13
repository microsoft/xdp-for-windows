//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdp/control.h>
#include <xdpifmode.h>

DECLARE_HANDLE(XDP_IF_BINDING_HANDLE);

typedef struct _XDP_CAPABILITIES_INTERNAL {
    //
    // The XDP interface data path mode.
    //
    XDP_INTERFACE_MODE Mode;

    //
    // The supported hook points.
    //
    CONST XDP_HOOK_ID *Hooks;
    UINT32 HookCount;

    //
    // The XDP capabilities reported by the XDP interface.
    //
    CONST XDP_CAPABILITIES_EX *CapabilitiesEx;

    //
    // Size of the capabilities buffer, including the driver API version array.
    //
    UINT32 CapabilitiesSize;
} XDP_CAPABILITIES_INTERNAL;

//
// Completes an XDP interface binding delete.
//
typedef
VOID
XDP_DELETE_BINDING_COMPLETE(
    _In_ VOID *InterfaceContext
    );

//
// Parameters for XdpIfCreateBinding.
//
typedef struct _XDP_REGISTER_IF {
    _In_ VOID *InterfaceContext;
    _In_ CONST XDP_CAPABILITIES_INTERNAL *InterfaceCapabilities;
    _In_ XDP_DELETE_BINDING_COMPLETE *DeleteBindingComplete;
    _Out_ XDP_IF_BINDING_HANDLE BindingHandle;
} XDP_REGISTER_IF;

//
// Create an XDP interface binding.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateBinding(
    _In_ NET_IFINDEX IfIndex,
    _Inout_ XDP_REGISTER_IF *Interfaces,
    _In_ ULONG InterfaceCount
    );

//
// Delete an XDP interface binding.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfDeleteBinding(
    _In_reads_(HandleCount) XDP_IF_BINDING_HANDLE *BindingHandles,
    _In_ ULONG HandleCount
    );
