//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offload.tmh"

static XDP_OPEN_INTERFACE_OFFLOAD_HANDLE XdpLwfOpenInterfaceOffloadHandle;
static XDP_GET_INTERFACE_OFFLOAD_CAPABILITIES XdpLwfGetInterfaceOffloadCapabilities;
static XDP_GET_INTERFACE_OFFLOAD XdpLwfGetInterfaceOffload;
static XDP_SET_INTERFACE_OFFLOAD XdpLwfSetInterfaceOffload;
static XDP_CLOSE_INTERFACE_OFFLOAD_HANDLE XdpLwfCloseInterfaceOffloadHandle;

CONST XDP_OFFLOAD_DISPATCH XdpLwfOffloadDispatch = {
    .OpenInterfaceOffloadHandle = XdpLwfOpenInterfaceOffloadHandle,
    .GetInterfaceOffloadCapabilities = XdpLwfGetInterfaceOffloadCapabilities,
    .GetInterfaceOffload = XdpLwfGetInterfaceOffload,
    .SetInterfaceOffload = XdpLwfSetInterfaceOffload,
    .CloseInterfaceOffloadHandle = XdpLwfCloseInterfaceOffloadHandle,
};

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpLwfOffloadWorker(
    _In_ SINGLE_LIST_ENTRY *WorkQueueHead
    )
{
    while (WorkQueueHead != NULL) {
        XDP_LWF_OFFLOAD_WORKITEM *Item;
        XDP_LWF_FILTER *Filter;

        Item = CONTAINING_RECORD(WorkQueueHead, XDP_LWF_OFFLOAD_WORKITEM, Link);
        Filter = Item->Filter;
        WorkQueueHead = WorkQueueHead->Next;

        Item->WorkRoutine(Item);

        XdpLwfDereferenceFilter(Filter);
    }
}

VOID
XdpLwfOffloadQueueWorkItem(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem,
    _In_ XDP_LWF_OFFLOAD_WORK_ROUTINE *WorkRoutine
    )
{
    WorkItem->Filter = Filter;
    WorkItem->WorkRoutine = WorkRoutine;

    XdpLwfReferenceFilter(Filter);
    XdpInsertWorkQueue(Filter->Offload.WorkQueue, &WorkItem->Link);
}


static
XDP_LWF_OFFLOAD_EDGE
XdpLwfConvertHookIdToOffloadEdge(
    _In_ CONST XDP_HOOK_ID *HookId
    )
{
    ASSERT(HookId->Layer == XDP_HOOK_L2);
    if ((HookId->Direction == XDP_HOOK_RX && HookId->SubLayer == XDP_HOOK_INSPECT) ||
        (HookId->Direction == XDP_HOOK_TX && HookId->SubLayer == XDP_HOOK_INJECT)) {
        return XdpOffloadEdgeLower;
    } else if ((HookId->Direction == XDP_HOOK_RX && HookId->SubLayer == XDP_HOOK_INJECT) ||
        (HookId->Direction == XDP_HOOK_TX && HookId->SubLayer == XDP_HOOK_INSPECT)) {
        return XdpOffloadEdgeUpper;
    } else {
        ASSERT(FALSE);
        return XdpOffloadEdgeLower;
    }
}

