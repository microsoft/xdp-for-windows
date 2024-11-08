//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This file contains tentative definitions of NDIS polling API extensions for
// XDP as well as a compatibility shim for older NDIS polling API versions.
//

EXTERN_C_START

#if NDIS_SUPPORT_NDIS685

#include <ndis/poll.h>

typedef struct _XDP_POLL_TRANSMIT_DATA {
    //
    // The number of TX frames completed to the XDP platform.
    //
    UINT32 FramesCompleted;

    //
    // The number of TX frames injected from the XDP platform.
    //
    UINT32 FramesTransmitted;
} XDP_POLL_TRANSMIT_DATA;

typedef struct _XDP_POLL_RECEIVE_DATA {
    //
    // The number of RX frames absorbed by the XDP platform.
    //
    UINT32 FramesAbsorbed;
} XDP_POLL_RECEIVE_DATA;

typedef struct _XDP_POLL_DATA {
    XDP_POLL_TRANSMIT_DATA Transmit;
    XDP_POLL_RECEIVE_DATA Receive;
} XDP_POLL_DATA;

typedef
_IRQL_requires_max_(HIGH_LEVEL)
VOID
XDP_NDIS_REQUEST_POLL(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _Reserved_ VOID *Reserved
    );

//
// This routine provides compatibility with NDIS polling APIs that lack support
// for XDP polling extensions. XDP interface drivers must invoke this helper (or
// a similar custom routine) prior to returning from their NDIS poll callback.
//
inline
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpCompleteNdisPoll(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _In_ NDIS_POLL_DATA *Poll,
    _In_ XDP_POLL_DATA *XdpPoll,
    _In_ XDP_NDIS_REQUEST_POLL *RequestPoll
    )
{
    if (Poll->Receive.IndicatedNblChain != NULL || Poll->Transmit.CompletedNblChain != NULL) {
        //
        // If NBL chains are returned to NDIS, a poll is implicitly requested.
        //
        return;
    }

    if (XdpPoll->Transmit.FramesCompleted > 0 || XdpPoll->Transmit.FramesTransmitted > 0 ||
        XdpPoll->Receive.FramesAbsorbed > 0) {
        //
        // XDP made forward progress, and this was not observable to NDIS.
        // Explicitly request another poll.
        //
        RequestPoll(PollHandle, NULL);
    }
}

#endif // NDIS_SUPPORT_NDIS685

EXTERN_C_END
