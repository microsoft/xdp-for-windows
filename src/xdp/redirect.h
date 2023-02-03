//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_RX_QUEUE XDP_RX_QUEUE;

typedef struct _XDP_REDIRECT_FRAME {
    UINT32 FrameIndex;
    UINT32 FragmentIndex;
} XDP_REDIRECT_FRAME;

typedef struct _XDP_REDIRECT_BATCH {
    VOID *Target;
    XDP_RX_QUEUE *RxQueue;
    XDP_REDIRECT_TARGET_TYPE TargetType;
    UINT32 Count;
    XDP_REDIRECT_FRAME FrameIndexes[32];
} XDP_REDIRECT_BATCH;

typedef struct _XDP_REDIRECT_CONTEXT {
    XDP_REDIRECT_BATCH RedirectBatches[1];
} XDP_REDIRECT_CONTEXT;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpRedirect(
    _In_ XDP_REDIRECT_CONTEXT *Redirect,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_REDIRECT_TARGET_TYPE TargetType,
    _In_ VOID *Target
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushRedirect(
    _In_ XDP_REDIRECT_CONTEXT *Redirect
    );
