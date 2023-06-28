//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "oid.tmh"

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
NDIS_STATUS
XdpLwfInvokeFOidRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_OID_REQUEST *NdisRequest
    )
{
    NDIS_STATUS Status;

    switch (RequestInterface) {
    case XDP_OID_REQUEST_INTERFACE_REGULAR:
        Status = NdisFOidRequest(NdisFilterHandle, NdisRequest);
        break;

    case XDP_OID_REQUEST_INTERFACE_DIRECT:
        Status = NdisFDirectOidRequest(NdisFilterHandle, NdisRequest);
        break;

    default:
        ASSERT(FALSE);
        Status = NDIS_STATUS_INVALID_PARAMETER;
        break;
    }

    return Status;
}

static
VOID
XdpLwfInvokeFOidRequestComplete(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_OID_REQUEST *NdisRequest,
    _In_ NDIS_STATUS Status
    )
{
    switch (RequestInterface) {
    case XDP_OID_REQUEST_INTERFACE_REGULAR:
        NdisFOidRequestComplete(NdisFilterHandle, NdisRequest, Status);
        break;

    case XDP_OID_REQUEST_INTERFACE_DIRECT:
        NdisFDirectOidRequestComplete(NdisFilterHandle, NdisRequest, Status);
        break;

    default:
        FRE_ASSERT(FALSE);
        break;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpLwfOidInternalRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_updates_bytes_to_(InformationBufferLength, *pBytesProcessed)
        VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _In_opt_ ULONG OutputBufferLength,
    _In_ ULONG MethodId,
    _Out_ ULONG *pBytesProcessed
    )
{
    XDP_OID_REQUEST FilterRequest;
    NDIS_OID_REQUEST *NdisRequest = &FilterRequest.Oid;
    NDIS_STATUS Status;

    *pBytesProcessed = 0;
    RtlZeroMemory(NdisRequest, sizeof(NDIS_OID_REQUEST));

    KeInitializeEvent(&FilterRequest.CompletionEvent, NotificationEvent, FALSE);

    NdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    NdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_2;
    NdisRequest->Header.Size = sizeof(NDIS_OID_REQUEST);
    NdisRequest->RequestType = RequestType;

    switch (RequestType) {
    case NdisRequestQueryInformation:
        NdisRequest->DATA.QUERY_INFORMATION.Oid = Oid;
        NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength =
            InformationBufferLength;
        break;

    case NdisRequestSetInformation:
        NdisRequest->DATA.SET_INFORMATION.Oid = Oid;
        NdisRequest->DATA.SET_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.SET_INFORMATION.InformationBufferLength =
            InformationBufferLength;
        break;

    case NdisRequestMethod:
        NdisRequest->DATA.METHOD_INFORMATION.Oid = Oid;
        NdisRequest->DATA.METHOD_INFORMATION.MethodId = MethodId;
        NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength =
            InformationBufferLength;
        NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength =
            OutputBufferLength;
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    NdisRequest->RequestId = (VOID *)XdpLwfOidInternalRequest;

    Status = XdpLwfInvokeFOidRequest(NdisFilterHandle, RequestInterface, NdisRequest);

    if (Status == NDIS_STATUS_PENDING) {
        KeWaitForSingleObject(
            &FilterRequest.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = FilterRequest.Status;
    }

    if (Status == NDIS_STATUS_INVALID_LENGTH) {
        // Map NDIS status to STATUS_BUFFER_TOO_SMALL.
        Status = NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    if (Status == NDIS_STATUS_SUCCESS) {
        if (RequestType == NdisRequestSetInformation) {
            *pBytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesRead;
        }

        if (RequestType == NdisRequestQueryInformation) {
            *pBytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
        }

        if (RequestType == NdisRequestMethod) {
            *pBytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesWritten;
        }
    } else if (Status == NDIS_STATUS_BUFFER_TOO_SHORT) {
        if (RequestType == NdisRequestSetInformation) {
            *pBytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesNeeded;
        }

        if (RequestType == NdisRequestQueryInformation) {
            *pBytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
        }

        if (RequestType == NdisRequestMethod) {
            *pBytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesNeeded;
        }
    }

    return XdpConvertNdisStatusToNtStatus(Status);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpLwfOidInternalRequestComplete(
    _In_ XDP_LWF_FILTER *Filter,
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

static
_Function_class_(IO_WORKITEM_ROUTINE_EX)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
XdpLwfOidRequestWorker(
    _In_ VOID *IoObject,
    _In_opt_ VOID *Context,
    _In_ IO_WORKITEM *IoWorkItem
    )
{
    XDP_LWF_FILTER *Filter = Context;
    NDIS_OID_REQUEST *Request;

    TraceEnter(TRACE_LWF, "Filter=%p IoObject=%p", Filter, IoObject);

    UNREFERENCED_PARAMETER(IoWorkItem);
    ASSERT(Context != NULL);

    Request = InterlockedExchangePointer(&Filter->OidWorkerRequest, NULL);
    FRE_ASSERT(Request != NULL);
    FRE_ASSERT(XdpLwfOidRequest((NDIS_HANDLE)Filter, Request) == NDIS_STATUS_PENDING);

    TraceExitSuccess(TRACE_LWF);
}

VOID
XdpLwfCommonOidRequestInspectComplete(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _Inout_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_ACTION Action,
    _In_ NDIS_STATUS Status
    )
{
    NDIS_OID_REQUEST *ClonedRequest = NULL;
    XDP_OID_CLONE *Context;

    TraceEnter(
        TRACE_LWF,
        "Filter=%p Request=%p Oid=%x RequestInterface=%!OID_REQUEST_INTERFACE! "
        "Action=%!OID_ACTION! Status=%!STATUS!",
        Filter, Request, Request->DATA.Oid, RequestInterface, Action, Status);

    switch (Action) {
    case XdpOidActionComplete:
        ASSERT(Status != NDIS_STATUS_PENDING);
        break;
    case XdpOidActionPass:
        Status =
            NdisAllocateCloneOidRequest(
                Filter->NdisFilterHandle, Request, POOLTAG_OID, &ClonedRequest);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        Context = (XDP_OID_CLONE *)(&ClonedRequest->SourceReserved[0]);
        Context->OriginalRequest = Request;
        ClonedRequest->RequestId = Request->RequestId;
        Status =
            XdpLwfInvokeFOidRequest(Filter->NdisFilterHandle, RequestInterface, ClonedRequest);
        break;
    default:
        ASSERT(FALSE);
        Status = NDIS_STATUS_FAILURE;
        break;
    }

Exit:

    if (Status != NDIS_STATUS_PENDING) {
        if (ClonedRequest != NULL) {
            XdpLwfOidRequestComplete(Filter, ClonedRequest, Status);
        } else {
            XdpLwfInvokeFOidRequestComplete(
                Filter->NdisFilterHandle, RequestInterface, Request, Status);
        }
    }

    TraceExitStatus(TRACE_LWF);
}

static
VOID
XdpLwfOidInspectRequestOffloadComplete(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_ACTION Action,
    _In_ NDIS_STATUS Status
    )
{
    TraceEnter(
        TRACE_LWF,
        "Filter=%p Request=%p Oid=%x Action=%!OID_ACTION! Status=%!STATUS!",
        Filter, Request, Request->DATA.Oid, Action, Status);

    if (Action == XdpOidActionComplete) {
        goto Exit;
    }

    Status = XdpGenericInspectOidRequest(&Filter->Generic, Request);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    if (Status != NDIS_STATUS_PENDING) {
        XdpLwfCommonOidRequestInspectComplete(
            Filter, XDP_OID_REQUEST_INTERFACE_REGULAR, Request, Action, Status);
    }

    TraceExitStatus(TRACE_LWF);
}

static
NDIS_STATUS
XdpLwfOidInspectRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request
    )
{
    NDIS_STATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p Request=%p Oid=%x ", Filter, Request, Request->DATA.Oid);

    Status =
        XdpLwfOffloadInspectOidRequest(Filter, Request, XdpLwfOidInspectRequestOffloadComplete);

    TraceExitStatus(TRACE_LWF);

    return Status;
}

NDIS_STATUS
XdpLwfCommonOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _Inout_ NDIS_OID_REQUEST *Request
    )
{
    NDIS_STATUS Status;
    XDP_OID_ACTION Action = XdpOidActionPass;
    NDIS_STATUS CompletionStatus = NDIS_STATUS_FAILURE;

    TraceEnter(
        TRACE_LWF, "Filter=%p Request=%p Oid=%x RequestInterface=%!OID_REQUEST_INTERFACE!",
        Filter, Request, Request->DATA.Oid, RequestInterface);

    if (RequestInterface == XDP_OID_REQUEST_INTERFACE_REGULAR) {
        if (KeGetCurrentIrql() == DISPATCH_LEVEL) {
            //
            // NDIS serializes OID requests for filters but, unlike miniports, OID
            // requests to filters may run at dispatch level. Since our
            // implementation requires OID inspection to run at passive level, queue
            // a passive level work item and return.
            //
            FRE_ASSERT(InterlockedExchangePointer(&Filter->OidWorkerRequest, Request) == NULL);
            IoQueueWorkItemEx(Filter->OidWorker, XdpLwfOidRequestWorker, DelayedWorkQueue, Filter);
            Status = NDIS_STATUS_PENDING;
            goto Exit;
        }

        Status = XdpLwfOidInspectRequest(Filter, Request);
        if (Status != NDIS_STATUS_PENDING) {
            ASSERT(Status != NDIS_STATUS_SUCCESS);
            Action = XdpOidActionComplete;
            CompletionStatus = Status;
            goto Exit;
        }
    } else {
        //
        // Inspection of direct or synchronous OIDs will require a redesign of
        // the passive serialized worker model. Since we are not interested in
        // inspecting any of those right now, simply pass them down the stack.
        //
        Action = XdpOidActionPass;
        Status = NDIS_STATUS_SUCCESS;
    }

Exit:

    if (Status != NDIS_STATUS_PENDING) {
        XdpLwfCommonOidRequestInspectComplete(
            Filter, RequestInterface, Request, Action, CompletionStatus);
    }

    TraceExitStatus(TRACE_LWF);

    return NDIS_STATUS_PENDING;
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    return XdpLwfCommonOidRequest(Filter, XDP_OID_REQUEST_INTERFACE_REGULAR, Request);
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfDirectOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    return XdpLwfCommonOidRequest(Filter, XDP_OID_REQUEST_INTERFACE_DIRECT, Request);
}

#if DBG
_Use_decl_annotations_
NDIS_STATUS
XdpVfLwfOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    NDIS_STATUS Status;
    KIRQL OldIrql = KeGetCurrentIrql();

    //
    // Verifier hook for XdpLwfOidRequest.
    //
    // Since OIDs are typically issued at passive level but may be running at
    // dispatch level, randomly raise IRQL to dispatch level to provide code
    // coverage.
    //

    if (RtlRandomNumberInRange(0, 2)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    Status = XdpLwfOidRequest(FilterModuleContext, Request);

    KeLowerIrql(OldIrql);

    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
XdpVfLwfDirectOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    NDIS_STATUS Status;
    KIRQL OldIrql = KeGetCurrentIrql();

    //
    // Verifier hook for XdpLwfOidRequest.
    //
    // Since OIDs are typically issued at passive level but may be running at
    // dispatch level, randomly raise IRQL to dispatch level to provide code
    // coverage.
    //

    if (RtlRandomNumberInRange(0, 2)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    Status = XdpLwfDirectOidRequest(FilterModuleContext, Request);

    KeLowerIrql(OldIrql);

    return Status;
}
#endif

VOID
XdpLwfCommonOidRequestComplete(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ NDIS_STATUS Status
    )
{
    NDIS_OID_REQUEST *OriginalRequest;
    XDP_OID_CLONE *Context;

    Context = (XDP_OID_CLONE *)(&Request->SourceReserved[0]);
    OriginalRequest = Context->OriginalRequest;

    if (OriginalRequest == NULL) {
        XdpLwfOidInternalRequestComplete(Filter, Request, Status);
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

    XdpLwfInvokeFOidRequestComplete(
        Filter->NdisFilterHandle, RequestInterface, OriginalRequest, Status);
}

_Use_decl_annotations_
VOID
XdpLwfOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    XdpLwfCommonOidRequestComplete(Filter, XDP_OID_REQUEST_INTERFACE_REGULAR, Request, Status);
}

_Use_decl_annotations_
VOID
XdpLwfDirectOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    XdpLwfCommonOidRequestComplete(Filter, XDP_OID_REQUEST_INTERFACE_DIRECT, Request, Status);
}

#if DBG
_Use_decl_annotations_
VOID
XdpVfLwfOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    KIRQL OldIrql = KeGetCurrentIrql();

    //
    // Verifier hook for XdpLwfOidRequestComplete.
    //
    // Since OIDs are typically completed at passive level but may be running at
    // dispatch level, randomly raise IRQL to dispatch level to provide code
    // coverage.
    //

    if (RtlRandomNumberInRange(0, 2)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    XdpLwfOidRequestComplete(FilterModuleContext, Request, Status);

    KeLowerIrql(OldIrql);
}

_Use_decl_annotations_
VOID
XdpVfLwfDirectOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    KIRQL OldIrql = KeGetCurrentIrql();

    //
    // Verifier hook for XdpLwfDirectOidRequestComplete.
    //
    // Since OIDs are typically completed at passive level but may be running at
    // dispatch level, randomly raise IRQL to dispatch level to provide code
    // coverage.
    //

    if (RtlRandomNumberInRange(0, 2)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    XdpLwfDirectOidRequestComplete(FilterModuleContext, Request, Status);

    KeLowerIrql(OldIrql);
}

#endif
