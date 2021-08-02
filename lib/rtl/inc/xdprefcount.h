//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdpassert.h>

typedef INT64 XDP_REFERENCE_COUNT;

inline
VOID
XdpInitializeReferenceCount(
    _Out_ XDP_REFERENCE_COUNT *RefCount
    )
{
    *RefCount = 1;
}

inline
VOID
XdpIncrementReferenceCount(
    _Inout_ XDP_REFERENCE_COUNT *RefCount
    )
{
    FRE_ASSERT(InterlockedIncrement64(RefCount) > 1);
}

inline
BOOLEAN
XdpDecrementReferenceCount(
    _Inout_ XDP_REFERENCE_COUNT *RefCount
    )
{
    INT64 NewValue = InterlockedDecrement64(RefCount);
    FRE_ASSERT(NewValue >= 0);

    return NewValue == 0;
}
