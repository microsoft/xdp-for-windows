//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushRedirectBatch(
    _In_ XDP_REDIRECT_BATCH *Batch
    )
{
    switch (Batch->TargetType) {

    case XDP_REDIRECT_TARGET_TYPE_XSK:
        XskReceive(Batch);
        break;

    default:
        ASSERT(FALSE);
    }

    Batch->Count = 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushRedirect(
    _In_ XDP_REDIRECT_CONTEXT *Redirect
    )
{
    for (ULONG Index = 0; Index < RTL_NUMBER_OF(Redirect->RedirectBatches); Index++) {
        XDP_REDIRECT_BATCH *Batch = &Redirect->RedirectBatches[Index];

        if (Batch->Count > 0) {
            XdpFlushRedirectBatch(Batch);
        }
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpRedirect(
    _In_ XDP_REDIRECT_CONTEXT *Redirect,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_REDIRECT_TARGET_TYPE TargetType,
    _In_ VOID *Target
    )
{
    XDP_REDIRECT_BATCH *Batch = &Redirect->RedirectBatches[0];

    if (Batch->Count > 0 &&
        (Batch->TargetType != TargetType ||
         Batch->Target != Target ||
         Batch->Count == RTL_NUMBER_OF(Batch->FrameIndexes))) {
        //
        // Flush the batch.
        //
        XdpFlushRedirectBatch(Batch);
    }

    if (Batch->Count == 0) {
        //
        // Start a redirect batch.
        //
        Batch->TargetType = TargetType;
        Batch->Target = Target;
        Batch->RxQueue = XdpRxQueueFromRedirectContext(Redirect);
    }

    //
    // Pend the frame for internal consumption.
    //
    ASSERT(Batch->Count < RTL_NUMBER_OF(Batch->FrameIndexes));
    Batch->FrameIndexes[Batch->Count].FrameIndex = FrameIndex;
    Batch->FrameIndexes[Batch->Count].FragmentIndex = FragmentIndex;
    Batch->Count++;
}
