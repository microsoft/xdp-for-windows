//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

//
// This header defines the API surface between an XDPIF (XDP interface)
// provider and core XDP.
//
// An XDPIF provider's main purpose is to notify core XDP when XDP interfaces
// are added and removed from the system. Upon XDP interface addition, an XDPIF
// provider supplies core XDP with an interface's XDP capabilities and binding
// info. Upon XDP interface removal, an XDPIF provider will notify and wait for
// core XDP to clean up any XDP state associated with the XDP interface.
//
// The main reasons why this API surface exists:
// 1) Separates Windows networking stack specifics from core XDP logic
// 2) Provides a consistent representation of XDP interfaces regardless of XDP
//    mode (native and generic)
//
// In the current implementation, the XDP LWF lib serves as the only XDPIF
// provider.
//
// Terms:
//
// XDP interface (XDPIF_INTERFACE_HANDLE)
//    Represents a logical component underneath core XDP that advertises XDP
//    capabilities. An XDP interface can support only 1 XDP mode (generic or
//    native).
//
// XDP interface set (XDPIF_INTERFACE_SET_HANDLE)
//    Represents a logical networking stack instance (1 instance per NIC) that
//    can provide XDP interfaces. A single XDP interface set can provide at most
//    1 generic XDP interface and 1 native XDP interface.
//

#pragma once

#include <xdp/control.h>
#include <xdpifmode.h>

DECLARE_HANDLE(XDPIF_INTERFACE_HANDLE);
DECLARE_HANDLE(XDPIF_INTERFACE_SET_HANDLE);

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
// Completes an XDP interface remove.
//
typedef
VOID
XDP_REMOVE_INTERFACE_COMPLETE(
    _In_ VOID *InterfaceContext
    );

//
// Parameters for XdpIfAddInterfaces.
//
typedef struct _XDP_ADD_INTERFACE {
    _In_ VOID *InterfaceContext;
    _In_ CONST XDP_CAPABILITIES_INTERNAL *InterfaceCapabilities;
    _In_ XDP_REMOVE_INTERFACE_COMPLETE *RemoveInterfaceComplete;
    _Out_ XDPIF_INTERFACE_HANDLE *InterfaceHandle;
} XDP_ADD_INTERFACE;

//
// Add XDP interfaces to an interface set.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfAddInterfaces(
    _In_ XDPIF_INTERFACE_SET_HANDLE InterfaceSetHandle,
    _Inout_ XDP_ADD_INTERFACE *Interfaces,
    _In_ UINT32 InterfaceCount
    );

//
// Remove XDP interfaces from an interface set.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfRemoveInterfaces(
    _In_ XDPIF_INTERFACE_HANDLE *Interfaces,
    _In_ UINT32 InterfaceCount
    );

//
// Create XDP interfaces.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateInterfaceSet(
    _In_ NET_IFINDEX IfIndex,
    _In_ VOID *InterfaceSetContext,
    _Out_ XDPIF_INTERFACE_SET_HANDLE *InterfaceSetHandle
    );

//
// Delete XDP interfaces.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfDeleteInterfaceSet(
    _In_ XDPIF_INTERFACE_SET_HANDLE InterfaceSetHandle
    );
