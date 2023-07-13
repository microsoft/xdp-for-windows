//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

EX_RUNDOWN_REF XdpRtlRundown = {
    .Count = EX_RUNDOWN_ACTIVE
};

_IRQL_requires_max_(APC_LEVEL)
_Acquires_exclusive_lock_(Lock)
VOID
RtlAcquirePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(Lock);
}

_IRQL_requires_max_(APC_LEVEL)
_Releases_exclusive_lock_(Lock)
VOID
RtlReleasePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    ExReleasePushLockExclusive(Lock);
    KeLeaveCriticalRegion();
}

_IRQL_requires_max_(APC_LEVEL)
_Acquires_shared_lock_(Lock)
VOID
RtlAcquirePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(Lock);
}

_IRQL_requires_max_(APC_LEVEL)
_Releases_shared_lock_(Lock)
VOID
RtlReleasePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    ExReleasePushLockShared(Lock);
    KeLeaveCriticalRegion();
}

UINT32
RtlRandomNumber(
    VOID
    )
{
    //
    // Generate a pseudo random value between 0 and 2^32 - 1.
    //
    // This routine is a quick and dirty psuedo random number generator.  It has
    // the advantages of being fast and consuming very little memory (for either
    // code or data).  The random numbers it produces are not of the best
    // quality, however.  A much better generator could be had if we were
    // willing to use an extra 256 bytes of memory for data.
    //
    // This routine uses the linear congruential method (see Knuth, Vol II),
    // with specific values for the multiplier and constant taken from Numerical
    // Recipes in C Second Edition by Press, et. al.
    //
    // To reduce contention, partition the random number state by CPU index.
    //
    static LONG RandomPartitions[16];

    UINT32 PartitionIndex = KeGetCurrentProcessorIndex() % RTL_NUMBER_OF(RandomPartitions);
    LONG *RandomState = &RandomPartitions[PartitionIndex];
    LONG NewValue, OldValue;

    if (*RandomState == 0) {
        //
        // (Re)seed. Take the current QPC and add a bit of per-partition entropy.
        //
        LARGE_INTEGER Qpc = KeQueryPerformanceCounter(NULL);
        InterlockedCompareExchange(RandomState, Qpc.LowPart ^ (PartitionIndex << 16), 0);
    }

    //
    // The algorithm is R = (aR + c) mod m, where R is the random number,
    // a is a magic multiplier, c is a constant, and the modulus m is the
    // maximum number of elements in the period.  We chose our m to be 2^32
    // in order to get the mod operation for free.
    //
    do {
        OldValue = *RandomState;
        NewValue = (1664525 * OldValue) + 1013904223;
    } while (InterlockedCompareExchange(RandomState, NewValue, OldValue) != OldValue);

    return (UINT32)NewValue;
}

UINT32
RtlRandomNumberInRange(
    _In_ UINT32 Min,
    _In_ UINT32 Max
    )
{
    UINT32 Number = Max - Min;

    //
    // Return a fast but low-quality random number in the range [Min, Max).
    //
    Number = (UINT32)(((UINT64)RtlRandomNumber() * Number) >> 32);
    Number += Min;

    return Number;
}

NTSTATUS
XdpRtlStart(
    VOID
    )
{
    ExReInitializeRundownProtection(&XdpRtlRundown);

    return STATUS_SUCCESS;
}

VOID
XdpRtlStop(
    VOID
    )
{
    ExWaitForRundownProtectionRelease(&XdpRtlRundown);
}
