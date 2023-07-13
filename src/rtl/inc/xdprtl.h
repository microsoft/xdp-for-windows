//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdp/rtl.h>

#ifndef RTL_IS_POWER_OF_TWO
#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))
#endif

#ifndef RTL_IS_CLEAR_OR_SINGLE_FLAG
#define RTL_IS_CLEAR_OR_SINGLE_FLAG(Flags, Mask) \
    (((Flags) & (Mask)) == 0 || !(((Flags) & (Mask)) & (((Flags) & (Mask)) - 1)))
#endif

#ifndef RTL_NUM_ALIGN_DOWN
#define RTL_NUM_ALIGN_DOWN(Number, Alignment) \
    ((Number) - ((Number) & ((Alignment) - 1)))
#endif

#ifndef RTL_NUM_ALIGN_UP
#define RTL_NUM_ALIGN_UP(Number, Alignment) \
    RTL_NUM_ALIGN_DOWN((Number) + (Alignment) - 1, (Alignment))
#endif

#ifndef RTL_PTR_SUBTRACT
#define RTL_PTR_SUBTRACT(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) - (ULONG_PTR)(Value)))
#endif

#ifndef ALIGN_DOWN_BY
#define ALIGN_DOWN_BY(Length, Alignment) \
    ((ULONG_PTR)(Length)& ~(Alignment - 1))
#endif

#ifndef ALIGN_UP_BY
#define ALIGN_UP_BY(Length, Alignment) \
    (ALIGN_DOWN_BY(((ULONG_PTR)(Length) + Alignment - 1), Alignment))
#endif

#define RTL_MILLISEC_TO_100NANOSEC(m) ((m) * 10000ui64)
#define RTL_SEC_TO_100NANOSEC(s) ((s) * 10000000ui64)
#define RTL_SEC_TO_MILLISEC(s) ((s) * 1000ui64)

#ifndef ReadUInt64NoFence
#define ReadUInt64NoFence ReadULong64NoFence
#endif

#ifndef htons
#define htons _byteswap_ushort
#endif

#ifndef ntohs
#define ntohs _byteswap_ushort
#endif

#ifndef htonl
#define htonl _byteswap_ulong
#endif

#ifndef ntohl
#define ntohl _byteswap_ulong
#endif

#ifdef KERNEL_MODE

_IRQL_requires_max_(APC_LEVEL)
_Acquires_exclusive_lock_(Lock)
VOID
RtlAcquirePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    );

_IRQL_requires_max_(APC_LEVEL)
_Releases_exclusive_lock_(Lock)
VOID
RtlReleasePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    );

_IRQL_requires_max_(APC_LEVEL)
_Acquires_shared_lock_(Lock)
VOID
RtlAcquirePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    );

_IRQL_requires_max_(APC_LEVEL)
_Releases_shared_lock_(Lock)
VOID
RtlReleasePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    );

NTSTATUS
XdpRtlStart(
    VOID
    );

VOID
XdpRtlStop(
    VOID
    );

__forceinline
VOID
RtlCopyVolatileMemory(
    _Out_writes_bytes_(Size) VOID *Destination,
    _In_reads_bytes_(Size) volatile const VOID *Source,
    _In_ SIZE_T Size
    )
{
    RtlCopyMemory(Destination, (const VOID *)Source, Size);
    _ReadWriteBarrier();
}

__forceinline
HANDLE
ReadHandleNoFence(
    _In_reads_bytes_(sizeof(HANDLE)) volatile CONST HANDLE *Address
    )
{
    return (HANDLE)ReadPointerNoFence((PVOID *)Address);
}

#else

inline
INT8
RtlFindMostSignificantBit(
    _In_ UINT64 Value
    )
{
    DWORD BitOffset;

    if (_BitScanReverse64(&BitOffset, Value)) {
        return (INT8)BitOffset;
    } else {
        return -1;
    }
}

inline
INT8
RtlFindLeastSignificantBit(
    _In_ UINT64 Value
    )
{
    DWORD BitOffset;

    if (BitScanForward64(&BitOffset, Value)) {
        return (CCHAR)BitOffset;

    } else {
        return -1;
    }
}

#endif

#if defined(STATUS_SUCCESS) && defined(STATUS_INTEGER_OVERFLOW)

inline
NTSTATUS
RtlUInt32RoundUpToPowerOfTwo(
    _In_ UINT32 Value,
    _Out_ UINT32 *Result
    )
{
    if (Value > (1ui32 << 31)) {
        return STATUS_INTEGER_OVERFLOW;
    }

    if (!RTL_IS_POWER_OF_TWO(Value)) {
        *Result = 1ui32 << (RtlFindMostSignificantBit(Value) + 1);
    } else {
        *Result = Value;
    }

    return STATUS_SUCCESS;
}

#endif

UINT32
RtlRandomNumber(
    VOID
    );

UINT32
RtlRandomNumberInRange(
    _In_ UINT32 Min,
    _In_ UINT32 Max
    );
