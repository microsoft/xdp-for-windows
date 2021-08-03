//
// Copyright (C) Microsoft Corporation. All rights reserved.
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

NTSTATUS
XskReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ CONST VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskHandle
    );

NTSTATUS
XskValidateDatapathHandle(
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
    _Inout_opt_ VOID *OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Inout_ IO_STATUS_BLOCK *IoStatus
    );

NTSTATUS
XskStart(
    VOID
    );

VOID
XskStop(
    VOID
    );
