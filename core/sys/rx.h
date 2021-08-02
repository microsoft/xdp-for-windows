//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _XDP_RX_QUEUE XDP_RX_QUEUE;

XDP_RX_QUEUE *
XdpRxQueueFind(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    );

NTSTATUS
XdpRxQueueFindOrCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_RX_QUEUE **RxQueue
    );

typedef struct _XDP_RX_QUEUE_NOTIFY_ENTRY XDP_RX_QUEUE_NOTIFICATION_ENTRY;

typedef enum _XDP_RX_QUEUE_NOTIFICATION_TYPE {
    XDP_RX_QUEUE_NOTIFICATION_ATTACH,
    XDP_RX_QUEUE_NOTIFICATION_DETACH,
    XDP_RX_QUEUE_NOTIFICATION_DETACH_COMPLETE,
    XDP_RX_QUEUE_NOTIFICATION_DELETE,
} XDP_RX_QUEUE_NOTIFICATION_TYPE;

typedef
VOID
XDP_RX_QUEUE_NOTIFY(
    XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    );

typedef struct _XDP_RX_QUEUE_NOTIFY_ENTRY {
    LIST_ENTRY Link;
    XDP_RX_QUEUE_NOTIFY *NotifyRoutine;
} XDP_RX_QUEUE_NOTIFICATION_ENTRY;

VOID
XdpRxQueueRegisterNotifications(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Inout_ XDP_RX_QUEUE_NOTIFICATION_ENTRY *Entry,
    _In_ XDP_RX_QUEUE_NOTIFY *NotifyRoutine
    );

VOID
XdpRxQueueDeregisterNotifications(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Inout_ XDP_RX_QUEUE_NOTIFICATION_ENTRY *Entry
    );

VOID
XdpRxQueueDereference(
    _In_ XDP_RX_QUEUE *RxQueue
    );

VOID
XdpRxQueueSync(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    );

NTSTATUS
XdpRxQueueSetProgram(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_ XDP_PROGRAM *Program
    );

XDP_PROGRAM *
XdpRxQueueGetProgram(
    _In_ XDP_RX_QUEUE *RxQueue
    );

NDIS_HANDLE
XdpRxQueueGetInterfacePollHandle(
    _In_ XDP_RX_QUEUE *RxQueue
    );

XDP_RX_QUEUE_CONFIG_ACTIVATE
XdpRxQueueGetConfig(
    _In_ XDP_RX_QUEUE *RxQueue
    );

BOOLEAN
XdpRxQueueIsRxBatchEnabled(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

UINT8
XdpRxQueueGetMaxmimumFragments(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

NTSTATUS
XdpRxStart(
    VOID
    );

VOID
XdpRxStop(
    VOID
    );
