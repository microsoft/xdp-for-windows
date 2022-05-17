//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

MINIPORT_SEND_NET_BUFFER_LISTS MpSendNetBufferLists;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpTransmit(
    _In_ ADAPTER_TX_QUEUE *Tq,
    _Inout_ NDIS_POLL_TRANSMIT_DATA *Poll,
    _Inout_ XDP_POLL_TRANSMIT_DATA *XdpPoll
    );

NDIS_STATUS
MpInitializeTransmitQueue(
    _Inout_ ADAPTER_TX_QUEUE *Tq,
    _In_ CONST ADAPTER_CONTEXT *Adapter
    );

VOID
MpCleanupTransmitQueue(
    _Inout_ ADAPTER_TX_QUEUE *Tq
    );

XDP_CREATE_TX_QUEUE     MpXdpCreateTxQueue;
XDP_ACTIVATE_TX_QUEUE   MpXdpActivateTxQueue;
XDP_DELETE_TX_QUEUE     MpXdpDeleteTxQueue;
