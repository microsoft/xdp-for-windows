//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

typedef struct _XDP_OID_CLONE {
    NDIS_OID_REQUEST *OriginalRequest;
} XDP_OID_CLONE;

C_ASSERT(sizeof(XDP_OID_CLONE) <= RTL_FIELD_SIZE(NDIS_OID_REQUEST, SourceReserved));

typedef struct _XDP_OID_REQUEST {
    NDIS_OID_REQUEST Oid;
    NDIS_STATUS Status;
    KEVENT CompletionEvent;
} XDP_OID_REQUEST;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
OidInternalRequestComplete(
    _In_ LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ NDIS_STATUS Status
    )
{
    XDP_OID_REQUEST *FilterRequest;

    UNREFERENCED_PARAMETER(Filter);

    FilterRequest = CONTAINING_RECORD(Request, XDP_OID_REQUEST, Oid);
    FilterRequest->Status = Status;
    KeSetEvent(&FilterRequest->CompletionEvent, 0, FALSE);
}

_Use_decl_annotations_
NDIS_STATUS
FilterOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NDIS_STATUS Status;
    NDIS_OID_REQUEST *ClonedRequest = NULL;
    XDP_OID_CLONE *Context;

    Status =
        NdisAllocateCloneOidRequest(
            Filter->NdisFilterHandle, Request, POOLTAG_OID, &ClonedRequest);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Context = (XDP_OID_CLONE *)(&ClonedRequest->SourceReserved[0]);
    Context->OriginalRequest = Request;
    ClonedRequest->RequestId = Request->RequestId;

    Status = NdisFOidRequest(Filter->NdisFilterHandle, ClonedRequest);
    if (Status != NDIS_STATUS_PENDING) {
        FilterOidRequestComplete(Filter, ClonedRequest, Status);
        Status = NDIS_STATUS_PENDING;
    }

Exit:

    return Status;
}

_Use_decl_annotations_
VOID
FilterOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NDIS_OID_REQUEST *OriginalRequest;
    XDP_OID_CLONE *Context;

    Context = (XDP_OID_CLONE *)(&Request->SourceReserved[0]);
    OriginalRequest = Context->OriginalRequest;

    if (OriginalRequest == NULL) {
        OidInternalRequestComplete(Filter, Request, Status);
        return;
    }

    //
    // Copy the information from the returned request to the original request
    //
    switch(Request->RequestType)
    {
    case NdisRequestMethod:
        OriginalRequest->DATA.METHOD_INFORMATION.OutputBufferLength =
            Request->DATA.METHOD_INFORMATION.OutputBufferLength;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesRead =
            Request->DATA.METHOD_INFORMATION.BytesRead;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesNeeded =
            Request->DATA.METHOD_INFORMATION.BytesNeeded;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesWritten =
            Request->DATA.METHOD_INFORMATION.BytesWritten;
        break;

    case NdisRequestSetInformation:
        OriginalRequest->DATA.SET_INFORMATION.BytesRead =
            Request->DATA.SET_INFORMATION.BytesRead;
        OriginalRequest->DATA.SET_INFORMATION.BytesNeeded =
            Request->DATA.SET_INFORMATION.BytesNeeded;
        break;

    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
    default:
        OriginalRequest->DATA.QUERY_INFORMATION.BytesWritten =
            Request->DATA.QUERY_INFORMATION.BytesWritten;
        OriginalRequest->DATA.QUERY_INFORMATION.BytesNeeded =
            Request->DATA.QUERY_INFORMATION.BytesNeeded;
        break;
    }

    NdisFreeCloneOidRequest(Filter->NdisFilterHandle, Request);
    NdisFOidRequestComplete(Filter->NdisFilterHandle, OriginalRequest, Status);
}
