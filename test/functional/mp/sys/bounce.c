//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

VOID
BounceInitialize(
    _Out_ BOUNCE_BUFFER *Bounce
    )
{
    RtlZeroMemory(Bounce, sizeof(*Bounce));
}

VOID
BounceCleanup(
    _Inout_ BOUNCE_BUFFER *Bounce
    )
{
    BounceFree(Bounce->Buffer);
    BounceInitialize(Bounce);
}

VOID
BounceFree(
    _In_ CONST VOID *Buffer
    )
{
    if (Buffer != NULL) {
        ExFreePoolWithTag((VOID *)Buffer, POOLTAG_BOUNCE);
    }
}

VOID *
BounceRelease(
    _Inout_ BOUNCE_BUFFER *Bounce
    )
{
    VOID *Buffer = Bounce->Buffer;
    Bounce->Buffer = NULL;
    return Buffer;
}

NTSTATUS
BounceBuffer(
    _Inout_ BOUNCE_BUFFER *Bounce,
    _In_ CONST VOID *Buffer,
    _In_ SIZE_T BufferSize,
    _In_ UINT32 Alignment
    )
{
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Bounce->Buffer == NULL);
    ASSERT(Alignment <= MEMORY_ALLOCATION_ALIGNMENT);

    if (BufferSize == 0) {
        //
        // Nothing to do.
        //
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Bounce->Buffer = ExAllocatePoolZero(NonPagedPoolNx, BufferSize, POOLTAG_BOUNCE);
    if (Bounce->Buffer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    try {
        ProbeForRead((VOID *)Buffer, BufferSize, Alignment);
        RtlCopyMemory(Bounce->Buffer, Buffer, BufferSize);
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        BounceCleanup(Bounce);
    }

    return Status;
}