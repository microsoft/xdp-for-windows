//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

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
