//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

VOID
FnIoIoctlCleanupEnqueue(
    _Inout_ DATA_ENQUEUE_IN *EnqueueIn
    )
{
    if (EnqueueIn->Frame.Buffers != NULL) {
        for (UINT32 BufferIndex = 0; BufferIndex < EnqueueIn->Frame.BufferCount; BufferIndex++) {
            if (EnqueueIn->Frame.Buffers[BufferIndex].VirtualAddress != NULL) {
                BounceFree(EnqueueIn->Frame.Buffers[BufferIndex].VirtualAddress);
            }
        }

        BounceFree(EnqueueIn->Frame.Buffers);
    }
}

NTSTATUS
FnIoIoctlBounceEnqueue(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_ENQUEUE_IN *EnqueueIn
    )
{
    NTSTATUS Status;
    BOUNCE_BUFFER Buffers, Buffer;
    CONST DATA_ENQUEUE_IN *IoBuffer = InputBuffer;
    UINT32 BufferCount;
    SIZE_T BufferArraySize;

    RtlZeroMemory(EnqueueIn, sizeof(*EnqueueIn));
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
    *EnqueueIn = *IoBuffer;
    EnqueueIn->Frame.Buffers = NULL;
    EnqueueIn->Frame.BufferCount = 0;

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
    BufferArraySize = sizeof(*IoBuffer->Frame.Buffers) * BufferCount;
    Status = BounceBuffer(&Buffers, IoBuffer->Frame.Buffers, BufferArraySize, __alignof(DATA_BUFFER));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    EnqueueIn->Frame.Buffers = BounceRelease(&Buffers);

    while (EnqueueIn->Frame.BufferCount < BufferCount) {
        CONST UINT32 BufferIndex = EnqueueIn->Frame.BufferCount;
        CONST DATA_BUFFER *RxBuffer = &EnqueueIn->Frame.Buffers[BufferIndex];
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

        EnqueueIn->Frame.Buffers[BufferIndex] = *RxBuffer;
        EnqueueIn->Frame.Buffers[BufferIndex].VirtualAddress = BounceRelease(&Buffer);
        EnqueueIn->Frame.BufferCount++;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnIoIoctlCleanupEnqueue(EnqueueIn);
        RtlZeroMemory(EnqueueIn, sizeof(*EnqueueIn));
    }

    BounceCleanup(&Buffer);
    BounceCleanup(&Buffers);

    return Status;
}

VOID
FnIoIoctlCleanupFilter(
    _In_ DATA_FILTER_IN *FilterIn
    )
{
    if (FilterIn->Mask != NULL) {
        BounceFree(FilterIn->Mask);
    }

    if (FilterIn->Pattern != NULL) {
        BounceFree(FilterIn->Pattern);
    }

    RtlZeroMemory(FilterIn, sizeof(*FilterIn));
}

NTSTATUS
FnIoIoctlBounceFilter(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_FILTER_IN *FilterIn
    )
{
    NTSTATUS Status;
    BOUNCE_BUFFER Pattern, Mask;
    CONST DATA_FILTER_IN *IoBuffer = InputBuffer;

    RtlZeroMemory(FilterIn, sizeof(*FilterIn));
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

    FilterIn->Pattern = BounceRelease(&Pattern);
    FilterIn->Mask = BounceRelease(&Mask);
    FilterIn->Length = IoBuffer->Length;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnIoIoctlCleanupFilter(FilterIn);
        RtlZeroMemory(FilterIn, sizeof(*FilterIn));
    }

    BounceCleanup(&Mask);
    BounceCleanup(&Pattern);

    return Status;
}
