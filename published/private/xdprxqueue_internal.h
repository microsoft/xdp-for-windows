//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdp/details/rxqueueconfig.h>

typedef
CONST XDP_HOOK_ID *
XDP_RX_QUEUE_CREATE_GET_HOOK_ID(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef struct _XDP_RX_QUEUE_NOTIFY_HANDLE *XDP_RX_QUEUE_NOTIFY_HANDLE;
typedef
XDP_RX_QUEUE_NOTIFY_HANDLE
XDP_RX_QUEUE_CREATE_GET_NOTIFY_HANDLE(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef struct _XDP_RX_QUEUE_CONFIG_RESERVED {
    XDP_OBJECT_HEADER               Header;
    XDP_RX_QUEUE_CREATE_GET_HOOK_ID *GetHookId;
    XDP_RX_QUEUE_CREATE_GET_NOTIFY_HANDLE *GetNotifyHandle;
} XDP_RX_QUEUE_CONFIG_RESERVED;

#define XDP_RX_QUEUE_CONFIG_RESERVED_REVISION_1 1

#define XDP_SIZEOF_RX_QUEUE_CONFIG_RESERVED_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_QUEUE_CONFIG_RESERVED, GetHookId)

inline
CONST XDP_HOOK_ID *
XdpRxQueueGetHookId(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    const XDP_RX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        Reserved->Header.Revision < XDP_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->Header.Size < XDP_SIZEOF_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->GetHookId == NULL) {
        return NULL;
    }

    return Reserved->GetHookId(RxQueueConfig);
}

inline
XDP_RX_QUEUE_NOTIFY_HANDLE
XdpRxQueueGetNotifyHandle(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    const XDP_RX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        Reserved->Header.Revision < XDP_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->Header.Size < XDP_SIZEOF_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->GetNotifyHandle == NULL) {
        return NULL;
    }

    return Reserved->GetNotifyHandle(RxQueueConfig);
}

typedef enum _XDP_RX_QUEUE_NOTIFY_CODE {
    //
    // The RX queue's current offload configuration has changed.
    //
    XDP_RX_QUEUE_NOTIFY_OFFLOAD_CURRENT_CONFIG,
} XDP_RX_QUEUE_NOTIFY_CODE;

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_RX_QUEUE_NOTIFY(
    _In_ XDP_RX_QUEUE_NOTIFY_HANDLE RxQueueNotifyHandle,
    _In_ XDP_RX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ const VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    );

XDP_RX_QUEUE_NOTIFY XdpRxQueueNotify;

typedef struct _XDP_RX_QUEUE_NOTIFY_DISPATCH {
    XDP_OBJECT_HEADER   Header;
    XDP_RX_QUEUE_NOTIFY *Notify;
} XDP_RX_QUEUE_NOTIFY_DISPATCH;

#define XDP_RX_QUEUE_NOTIFY_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_RX_QUEUE_NOTIFY_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_QUEUE_NOTIFY_DISPATCH, Notify)

typedef struct _XDP_RX_QUEUE_NOTIFY_DETAILS {
    const XDP_RX_QUEUE_NOTIFY_DISPATCH *Dispatch;
} XDP_RX_QUEUE_NOTIFY_DETAILS;

inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDPEXPORT(XdpRxQueueNotify)(
    _In_ XDP_RX_QUEUE_NOTIFY_HANDLE RxQueueNotifyHandle,
    _In_ XDP_RX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ const VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    )
{
    const XDP_RX_QUEUE_NOTIFY_DETAILS *Details = (CONST XDP_RX_QUEUE_NOTIFY_DETAILS *)RxQueueNotifyHandle;
    const XDP_RX_QUEUE_NOTIFY_DISPATCH *Dispatch = Details->Dispatch;

    ASSERT(Dispatch != NULL);
    ASSERT(Dispatch->Header.Revision >= XDP_RX_QUEUE_NOTIFY_DISPATCH_REVISION_1);
    ASSERT(Dispatch->Header.Size >= XDP_SIZEOF_RX_QUEUE_NOTIFY_DISPATCH_REVISION_1);

    Dispatch->Notify(RxQueueNotifyHandle, NotifyCode, NotifyBuffer, NotifyBufferSize);
}