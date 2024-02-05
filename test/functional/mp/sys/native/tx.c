//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static const XDP_INTERFACE_TX_QUEUE_DISPATCH MpXdpTxDispatch = {
    MpXdpNotify,
};

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpCreateTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ const XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    UNREFERENCED_PARAMETER(InterfaceContext);
    UNREFERENCED_PARAMETER(Config);
    UNREFERENCED_PARAMETER(InterfaceTxQueue);

    *InterfaceTxQueueDispatch = &MpXdpTxDispatch;

    return STATUS_NOT_SUPPORTED;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MpXdpActivateTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    UNREFERENCED_PARAMETER(InterfaceTxQueue);
    UNREFERENCED_PARAMETER(XdpTxQueue);
    UNREFERENCED_PARAMETER(Config);

    return STATUS_NOT_SUPPORTED;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
MpXdpDeleteTxQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    )
{
    UNREFERENCED_PARAMETER(InterfaceTxQueue);
}
