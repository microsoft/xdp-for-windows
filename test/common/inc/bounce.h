//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _BOUNCE_BUFFER {
    VOID *Buffer;
} BOUNCE_BUFFER;

VOID
BounceInitialize(
    _Out_ BOUNCE_BUFFER *Bounce
    );

VOID
BounceCleanup(
    _Inout_ BOUNCE_BUFFER *Bounce
    );

VOID
BounceFree(
    _In_opt_ const VOID *Buffer
    );

VOID *
BounceRelease(
    _Inout_ BOUNCE_BUFFER *Bounce
    );

NTSTATUS
BounceBuffer(
    _Inout_ BOUNCE_BUFFER *Bounce,
    _In_ const VOID *Buffer,
    _In_ SIZE_T BufferSize,
    _In_ UINT32 Alignment
    );
