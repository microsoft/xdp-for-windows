//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

VOID
XskReceive(
    _In_ XDP_REDIRECT_BATCH *Batch
    );

BOOLEAN
XskReceiveBatchedExclusive(
    _In_ VOID *Target
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskFillTxCompletion(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32
XskFillTx(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry,
    _In_ UINT32 FrameQuota
    );

NTSTATUS
XskReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ CONST VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskHandle
    );

NTSTATUS
XskValidateDatapathHandle(
    _In_ HANDLE XskHandle
    );

BOOLEAN
XskCanBypass(
    _In_ HANDLE XskHandle,
    _In_ XDP_RX_QUEUE *RxQueue
    );

VOID
XskDereferenceDatapathHandle(
    _In_ HANDLE XskHandle
    );

XDP_FILE_CREATE_ROUTINE XskIrpCreateSocket;

BOOLEAN
XskFastIo(
    _In_ XDP_FILE_OBJECT_HEADER *FileObjectHeader,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ VOID *OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ IO_STATUS_BLOCK *IoStatus
    );

NTSTATUS
XskStart(
    VOID
    );

VOID
XskStop(
    VOID
    );
