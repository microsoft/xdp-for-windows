//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

NTSTATUS
NdisPollStart(
    VOID
    );

VOID
NdisPollStop(
    VOID
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
NdisPollInvokePoll(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ ULONG RxQuota,
    _In_ ULONG TxQuota
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NdisPollEnableInterrupt(
    _In_ PNDIS_POLL_QUEUE Q
    );
