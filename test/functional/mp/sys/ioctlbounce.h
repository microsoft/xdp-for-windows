//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

VOID
IoctlCleanupRxEnqueue(
    _Inout_ RX_ENQUEUE_IN *RxEnqueueIn
    );

NTSTATUS
IoctlBounceRxEnqueue(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ RX_ENQUEUE_IN *RxEnqueueIn
    );

VOID
IoctlCleanupTxFilter(
    _In_ TX_FILTER_IN *TxFilterIn
    );

NTSTATUS
IoctlBounceTxFilter(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ TX_FILTER_IN *TxFilterIn
    );
