//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_LWF_GENERIC XDP_LWF_GENERIC;
typedef struct _XDP_LWF_GENERIC_RX_QUEUE XDP_LWF_GENERIC_RX_QUEUE;
typedef struct _XDP_LWF_GENERIC_TX_QUEUE XDP_LWF_GENERIC_TX_QUEUE;

typedef struct _XDP_LWF_GENERIC_RSS_QUEUE {
    ULONG RssHash;
    ULONG IdealProcessor;
    XDP_LWF_GENERIC_RX_QUEUE *RxQueue;
    XDP_LWF_GENERIC_TX_QUEUE *TxQueue;
    XDP_LWF_GENERIC_RX_QUEUE *TxInspectQueue;
    XDP_LWF_GENERIC_TX_QUEUE *RxInjectQueue;
} XDP_LWF_GENERIC_RSS_QUEUE;

typedef struct _RSS_INDIRECTION_ENTRY {
    UINT32 QueueIndex;
} RSS_INDIRECTION_ENTRY;

typedef struct _XDP_LWF_GENERIC_INDIRECTION_TABLE {
    ULONG IndirectionMask;
    XDP_LIFETIME_ENTRY DeleteEntry;
    RSS_INDIRECTION_ENTRY Entries[0];
} XDP_LWF_GENERIC_INDIRECTION_TABLE;

typedef struct _XDP_LWF_GENERIC_RSS_CLEANUP {
    XDP_LIFETIME_ENTRY DeleteEntry;
    XDP_LWF_GENERIC_RSS_QUEUE *Queues;
} XDP_LWF_GENERIC_RSS_CLEANUP;

typedef struct _XDP_LWF_GENERIC_RSS {
    XDP_LWF_GENERIC_RSS_QUEUE *Queues;
    XDP_LWF_GENERIC_INDIRECTION_TABLE *IndirectionTable;
    ULONG QueueCount;
    XDP_LWF_GENERIC_RSS_CLEANUP *QueueCleanup;
} XDP_LWF_GENERIC_RSS;

XDP_LWF_GENERIC_RSS_QUEUE *
XdpGenericRssGetQueueById(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 QueueId
    );

_IRQL_requires_(DISPATCH_LEVEL)
XDP_LWF_GENERIC_RSS_QUEUE *
XdpGenericRssGetQueue(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ ULONG CurrentProcessor,
    _In_ BOOLEAN TxInspect,
    _In_ UINT32 RssHash
    );

NDIS_STATUS
XdpGenericRssInspectOidRequest(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_OID_REQUEST *Request
    );

typedef struct _XDP_LWF_GENERIC_INDIRECTION_STORAGE {
    XDP_LWF_GENERIC_INDIRECTION_TABLE *NewIndirectionTable;
    XDP_LWF_GENERIC_RSS_QUEUE *NewQueues;
    ULONG AssignedQueues;
} XDP_LWF_GENERIC_INDIRECTION_STORAGE;

VOID
XdpGenericRssFreeIndirection(
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    );

NTSTATUS
XdpGenericRssCreateIndirection(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *RssParams,
    _In_ ULONG RssParamsLength,
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    );

VOID
XdpGenericRssApplyIndirection(
    _In_ XDP_LWF_GENERIC *Generic,
    _Inout_ XDP_LWF_GENERIC_INDIRECTION_STORAGE *Indirection
    );

NTSTATUS
XdpGenericRssInitialize(
    _In_ XDP_LWF_GENERIC *Generic
    );

VOID
XdpGenericRssCleanup(
    _In_ XDP_LWF_GENERIC *Generic
    );
