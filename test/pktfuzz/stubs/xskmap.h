//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

inline
NTSTATUS
XdpXskMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskMapHandle
    )
{
    UNREFERENCED_PARAMETER(RequestorMode);
    UNREFERENCED_PARAMETER(HandleBounced);

    *XskMapHandle = *(HANDLE *)HandleBuffer;

    return STATUS_SUCCESS;
}

inline
VOID
XdpXskMapDereferenceDatapathHandle(
    _In_ HANDLE XskMapHandle
    )
{
    DBG_UNREFERENCED_PARAMETER(XskMapHandle);
}

inline
VOID *
XdpXskMapLookup(
    _In_ HANDLE XskMapHandle,
    _In_ UINT32 Key
    )
{
    UNREFERENCED_PARAMETER(XskMapHandle);
    UNREFERENCED_PARAMETER(Key);

    return NULL;
}

inline
UINT32
XdpRxQueueGetQueueIdFromInspectionContext(
    _In_ const XDP_INSPECTION_CONTEXT *Context
    )
{
    UNREFERENCED_PARAMETER(Context);

    return 0;
}
