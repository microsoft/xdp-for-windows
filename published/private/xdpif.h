//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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

#include <xdpapi.h>
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
// Offload configuration.
//

#define XDP_RSS_INDIRECTION_TABLE_SIZE 128
#define XDP_RSS_HASH_SECRET_KEY_SIZE   40

typedef enum {
    XdpOffloadRss,
    XdpOffloadQeo,
} XDP_INTERFACE_OFFLOAD_TYPE;

typedef enum {
    XdpQueueOffloadTypeChecksum,
    XdpQueueOffloadTypeLso,
    XdpQueueOffloadTypeUso,
    XdpQueueOffloadTypeRsc,
} XDP_QUEUE_OFFLOAD_TYPE;

typedef enum {
    XdpOffloadStateUnspecified,
    XdpOffloadStateEnabled,
    XdpOffloadStateDisabled,
} XDP_OFFLOAD_STATE;

typedef struct _XDP_OFFLOAD_PARAMS_RSS {
    XDP_OFFLOAD_STATE State;
    UINT32 Flags;
    UINT32 HashType;
    UINT16 HashSecretKeySize;
    UINT16 IndirectionTableSize;
    UCHAR HashSecretKey[XDP_RSS_HASH_SECRET_KEY_SIZE];
    PROCESSOR_NUMBER IndirectionTable[XDP_RSS_INDIRECTION_TABLE_SIZE];
} XDP_OFFLOAD_PARAMS_RSS;

typedef struct _XDP_OFFLOAD_PARAMS_CHECKSUM {
    XDP_OFFLOAD_STATE Ipv4Rx;
    XDP_OFFLOAD_STATE Ipv4Tx;
    XDP_OFFLOAD_STATE Tcpv4Rx;
    XDP_OFFLOAD_STATE Tcpv4Tx;
    XDP_OFFLOAD_STATE Tcpv6Rx;
    XDP_OFFLOAD_STATE Tcpv6Tx;
    XDP_OFFLOAD_STATE Udpv4Rx;
    XDP_OFFLOAD_STATE Udpv4Tx;
    XDP_OFFLOAD_STATE Udpv6Rx;
    XDP_OFFLOAD_STATE Udpv6Tx;
} XDP_OFFLOAD_PARAMS_CHECKSUM;

typedef struct _XDP_OFFLOAD_PARAMS_LSO {
    XDP_OFFLOAD_STATE Ipv4;
    XDP_OFFLOAD_STATE Ipv6;
} XDP_OFFLOAD_PARAMS_LSO;

typedef struct _XDP_OFFLOAD_PARAMS_USO {
    XDP_OFFLOAD_STATE Ipv4;
    XDP_OFFLOAD_STATE Ipv6;
} XDP_OFFLOAD_PARAMS_USO;

typedef struct _XDP_OFFLOAD_PARAMS_RSC {
    XDP_OFFLOAD_STATE Ipv4;
    XDP_OFFLOAD_STATE Ipv6;
} XDP_OFFLOAD_PARAMS_RSC;

typedef struct _XDP_OFFLOAD_PARAMS_QEO_CONNECTION {
    LIST_ENTRY TransactionEntry;
    XDP_QUIC_CONNECTION Params;
} XDP_OFFLOAD_PARAMS_QEO_CONNECTION;

typedef struct _XDP_OFFLOAD_PARAMS_QEO {
    LIST_ENTRY Connections;
    UINT32 ConnectionCount;
} XDP_OFFLOAD_PARAMS_QEO;

//
// Open an interface queue offload configuration handle.
//
typedef
NTSTATUS
XDP_OPEN_QUEUE_OFFLOAD_HANDLE(
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ CONST XDP_QUEUE_INFO *QueueInfo,
    _Out_ VOID **QueueOffloadHandle
    );

//
// Query current offload state on an interface queue.
//
typedef
NTSTATUS
XDP_GET_QUEUE_OFFLOAD(
    _In_ VOID *QueueOffloadHandle,
    _In_ XDP_QUEUE_OFFLOAD_TYPE OffloadType,
    _Inout_ VOID *OffloadParams,
    _Inout_ UINT32 *OffloadParamsSize
    );

//
// Configure an offload on an interface queue.
//
typedef
NTSTATUS
XDP_SET_QUEUE_OFFLOAD(
    _In_ VOID *QueueOffloadHandle,
    _In_ XDP_QUEUE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    );

//
// Close an interface queue offload configuration handle.
// This reverts any offload configuration done via the handle.
//
typedef
VOID
XDP_CLOSE_QUEUE_OFFLOAD_HANDLE(
    _In_ VOID *QueueOffloadHandle
    );

//
// Open an interface offload configuration handle.
//
typedef
NTSTATUS
XDP_OPEN_INTERFACE_OFFLOAD_HANDLE(
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Out_ VOID **InterfaceOffloadHandle
    );

//
// Query offload capabilities on an interface.
//
typedef
NTSTATUS
XDP_GET_INTERFACE_OFFLOAD_CAPABILITIES(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadCapabilities,
    _Inout_ UINT32 *OffloadCapabilitiesSize
    );

//
// Query current offload state on an interface.
//
typedef
NTSTATUS
XDP_GET_INTERFACE_OFFLOAD(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadParams,
    _Inout_ UINT32 *OffloadParamsSize
    );

//
// Configure an offload on an interface.
//
typedef
NTSTATUS
XDP_SET_INTERFACE_OFFLOAD(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    );

//
// Close an interface offload configuration handle.
// This reverts any offload configuration done or references added via the
// handle.
//
typedef
VOID
XDP_CLOSE_INTERFACE_OFFLOAD_HANDLE(
    _In_ VOID *InterfaceOffloadHandle
    );

typedef struct _XDP_OFFLOAD_DISPATCH {
    XDP_OPEN_QUEUE_OFFLOAD_HANDLE *OpenQueueOffloadHandle;
    XDP_GET_QUEUE_OFFLOAD *GetQueueOffload;
    XDP_SET_QUEUE_OFFLOAD *SetQueueOffload;
    XDP_CLOSE_QUEUE_OFFLOAD_HANDLE *CloseQueueOffloadHandle;
    XDP_OPEN_INTERFACE_OFFLOAD_HANDLE *OpenInterfaceOffloadHandle;
    XDP_GET_INTERFACE_OFFLOAD_CAPABILITIES *GetInterfaceOffloadCapabilities;
    XDP_GET_INTERFACE_OFFLOAD *GetInterfaceOffload;
    XDP_SET_INTERFACE_OFFLOAD *SetInterfaceOffload;
    XDP_CLOSE_INTERFACE_OFFLOAD_HANDLE *CloseInterfaceOffloadHandle;
} XDP_OFFLOAD_DISPATCH;

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
    _In_ CONST XDP_OFFLOAD_DISPATCH *OffloadDispatch,
    _In_ VOID *InterfaceSetContext,
    _Out_ XDPIF_INTERFACE_SET_HANDLE *InterfaceSetHandle
    );

//
// Delete XDP interface set. The set must be empty.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfDeleteInterfaceSet(
    _In_ XDPIF_INTERFACE_SET_HANDLE InterfaceSetHandle
    );
