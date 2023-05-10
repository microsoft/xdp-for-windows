//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdprefcount.h>

typedef struct _XDP_RUNDOWN_REF {
    KSPIN_LOCK Lock;
    BOOLEAN Rundown;
    XDP_REFERENCE_COUNT Count;
} XDP_RUNDOWN_REF;

inline
VOID
XdpInitializeRundown(
    _Out_ XDP_RUNDOWN_REF *Rundown
    )
{
    KeInitializeSpinLock(&Rundown->Lock);
    Rundown->Rundown = FALSE;
    XdpInitializeReferenceCount(&Rundown->Count);
}

inline
BOOLEAN
XdpAcquireRundown(
    _Inout_ XDP_RUNDOWN_REF *Rundown
    )
{
    KIRQL OldIrql;
    BOOLEAN Acquired = FALSE;

    KeAcquireSpinLock(&Rundown->Lock, &OldIrql);

    if (!Rundown->Rundown) {
        XdpIncrementReferenceCount(&Rundown->Count);
        Acquired = TRUE;
    }

    KeReleaseSpinLock(&Rundown->Lock, OldIrql);

    return Acquired;
}

inline
BOOLEAN
XdpReleaseRundown(
    _Inout_ XDP_RUNDOWN_REF *Rundown
    )
{
    return XdpDecrementReferenceCount(&Rundown->Count);
}

inline
BOOLEAN
XdpDisableRundown(
    _Inout_ XDP_RUNDOWN_REF *Rundown
    )
{
    KIRQL OldIrql;
    BOOLEAN RundownComplete = FALSE;

    KeAcquireSpinLock(&Rundown->Lock, &OldIrql);

    ASSERT(!Rundown->Rundown);
    Rundown->Rundown = TRUE;
    RundownComplete = XdpDecrementReferenceCount(&Rundown->Count);

    KeReleaseSpinLock(&Rundown->Lock, OldIrql);

    return RundownComplete;
}
