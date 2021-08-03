//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdp/details/rxqueueconfig.h>

typedef
CONST XDP_HOOK_ID *
XDP_RX_QUEUE_CREATE_GET_HOOK_ID(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef struct _XDP_RX_QUEUE_CONFIG_RESERVED {
    UINT32 Size;
    XDP_RX_QUEUE_CREATE_GET_HOOK_ID         *GetHookId;
} XDP_RX_QUEUE_CONFIG_RESERVED;

inline
CONST XDP_HOOK_ID *
XdpRxQueueGetHookId(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    CONST XDP_RX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        !RTL_CONTAINS_FIELD(Reserved, Reserved->Size, GetHookId) ||
        Reserved->GetHookId == NULL) {
        return NULL;
    }

    return Reserved->GetHookId(RxQueueConfig);
}
