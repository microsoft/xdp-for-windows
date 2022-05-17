//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/datapath.h>
#include <xdp/details/export.h>

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_RECEIVE(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_FLUSH_RECEIVE(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    );

typedef struct _XDP_RX_QUEUE_DISPATCH {
    XDP_RECEIVE *Receive;
    XDP_FLUSH_RECEIVE *FlushReceive;
} XDP_RX_QUEUE_DISPATCH;

inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDPEXPORT(XdpReceive)(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    CONST XDP_RX_QUEUE_DISPATCH *Dispatch = (CONST XDP_RX_QUEUE_DISPATCH *)XdpRxQueue;
    Dispatch->Receive(XdpRxQueue);
}

inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDPEXPORT(XdpFlushReceive)(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    CONST XDP_RX_QUEUE_DISPATCH *Dispatch = (CONST XDP_RX_QUEUE_DISPATCH *)XdpRxQueue;
    Dispatch->FlushReceive(XdpRxQueue);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_FLUSH_TRANSMIT(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    );

typedef struct _XDP_TX_QUEUE_DISPATCH {
    XDP_FLUSH_TRANSMIT  *FlushTransmit;
} XDP_TX_QUEUE_DISPATCH;

inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDPEXPORT(XdpFlushTransmit)(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    )
{
    CONST XDP_TX_QUEUE_DISPATCH *Dispatch = (CONST XDP_TX_QUEUE_DISPATCH *)XdpTxQueue;
    Dispatch->FlushTransmit(XdpTxQueue);
}

EXTERN_C_END
