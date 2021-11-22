//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

_Use_decl_annotations_
VOID
FilterReturnNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG ReturnFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    NdisFReturnNetBufferLists(
        Filter->NdisFilterHandle, NetBufferLists, ReturnFlags);
}

_Use_decl_annotations_
VOID
FilterReceiveNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG NumberOfNetBufferLists,
    ULONG ReceiveFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    NdisFIndicateReceiveNetBufferLists(
        Filter->NdisFilterHandle, NetBufferLists, PortNumber,
        NumberOfNetBufferLists, ReceiveFlags);
}