static
NTSTATUS
XdpLwfOpenInterfaceOffloadHandle(
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Out_ VOID **InterfaceOffloadHandle
    )
{
    NTSTATUS Status;
    XDP_LWF_FILTER *Filter = InterfaceContext;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = NULL;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    OffloadContext =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*OffloadContext), POOLTAG_OFFLOAD);
    if (OffloadContext == NULL) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed to allocate interface offload context", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    TraceVerbose(TRACE_LWF, "Filter=%p OffloadContext=%p Created", Filter, OffloadContext);

    OffloadContext->Filter = Filter;
    OffloadContext->Edge = XdpLwfConvertHookIdToOffloadEdge(HookId);

    XdpLwfReferenceFilter(Filter);
    *InterfaceOffloadHandle = OffloadContext;
    OffloadContext = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (OffloadContext != NULL) {
        ExFreePoolWithTag(OffloadContext, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
VOID
XdpLwfCloseInterfaceOffloadHandle(
    _In_ VOID *InterfaceOffloadHandle
    )
{
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = InterfaceOffloadHandle;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;

    TraceEnter(TRACE_LWF, "OffloadContext=%p", OffloadContext);

    //
    // Revert the handle's configured offloads.
    //

    XdpLwfOffloadRssCloseInterfaceOffloadHandle(Filter, OffloadContext);

    TraceVerbose(TRACE_LWF, "OffloadContext=%p Deleted", OffloadContext);

    ExFreePoolWithTag(OffloadContext, POOLTAG_OFFLOAD);

    XdpLwfDereferenceFilter(Filter);

    TraceExitSuccess(TRACE_LWF);
}

static
NTSTATUS
XdpLwfGetInterfaceOffloadCapabilities(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadCapabilities,
    _Inout_ UINT32 *OffloadCapabilitiesSize
    )
{
    NTSTATUS Status;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = InterfaceOffloadHandle;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

    switch (OffloadType) {
    case XdpOffloadRss:
        ASSERT(OffloadCapabilities != NULL);
        Status =
            XdpLwfOffloadRssGetCapabilities(
                Filter, OffloadCapabilities, OffloadCapabilitiesSize);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
NTSTATUS
XdpLwfGetInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadParams,
    _Inout_ UINT32 *OffloadParamsSize
    )
{
    NTSTATUS Status;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = InterfaceOffloadHandle;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

    switch (OffloadType) {
    case XdpOffloadRss:
        ASSERT(OffloadParams != NULL);
        Status = XdpLwfOffloadRssGet(Filter, OffloadContext, OffloadParams, OffloadParamsSize);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
NTSTATUS
XdpLwfSetInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    )
{
    NTSTATUS Status;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = InterfaceOffloadHandle;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

    switch (OffloadType) {
    case XdpOffloadRss:
        Status = XdpLwfOffloadRssSet(Filter, OffloadContext, OffloadParams, OffloadParamsSize);
        break;
    case XdpOffloadQeo:
        Status = XdpLwfOffloadQeoSet(Filter, OffloadContext, OffloadParams, OffloadParamsSize);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

typedef struct _XDP_LWF_OFFLOAD_INSPECT_OID {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _In_ NDIS_OID_REQUEST *Request;
    _In_ XDP_OID_INSPECT_COMPLETE *InspectComplete;
} XDP_LWF_OFFLOAD_INSPECT_OID;

static
_Offload_work_routine_
VOID
XdpLwfOffloadInspectOidRequestWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_INSPECT_OID *InspectRequest =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_INSPECT_OID, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    NDIS_OID_REQUEST *Request = InspectRequest->Request;
    NTSTATUS Status;
    XDP_OID_ACTION Action = XdpOidActionPass;
    NDIS_STATUS CompletionStatus = NDIS_STATUS_SUCCESS;

    Status = XdpLwfOffloadRssInspectOidRequest(Filter, Request, &Action, &CompletionStatus);
    if (!NT_SUCCESS(Status) || Action != XdpOidActionPass) {
        goto Exit;
    }

Exit:

    InspectRequest->InspectComplete(Filter, Request, Action, CompletionStatus);

    ExFreePoolWithTag(InspectRequest, POOLTAG_OFFLOAD);
}

NDIS_STATUS
XdpLwfOffloadInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_INSPECT_COMPLETE *InspectComplete
    )
{
    XDP_LWF_OFFLOAD_INSPECT_OID *InspectRequest;
    NDIS_STATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p Request=%p OID=%x", Filter, Request, Request->DATA.Oid);

    InspectRequest = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*InspectRequest), POOLTAG_OFFLOAD);
    if (InspectRequest == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    InspectRequest->Request = Request;
    InspectRequest->InspectComplete = InspectComplete;
    XdpLwfOffloadQueueWorkItem(
        Filter, &InspectRequest->WorkItem, XdpLwfOffloadInspectOidRequestWorker);
    Status = NDIS_STATUS_PENDING;

Exit:

    TraceExitStatus(TRACE_LWF);

    return Status;
}

VOID
XdpLwfOffloadTransformNbls(
    _In_ XDP_LWF_FILTER *Filter,
    _Inout_ NBL_COUNTED_QUEUE *NblList,
    _In_ UINT32 XdpInspectFlags
    )
{
    KIRQL OldIrql = DISPATCH_LEVEL;

    if (!(XdpInspectFlags & XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH)) {
        OldIrql = KeRaiseIrqlToDpcLevel();
    }

    //
    // If the offloads of the lower and upper edge differ, some offloads might
    // require NBL transformations before they are passed up to the traditional
    // stack.
    //
    if (ReadPointerNoFence(&Filter->Offload.LowerEdge.Rss) != NULL) {
        for (NET_BUFFER_LIST *Nbl = NblList->Queue.First; Nbl != NULL; Nbl = Nbl->Next) {
            XdpLwfOffloadRssNblTransform(Filter, Nbl);
        }
    }

    if (OldIrql != DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }
}

typedef struct _XDP_LWF_OFFLOAD_DEACTIVATE {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
} XDP_LWF_OFFLOAD_DEACTIVATE;

static
_Offload_work_routine_
VOID
XdpLwfOffloadDeactivateWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_DEACTIVATE *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_DEACTIVATE, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;

    XdpLwfOffloadRssDeactivate(Filter);

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

VOID
XdpLwfOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_DEACTIVATE Request = {0};

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    //
    // Clean up any state requiring the serialized work queue. If the queue
    // doesn't exist, then the offload module never started and there is nothing
    // to clean up.
    //
    if (Filter->Offload.WorkQueue != NULL) {
        KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
        XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadDeactivateWorker);
        KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);
    }

    TraceExitSuccess(TRACE_LWF);
}

NTSTATUS
XdpLwfOffloadStart(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    Filter->Offload.WorkQueue =
        XdpCreateWorkQueue(XdpLwfOffloadWorker, PASSIVE_LEVEL, XdpLwfDriverObject, NULL);
    if (Filter->Offload.WorkQueue == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_LWF);

    return Status;
}

VOID
XdpLwfOffloadUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    if (Filter->Offload.WorkQueue != NULL) {
        XdpShutdownWorkQueue(Filter->Offload.WorkQueue, FALSE);
        Filter->Offload.WorkQueue = NULL;
    }

    TraceExitSuccess(TRACE_LWF);
}
