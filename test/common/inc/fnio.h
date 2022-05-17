//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "fniotypes.h"

typedef struct _DATA_FILTER DATA_FILTER;

//
// APIs for filtering, getting, and flushing IO.
//

_IRQL_requires_max_(DISPATCH_LEVEL)
DATA_FILTER *
FnIoCreateFilter(
    _In_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoDeleteFilter(
    _In_ DATA_FILTER *Filter
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FnIoFilterNbl(
    _In_ DATA_FILTER *Filter,
    _In_ NET_BUFFER_LIST *Nbl
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoGetFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoDequeueFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
SIZE_T
FnIoFlushDequeuedFrames(
    _In_ DATA_FILTER *Filter,
    _Out_ NET_BUFFER_LIST **NblChain
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
SIZE_T
FnIoFlushAllFrames(
    _In_ DATA_FILTER *Filter,
    _Out_ NET_BUFFER_LIST **NblChain
    );

//
// APIs for queueing up new IO.
//

typedef struct DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) _ENQUEUE_NBL_CONTEXT {
    UINT32 TrailingMdlBytes;
} ENQUEUE_NBL_CONTEXT;

#define FNIO_ENQUEUE_NBL_CONTEXT_SIZE sizeof(ENQUEUE_NBL_CONTEXT)

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoEnqueueFrameBegin(
    _In_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength,
    _In_ NDIS_HANDLE NblPool,
    _Out_ DATA_ENQUEUE_IN *EnqueueIn,
    _Out_ NET_BUFFER_LIST **Nbl
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoEnqueueFrameEnd(
    _In_ DATA_ENQUEUE_IN *EnqueueIn
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoEnqueueFrameReturn(
    _In_ NET_BUFFER_LIST *Nbl
    );
