//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This file contains declarations for the XDP Driver API control path.
//

#pragma once

#include <xdp/datapath.h>
#include <xdp/guid.h>
#include <xdp/interfaceconfig.h>
#include <xdp/rtl.h>
#include <xdp/rxqueueconfig.h>
#include <xdp/txqueueconfig.h>

EXTERN_C_START

#pragma warning(push)
#pragma warning(disable:4214) // nonstandard extension used: bit field types other than int

//
// Forward declaration of XDP interface dispatch table.
//
typedef struct _XDP_INTERFACE_DISPATCH XDP_INTERFACE_DISPATCH;

DECLARE_HANDLE(XDP_REGISTRATION_HANDLE);

typedef
_When_(DriverApiVersionCount == 0, _Struct_size_bytes_(Header.Size))
_When_(DriverApiVersionCount > 0, _Struct_size_bytes_(DriverApiVersionsOffset + DriverApiVersionCount * sizeof(XDP_VERSION)))
struct _XDP_CAPABILITIES_EX {
    XDP_OBJECT_HEADER Header;

    UINT32 DriverApiVersionsOffset;
    UINT32 DriverApiVersionCount;
    XDP_VERSION DdkDriverApiVersion;
    GUID InstanceId;
} XDP_CAPABILITIES_EX;

#define XDP_CAPABILITIES_EX_REVISION_1 1

#define XDP_SIZEOF_CAPABILITIES_EX_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_CAPABILITIES_EX, InstanceId)

typedef struct _XDP_CAPABILITIES {
    XDP_CAPABILITIES_EX CapabilitiesEx;
    XDP_VERSION DriverApiVersion;
} XDP_CAPABILITIES;

inline
NTSTATUS
XdpInitializeCapabilities(
    _Out_ XDP_CAPABILITIES *Capabilities,
    _In_ CONST XDP_VERSION *DriverApiVersion
    )
{
    CONST XDP_VERSION DdkDriverApiVersion = {
        XDP_DRIVER_API_MAJOR_VER,
        XDP_DRIVER_API_MINOR_VER,
        XDP_DRIVER_API_PATCH_VER
    };

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->CapabilitiesEx.Header.Revision = XDP_CAPABILITIES_EX_REVISION_1;
    Capabilities->CapabilitiesEx.Header.Size = XDP_SIZEOF_CAPABILITIES_EX_REVISION_1;

    Capabilities->CapabilitiesEx.DriverApiVersionsOffset =
        FIELD_OFFSET(XDP_CAPABILITIES, DriverApiVersion);
    Capabilities->CapabilitiesEx.DriverApiVersionCount = 1;

    Capabilities->DriverApiVersion = *DriverApiVersion;
    Capabilities->CapabilitiesEx.DdkDriverApiVersion = DdkDriverApiVersion;

    return XdpGuidCreate(&Capabilities->CapabilitiesEx.InstanceId);
}

NTSTATUS
XdpRegisterInterfaceEx(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_CAPABILITIES_EX *CapabilitiesEx,
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    );

inline
NTSTATUS
XdpRegisterInterface(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_CAPABILITIES *Capabilities,
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    )
{
    return
        XdpRegisterInterfaceEx(
            InterfaceIndex, &Capabilities->CapabilitiesEx, InterfaceContext,
            InterfaceDispatch, RegistrationHandle);
}

VOID
XdpDeregisterInterface(
    _In_ XDP_REGISTRATION_HANDLE RegistrationHandle
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_OPEN_INTERFACE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_INTERFACE_CONFIG InterfaceConfig
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_CLOSE_INTERFACE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext
    );

typedef struct _XDP_INTERFACE_RX_QUEUE_DISPATCH {
    XDP_INTERFACE_NOTIFY_QUEUE *InterfaceNotifyQueue;
} XDP_INTERFACE_RX_QUEUE_DISPATCH;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_CREATE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_ACTIVATE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_DELETE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    );

typedef struct _XDP_INTERFACE_TX_QUEUE_DISPATCH {
    XDP_INTERFACE_NOTIFY_QUEUE *InterfaceNotifyQueue;
} XDP_INTERFACE_TX_QUEUE_DISPATCH;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_CREATE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_ACTIVATE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_DELETE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    );

typedef struct _XDP_INTERFACE_DISPATCH {
    XDP_OBJECT_HEADER       Header;
    XDP_OPEN_INTERFACE      *OpenInterface;
    XDP_CLOSE_INTERFACE     *CloseInterface;
    XDP_CREATE_RX_QUEUE     *CreateRxQueue;
    XDP_ACTIVATE_RX_QUEUE   *ActivateRxQueue;
    XDP_DELETE_RX_QUEUE     *DeleteRxQueue;
    XDP_CREATE_TX_QUEUE     *CreateTxQueue;
    XDP_ACTIVATE_TX_QUEUE   *ActivateTxQueue;
    XDP_DELETE_TX_QUEUE     *DeleteTxQueue;
} XDP_INTERFACE_DISPATCH;

#define XDP_INTERFACE_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_INTERFACE_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_INTERFACE_DISPATCH, DeleteTxQueue)

#pragma warning(pop)

EXTERN_C_END
