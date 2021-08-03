//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

VOID
IoctlCleanupRxEnqueue(
    _Inout_ RX_ENQUEUE_IN *RxEnqueueIn
    )
{
    if (RxEnqueueIn->Buffers != NULL) {
        for (UINT32 BufferIndex = 0; BufferIndex < RxEnqueueIn->Frame.BufferCount; BufferIndex++) {
            if (RxEnqueueIn->Buffers[BufferIndex].VirtualAddress != NULL) {
                BounceFree(RxEnqueueIn->Buffers[BufferIndex].VirtualAddress);
            }
        }

        BounceFree(RxEnqueueIn->Buffers);
    }
}

NTSTATUS
IoctlBounceRxEnqueue(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ RX_ENQUEUE_IN *RxEnqueueIn
    )
{
    NTSTATUS Status;
    BOUNCE_BUFFER Buffers, Buffer;
    CONST RX_ENQUEUE_IN *IoBuffer = InputBuffer;
    UINT32 BufferCount;
    SIZE_T BufferArraySize;

    RtlZeroMemory(RxEnqueueIn, sizeof(*RxEnqueueIn));
    BounceInitialize(&Buffers);
    BounceInitialize(&Buffer);

    if (InputBufferLength < sizeof(*IoBuffer)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    //
    // The first level of the input structure has already been bounced by the
    // IO manager. Zero unbounced fields from the output structure.
    //
    *RxEnqueueIn = *IoBuffer;
    RxEnqueueIn->Buffers = NULL;
    RxEnqueueIn->Frame.BufferCount = 0;

    //
    // Each frame must have at least one buffer.
    //
    BufferCount = IoBuffer->Frame.BufferCount;
    if (BufferCount == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Copy the user buffer array into a trusted kernel buffer.
    //
    BufferArraySize = sizeof(*IoBuffer->Buffers) * BufferCount;
    Status = BounceBuffer(&Buffers, IoBuffer->Buffers, BufferArraySize, __alignof(RX_BUFFER));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RxEnqueueIn->Buffers = BounceRelease(&Buffers);

    while (RxEnqueueIn->Frame.BufferCount < BufferCount) {
        CONST UINT32 BufferIndex = RxEnqueueIn->Frame.BufferCount;
        CONST RX_BUFFER *RxBuffer = &RxEnqueueIn->Buffers[BufferIndex];
        UINT32 TotalLength;

        if (RxBuffer->BufferLength == 0) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        Status = RtlUInt32Add(RxBuffer->DataOffset, RxBuffer->DataLength, &TotalLength);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        if (TotalLength > RxBuffer->BufferLength) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        //
        // Bounce each user buffer into a trusted kernel buffer.
        //
        Status =
            BounceBuffer(
                &Buffer, RxBuffer->VirtualAddress, RxBuffer->BufferLength, __alignof(UCHAR));
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        RxEnqueueIn->Buffers[BufferIndex] = *RxBuffer;
        RxEnqueueIn->Buffers[BufferIndex].VirtualAddress = BounceRelease(&Buffer);
        RxEnqueueIn->Frame.BufferCount++;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        IoctlCleanupRxEnqueue(RxEnqueueIn);
        RtlZeroMemory(RxEnqueueIn, sizeof(*RxEnqueueIn));
    }

    BounceCleanup(&Buffer);
    BounceCleanup(&Buffers);

    return Status;
}

VOID
IoctlCleanupTxFilter(
    _In_ TX_FILTER_IN *TxFilterIn
    )
{
    if (TxFilterIn->Mask != NULL) {
        BounceFree(TxFilterIn->Mask);
    }

    if (TxFilterIn->Pattern != NULL) {
        BounceFree(TxFilterIn->Pattern);
    }

    RtlZeroMemory(TxFilterIn, sizeof(*TxFilterIn));
}

NTSTATUS
IoctlBounceTxFilter(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ TX_FILTER_IN *TxFilterIn
    )
{
    NTSTATUS Status;
    BOUNCE_BUFFER Pattern, Mask;
    CONST TX_FILTER_IN *IoBuffer = InputBuffer;

    RtlZeroMemory(TxFilterIn, sizeof(*TxFilterIn));
    BounceInitialize(&Pattern);
    BounceInitialize(&Mask);

    if (InputBufferLength < sizeof(*IoBuffer)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (IoBuffer->Length == 0) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    //
    // Copy the user buffer array into a trusted kernel buffer.
    //
    Status = BounceBuffer(&Pattern, IoBuffer->Pattern, IoBuffer->Length, __alignof(UCHAR));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = BounceBuffer(&Mask, IoBuffer->Mask, IoBuffer->Length, __alignof(UCHAR));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    TxFilterIn->Pattern = BounceRelease(&Pattern);
    TxFilterIn->Mask = BounceRelease(&Mask);
    TxFilterIn->Length = IoBuffer->Length;

Exit:

    if (!NT_SUCCESS(Status)) {
        IoctlCleanupTxFilter(TxFilterIn);
        RtlZeroMemory(TxFilterIn, sizeof(*TxFilterIn));
    }

    BounceCleanup(&Mask);
    BounceCleanup(&Pattern);

    return Status;
}