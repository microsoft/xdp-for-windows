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

typedef struct _XDP_RX_QUEUE_CONFIG_RESERVED {
    XDP_OBJECT_HEADER               Header;
    XDP_RX_QUEUE_CREATE_GET_HOOK_ID *GetHookId;
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
    CONST XDP_RX_QUEUE_CONFIG_RESERVED *Reserved = Details->Dispatch->Reserved;

    if (Reserved == NULL ||
        Reserved->Header.Revision < XDP_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->Header.Size < XDP_SIZEOF_RX_QUEUE_CONFIG_RESERVED_REVISION_1 ||
        Reserved->GetHookId == NULL) {
        return NULL;
    }

    return Reserved->GetHookId(RxQueueConfig);
}
