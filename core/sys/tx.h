//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _XDP_TX_QUEUE XDP_TX_QUEUE;

typedef
VOID
TX_QUEUE_DETACH_EVENT(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ VOID *Client
    );

NTSTATUS
XdpTxQueueCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ UINT32 QueueId,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ VOID *Client,
    _In_ TX_QUEUE_DETACH_EVENT *DetachHandler,
    _In_opt_ XDP_TX_QUEUE_HANDLE ExclusiveTxQueue,
    _Out_ XDP_TX_QUEUE **NewTxQueue
    );

NTSTATUS
XdpTxQueueActivateExclusive(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxDispatch,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue
    );

VOID
XdpTxQueueClose(
    _In_ XDP_TX_QUEUE *TxQueue
    );

CONST XDP_TX_CAPABILITIES *
XdpTxQueueGetCapabilities(
    _In_ XDP_TX_QUEUE *TxQueue
    );

CONST XDP_DMA_CAPABILITIES *
XdpTxQueueGetDmaCapabilities(
    _In_ XDP_TX_QUEUE *TxQueue
    );

NDIS_HANDLE
XdpTxQueueGetInterfacePollHandle(
    _In_ XDP_TX_QUEUE *TxQueue
    );

XDP_TX_QUEUE_CONFIG_ACTIVATE
XdpTxQueueGetConfig(
    _In_ XDP_TX_QUEUE *TxQueue
    );

BOOLEAN
XdpTxQueueIsVirtualAddressEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

BOOLEAN
XdpTxQueueIsLogicalAddressEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

BOOLEAN
XdpTxQueueIsMdlEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

NTSTATUS
XdpTxStart(
    VOID
    );

VOID
XdpTxStop(
    VOID
    );
