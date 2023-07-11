//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "ec.h"

typedef struct _XDP_LWF_GENERIC XDP_LWF_GENERIC;

typedef struct _XDP_LWF_GENERIC_TX_STATS {
    UINT64 BatchesPosted;
} XDP_LWF_GENERIC_TX_STATS;

typedef struct _XDP_LWF_GENERIC_TX_QUEUE {
    ULONG QueueId;
    LIST_ENTRY Link;
    XDP_LWF_GENERIC *Generic;
    XDP_TX_QUEUE_NOTIFY_HANDLE XdpNotifyHandle;

    NDIS_HANDLE NdisFilterHandle;
    XDP_TX_QUEUE_HANDLE XdpTxQueue;
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    XDP_EXTENSION BufferMdlExtension;
    XDP_EXTENSION FrameTxCompletionContextExtension;
    XDP_EXTENSION TxCompletionContextExtension;

    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue;
    XDP_EC Ec;

    XDP_PCW_LWF_TX_QUEUE PcwStats;

    struct {
        BOOLEAN Pause : 1;
        BOOLEAN RxInject : 1;
        BOOLEAN TxCompletionContextEnabled : 1;
    } Flags;

    KEVENT *PauseComplete;

    BOOLEAN NeedFlush;
    ULONG FrameCount;
    ULONG OutstandingCount;
    XDP_LWF_GENERIC_TX_STATS Stats;
    SLIST_HEADER NblComplete;
    NET_BUFFER_LIST *FreeNbls;
    NDIS_HANDLE NblPool;
    PCW_INSTANCE *PcwInstance;
    XDP_LIFETIME_ENTRY DeleteEntry;
    KEVENT *DeleteComplete;
} XDP_LWF_GENERIC_TX_QUEUE;

FILTER_SEND_NET_BUFFER_LISTS XdpGenericSendNetBufferLists;
FILTER_SEND_NET_BUFFER_LISTS_COMPLETE XdpGenericSendNetBufferListsComplete;

VOID
XdpGenericSendInjectComplete(
    _In_ VOID *ClassificationResult,
    _In_ NBL_COUNTED_QUEUE *Queue
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpGenericTxFlushRss(
    _In_ XDP_LWF_GENERIC_RSS_QUEUE *Queue,
    _In_ ULONG CurrentProcessor
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxPause(
    _In_ XDP_LWF_GENERIC *Generic
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 NewMtu
    );

XDP_CREATE_TX_QUEUE XdpGenericTxCreateQueue;
XDP_ACTIVATE_TX_QUEUE XdpGenericTxActivateQueue;
XDP_DELETE_TX_QUEUE XdpGenericTxDeleteQueue;
