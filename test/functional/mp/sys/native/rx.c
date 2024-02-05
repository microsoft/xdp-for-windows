//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static const XDP_INTERFACE_RX_QUEUE_DISPATCH MpXdpRxDispatch = {
    MpXdpNotify,
};

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpCreateRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ const XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    UNREFERENCED_PARAMETER(InterfaceContext);
    UNREFERENCED_PARAMETER(Config);
    UNREFERENCED_PARAMETER(InterfaceRxQueue);

    *InterfaceRxQueueDispatch = &MpXdpRxDispatch;

    return STATUS_NOT_SUPPORTED;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpActivateRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    UNREFERENCED_PARAMETER(InterfaceRxQueue);
    UNREFERENCED_PARAMETER(XdpRxQueue);
    UNREFERENCED_PARAMETER(Config);

    return STATUS_NOT_SUPPORTED;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
MpXdpDeleteRxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    )
{
    UNREFERENCED_PARAMETER(InterfaceRxQueue);
}
