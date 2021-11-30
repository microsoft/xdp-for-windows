//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "precomp.h"

VOID
FnIoIoctlCleanupFilter(
    _In_ DATA_FILTER_IN *FilterIn
    );

NTSTATUS
FnIoIoctlBounceFilter(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_FILTER_IN *FilterIn
    );

VOID
FnIoIoctlCleanupEnqueue(
    _Inout_ DATA_ENQUEUE_IN *EnqueueIn
    );

NTSTATUS
FnIoIoctlBounceEnqueue(
    _In_ CONST VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_ENQUEUE_IN *EnqueueIn
    );
