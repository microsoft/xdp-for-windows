//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpXdpNotify(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    );

VOID
MpDepopulateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

NDIS_STATUS
MpPopulateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

NDIS_STATUS
MpStartRss(
    _In_ ADAPTER_CONTEXT *Adapter,
    _Inout_ ADAPTER_QUEUE *RssQueue
    );

VOID
MpSetRss(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *RssParams,
    _In_ SIZE_T RssParamsLength
    );
