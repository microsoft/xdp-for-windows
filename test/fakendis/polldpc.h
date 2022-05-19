//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "poll.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuInsert(
    _In_ PNDIS_POLL_QUEUE Q
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuUpdate(
    _In_ PNDIS_POLL_QUEUE Q
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollCpuCleanup(
    _In_ PNDIS_POLL_QUEUE Q
    );

NTSTATUS
NdisPollCpuStart(
    VOID
    );

VOID
NdisPollCpuStop(
    VOID
    );
