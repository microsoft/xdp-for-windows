//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef enum {
    PagedPool,
    NonPagedPoolNx,
    NonPagedPoolNxCacheAligned,
} POOL_TYPE;

inline
VOID *
ExAllocatePoolZero(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag
    )
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);

    return calloc(1, NumberOfBytes);
}

inline
VOID
ExFreePoolWithTag(
    _In_ VOID *P,
    _In_ ULONG Tag
    )
{
    UNREFERENCED_PARAMETER(Tag);

    free(P);
}

typedef CCHAR KPROCESSOR_MODE;

typedef enum _MODE {
    KernelMode,
    UserMode,
    MaximumMode
} MODE;

#include <intsafe.h>

inline
NTSTATUS
RtlUInt32Add(
    _In_ UINT32 a,
    _In_ UINT32 b,
    _Out_ UINT32 *c
    )
{
    if (SUCCEEDED(UInt32Add(a, b, c))) {
        return STATUS_SUCCESS;
    } else {
        return STATUS_INTEGER_OVERFLOW;
    }
}

inline
NTSTATUS
RtlSizeTAdd(
    _In_ SIZE_T a,
    _In_ SIZE_T b,
    _Out_ SIZE_T *c
    )
{
    if (SUCCEEDED(SizeTAdd(a, b, c))) {
        return STATUS_SUCCESS;
    } else {
        return STATUS_INTEGER_OVERFLOW;
    }
}

inline
NTSTATUS
RtlSizeTMult(
    _In_ SIZE_T a,
    _In_ SIZE_T b,
    _Out_ SIZE_T *c
    )
{
    if (SUCCEEDED(SizeTMult(a, b, c))) {
        return STATUS_SUCCESS;
    } else {
        return STATUS_INTEGER_OVERFLOW;
    }
}
