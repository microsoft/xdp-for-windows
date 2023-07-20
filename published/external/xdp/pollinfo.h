//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <ndis/types.h>

typedef struct _XDP_POLL_INFO {
    XDP_OBJECT_HEADER Header;
    NDIS_HANDLE PollHandle;
    BOOLEAN Shared;
} XDP_POLL_INFO;

#define XDP_POLL_INFO_REVISION_1 1

#define XDP_SIZEOF_POLL_INFO_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_POLL_INFO, Shared)

inline
VOID
XdpInitializeSharedPollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
{
    RtlZeroMemory(PollInfo, sizeof(*PollInfo));
    PollInfo->Header.Revision = XDP_POLL_INFO_REVISION_1;
    PollInfo->Header.Size = XDP_SIZEOF_POLL_INFO_REVISION_1;

    PollInfo->PollHandle = PollHandle;
    PollInfo->Shared = TRUE;
}

inline
VOID
XdpInitializeExclusivePollInfo(
    _Out_ XDP_POLL_INFO *PollInfo,
    _In_ NDIS_HANDLE PollHandle
    )
{
    RtlZeroMemory(PollInfo, sizeof(*PollInfo));
    PollInfo->Header.Revision = XDP_POLL_INFO_REVISION_1;
    PollInfo->Header.Size = XDP_SIZEOF_POLL_INFO_REVISION_1;

    PollInfo->PollHandle = PollHandle;
    PollInfo->Shared = FALSE;
}

EXTERN_C_END
