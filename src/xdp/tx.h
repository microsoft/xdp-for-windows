//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_TX_QUEUE XDP_TX_QUEUE;

//
// Data path routines.
//
VOID
XdpTxQueueInvokeInterfaceNotify(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    );

XDP_PCW_TX_QUEUE *
XdpTxQueueGetStats(
    _In_ XDP_TX_QUEUE *TxQueue
    );

//
// Control path routines.
//

NTSTATUS
XdpTxQueueFindOrCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_TX_QUEUE **TxQueue
    );

VOID
XdpTxQueueDereference(
    _In_ XDP_TX_QUEUE *TxQueue
    );

typedef struct _XDP_TX_QUEUE_NOTIFY_ENTRY XDP_TX_QUEUE_NOTIFICATION_ENTRY;

typedef enum _XDP_TX_QUEUE_NOTIFICATION_TYPE {
    XDP_TX_QUEUE_NOTIFICATION_DETACH,
} XDP_TX_QUEUE_NOTIFICATION_TYPE;

typedef
VOID
XDP_TX_QUEUE_NOTIFICATION_ROUTINE(
    XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_TX_QUEUE_NOTIFICATION_TYPE NotificationType
    );

typedef struct _XDP_TX_QUEUE_NOTIFY_ENTRY {
    LIST_ENTRY Link;
    XDP_TX_QUEUE_NOTIFICATION_ROUTINE *NotifyRoutine;
} XDP_TX_QUEUE_NOTIFICATION_ENTRY;

VOID
XdpTxQueueRegisterNotifications(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry,
    _In_ XDP_TX_QUEUE_NOTIFICATION_ROUTINE *NotifyRoutine
    );

VOID
XdpTxQueueDeregisterNotifications(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry
    );

typedef enum _XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE {
    XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE_XSK,
} XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE;

typedef struct _XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY {
    LIST_ENTRY Link;
} XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY;

NTSTATUS
XdpTxQueueAddDatapathClient(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry,
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE TxClientType
    );

VOID
XdpTxQueueRemoveDatapathClient(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry
    );

VOID
XdpTxQueueSync(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
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
