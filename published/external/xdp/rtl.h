//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_RTL_H
#define XDP_RTL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RTL_PTR_ADD
#define RTL_PTR_ADD(Pointer, Value) \
    ((VOID *)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))
#endif

#ifndef RTL_PTR_SUBTRACT
#define RTL_PTR_SUBTRACT(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) - (ULONG_PTR)(Value)))
#endif

#define RTL_PTR_DIFF(A, B) \
   ((ULONG_PTR)(A) - (ULONG_PTR)(B))

#if (!defined(NTDDI_WIN10_CO) || (WDK_NTDDI_VERSION < NTDDI_WIN10_CO)) && \
    !defined(UINT32_VOLATILE_ACCESSORS)
#define UINT32_VOLATILE_ACCESSORS

FORCEINLINE
UINT32
ReadUInt32Acquire(
    _In_ _Interlocked_operand_ UINT32 const volatile *Source
    )
{
    return (UINT32)ReadULongAcquire((PULONG)Source);
}

FORCEINLINE
UINT32
ReadUInt32NoFence(
    _In_ _Interlocked_operand_ UINT32 const volatile *Source
    )
{
    return (UINT32)ReadULongNoFence((PULONG)Source);
}

FORCEINLINE
VOID
WriteUInt32Release(
    _Out_ _Interlocked_operand_ UINT32 volatile *Destination,
    _In_ UINT32 Value
    )
{
    WriteULongRelease((PULONG)Destination, (ULONG)Value);
}

FORCEINLINE
VOID
WriteUInt32NoFence(
    _Out_ _Interlocked_operand_ UINT32 volatile *Destination,
    _In_ UINT32 Value
    )
{
    WriteULongNoFence((PULONG)Destination, (ULONG)Value);
}

#endif

#ifdef _M_ARM64
//
// A pair of acquire and release operations is sufficient on ARM64
// because they cannot be reordered relative to each other.
//
#define XdpBarrierBetweenReleaseAndAcquire()
#else
#ifdef _KERNEL_MODE
#define XdpBarrierBetweenReleaseAndAcquire() KeMemoryBarrier()
#else
#define XdpBarrierBetweenReleaseAndAcquire() MemoryBarrier()
#endif // _KERNEL_MODE
#endif // _M_ARM64_

#ifdef __cplusplus
} // extern "C"
#endif

#endif
