//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#define RTL_IS_CLEAR_OR_SINGLE_FLAG(Flags, Mask) \
    (((Flags) & (Mask)) == 0 || !(((Flags) & (Mask)) & (((Flags) & (Mask)) - 1)))

#define RTL_PTR_ADD(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))

#define RTL_PTR_SUBTRACT(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) - (ULONG_PTR)(Value)))

#define RTL_MILLISEC_TO_100NANOSEC(m) ((m) * 10000ui64)
#define RTL_SEC_TO_100NANOSEC(s) ((s) * 10000000ui64)

#define ReadUInt64NoFence ReadULong64NoFence

NTSTATUS
RtlUInt32RoundUpToPowerOfTwo(
    _In_ UINT32 Value,
    _Out_ UINT32 *Result
    );
