//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

_Use_decl_annotations_
VOID
FilterSendNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG SendFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    NdisFSendNetBufferLists(
        Filter->NdisFilterHandle, NetBufferLists, PortNumber, SendFlags);
}

_Use_decl_annotations_
VOID
FilterSendNetBufferListsComplete(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG SendCompleteFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    NdisFSendNetBufferListsComplete(
        Filter->NdisFilterHandle, NetBufferLists, SendCompleteFlags);
}
