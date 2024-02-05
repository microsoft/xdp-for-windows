//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "precomp.h"

VOID
FnIoIoctlCleanupFilter(
    _In_ DATA_FILTER_IN *FilterIn
    );

NTSTATUS
FnIoIoctlBounceFilter(
    _In_ const VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_FILTER_IN *FilterIn
    );

VOID
FnIoIoctlCleanupEnqueue(
    _Inout_ DATA_ENQUEUE_IN *EnqueueIn
    );

NTSTATUS
FnIoIoctlBounceEnqueue(
    _In_ const VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ DATA_ENQUEUE_IN *EnqueueIn
    );
