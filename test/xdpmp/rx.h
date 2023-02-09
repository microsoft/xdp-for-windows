//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

MINIPORT_RETURN_NET_BUFFER_LISTS MpReturnNetBufferLists;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpReceive(
    _In_ ADAPTER_RX_QUEUE *Rq,
    _Inout_ NDIS_POLL_RECEIVE_DATA *Poll,
    _Inout_ XDP_POLL_RECEIVE_DATA *XdpPoll
    );

NDIS_STATUS
MpInitializeReceiveQueue(
    _Inout_ ADAPTER_RX_QUEUE *Rq,
    _In_ CONST ADAPTER_CONTEXT *Adapter
    );

VOID
MpCleanupReceiveQueue(
    _Inout_ ADAPTER_RX_QUEUE *Rq
    );

XDP_CREATE_RX_QUEUE     MpXdpCreateRxQueue;
XDP_ACTIVATE_RX_QUEUE   MpXdpActivateRxQueue;
XDP_DELETE_RX_QUEUE     MpXdpDeleteRxQueue;
