//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#include <ndis/types.h>

//
// Structure containing NDIS poll API information for an XDP queue.
//
typedef struct _XDP_POLL_INFO {
    UINT32 Size;

    //
    // A handle to the interface's NDIS polling context for a queue.
    //
    NDIS_HANDLE PollHandle;

    //
    // Indicates whether the polling context is shared with any other queues of
    // the same direction. A single RX/TX queue pair is not considered shared.
    //
    BOOLEAN Shared;
} XDP_POLL_INFO;

//
// Initializes an XDP poll information object with a shared NDIS polling handle.
// An NDIS poll handle is considered shared if multiple interface queues use the
// same handle in the same data path direction. A single RX/TX queue pair is not
// considered shared.
//
inline
VOID
XdpInitializeSharedPollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
{
    RtlZeroMemory(PollInfo, sizeof(*PollInfo));
    PollInfo->Size = sizeof(*PollInfo);

    PollInfo->PollHandle = PollHandle;
    PollInfo->Shared = TRUE;
}

//
// Initializes an XDP poll information object with an exclusive NDIS polling
// handle. A single RX/TX queue pair using one NDIS polling handle is considered
// exclusive.
//
inline
VOID
XdpInitializeExclusivePollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
{
    RtlZeroMemory(PollInfo, sizeof(*PollInfo));
    PollInfo->Size = sizeof(*PollInfo);

    PollInfo->PollHandle = PollHandle;
    PollInfo->Shared = FALSE;
}

EXTERN_C_END
