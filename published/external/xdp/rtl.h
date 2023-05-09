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

#ifdef __cplusplus
} // extern "C"
#endif

#endif
