//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_MAP XDP_MAP;

inline
NTSTATUS
XdpMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ XDP_MAP **Map
    )
{
    UNREFERENCED_PARAMETER(RequestorMode);
    UNREFERENCED_PARAMETER(HandleBounced);

    *Map = *(XDP_MAP **)HandleBuffer;

    return STATUS_SUCCESS;
}

inline
VOID
XdpMapDereferenceDatapathHandle(
    _In_ XDP_MAP *Map
    )
{
    DBG_UNREFERENCED_PARAMETER(Map);
}

inline
XDP_MAP_TYPE
XdpMapGetType(
    _In_ XDP_MAP *Map
    )
{
    UNREFERENCED_PARAMETER(Map);

    return XDP_MAP_TYPE_XSKMAP;
}

inline
VOID *
XdpXskMapLookup(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key
    )
{
    UNREFERENCED_PARAMETER(Map);
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
