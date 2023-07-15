//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

inline
NTSTATUS
XskReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ CONST VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskHandle
    )
{
    UNREFERENCED_PARAMETER(RequestorMode);
    UNREFERENCED_PARAMETER(HandleBounced);

    *XskHandle = *(HANDLE *)HandleBuffer;

    return STATUS_SUCCESS;
}

inline
VOID
XskDereferenceDatapathHandle(
    _In_ HANDLE XskHandle
    )
{
    DBG_UNREFERENCED_PARAMETER(XskHandle);

    ASSERT(XskHandle != NULL);
}
