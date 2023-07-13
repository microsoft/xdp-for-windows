//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

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
    UNREFERENCED_PARAMETER(Redirect);
    UNREFERENCED_PARAMETER(FrameIndex);
    UNREFERENCED_PARAMETER(FragmentIndex);
    UNREFERENCED_PARAMETER(TargetType);
    UNREFERENCED_PARAMETER(Target);
}
