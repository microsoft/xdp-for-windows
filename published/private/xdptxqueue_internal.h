//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdp/details/txqueueconfig.h>

typedef
CONST XDP_HOOK_ID *
XDP_TX_QUEUE_CREATE_GET_HOOK_ID(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

typedef struct _XDP_TX_QUEUE_NOTIFY_HANDLE *XDP_TX_QUEUE_NOTIFY_HANDLE;
typedef
XDP_TX_QUEUE_NOTIFY_HANDLE
XDP_TX_QUEUE_CREATE_GET_NOTIFY_HANDLE(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

typedef struct _XDP_TX_QUEUE_CONFIG_RESERVED {
    UINT32 Size;
    XDP_TX_QUEUE_CREATE_GET_HOOK_ID         *GetHookId;
    XDP_TX_QUEUE_CREATE_GET_NOTIFY_HANDLE   *GetNotifyHandle;
} XDP_TX_QUEUE_CONFIG_RESERVED;

inline
CONST XDP_HOOK_ID *
XdpTxQueueGetHookId(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    CONST XDP_TX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        !RTL_CONTAINS_FIELD(Reserved, Reserved->Size, GetHookId) ||
        Reserved->GetHookId == NULL) {
        return NULL;
    }

    return Reserved->GetHookId(TxQueueConfig);
}

inline
XDP_TX_QUEUE_NOTIFY_HANDLE
XdpTxQueueGetNotifyHandle(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    CONST XDP_TX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        !RTL_CONTAINS_FIELD(Reserved, Reserved->Size, GetNotifyHandle) ||
        Reserved->GetNotifyHandle == NULL) {
        return NULL;
    }

    return Reserved->GetNotifyHandle(TxQueueConfig);
}

typedef enum _XDP_TX_QUEUE_NOTIFY_CODE {
    //
    // The TX queue's MTU has changed. The XDP platform will mark the TX queue
    // for deletion.
    //
    XDP_TX_QUEUE_NOTIFY_MAX_FRAME_SIZE,
} XDP_TX_QUEUE_NOTIFY_CODE;

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_TX_QUEUE_NOTIFY(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle,
    _In_ XDP_TX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ CONST VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    );

XDP_TX_QUEUE_NOTIFY XdpTxQueueNotify;

typedef struct _XDP_TX_QUEUE_NOTIFY_DISPATCH {
    UINT32 Size;
    XDP_TX_QUEUE_NOTIFY                     *Notify;
} XDP_TX_QUEUE_NOTIFY_DISPATCH;

typedef struct _XDP_TX_QUEUE_NOTIFY_DETAILS {
    CONST XDP_TX_QUEUE_NOTIFY_DISPATCH *Dispatch;
} XDP_TX_QUEUE_NOTIFY_DETAILS;

inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDPEXPORT(XdpTxQueueNotify)(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle,
    _In_ XDP_TX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ CONST VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    )
{
    CONST XDP_TX_QUEUE_NOTIFY_DETAILS *Details = (CONST XDP_TX_QUEUE_NOTIFY_DETAILS *)TxQueueNotifyHandle;
    CONST XDP_TX_QUEUE_NOTIFY_DISPATCH *Dispatch = Details->Dispatch;

    ASSERT(Dispatch != NULL);
    ASSERT(RTL_CONTAINS_FIELD(Dispatch, Dispatch->Size, Notify));

    Dispatch->Notify(TxQueueNotifyHandle, NotifyCode, NotifyBuffer, NotifyBufferSize);
}
