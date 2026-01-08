//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdpassert.h>

typedef INT64 XDP_REFERENCE_COUNT;

#if defined(_AMD64_)

#define RtlAcquireFenceAfterReleaseOp(Ptr)

#elif defined(_ARM64_)

//
// The LDAR instruction has Armv8 Acquire semantics, which prevents
// it from being reordered before a prior operation with Release
// semantics. In addition, it prevents all RW operations after it
// from being reordered before it, so it can be used here as an
// acquire fence if a prior operation has Release semantics.
//

#define RtlAcquireFenceAfterReleaseOp(Ptr)            \
    static_assert(sizeof(*(Ptr)) == 8, "== 8 bytes"); \
    __ldar64((volatile unsigned __int64 *)(Ptr))

#else

#error Unsupported architecture.

#endif

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
XdpInitializeReferenceCountEx(
    _Out_ XDP_REFERENCE_COUNT *RefCount,
    _In_ SSIZE_T Bias
    )
{
    FRE_ASSERT(Bias > 0);
    *RefCount = Bias;
}

inline
VOID
XdpIncrementReferenceCount(
    _Inout_ XDP_REFERENCE_COUNT *RefCount
    )
{
    FRE_ASSERT(InterlockedIncrementNoFence64(RefCount) > 1);
}

inline
BOOLEAN
XdpDecrementReferenceCount(
    _Inout_ XDP_REFERENCE_COUNT *RefCount
    )
{
    INT64 NewValue = InterlockedDecrementRelease64(RefCount);

    if (NewValue > 0) {
        return FALSE;
    } else if (NewValue == 0) {
        //
        // An acquire fence is required before object destruction to ensure
        // that the destructor cannot observe values changing on other threads.
        //
        RtlAcquireFenceAfterReleaseOp(RefCount);
        return TRUE;
    } else {
        __fastfail(FAST_FAIL_INVALID_REFERENCE_COUNT);
        return FALSE;
    }
}

#undef RtlAcquireFenceAfterReleaseOp
