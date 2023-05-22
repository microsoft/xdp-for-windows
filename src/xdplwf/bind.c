//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "bind.tmh"

#define FILTER_FRIENDLY_NAME        L"XDP Lightweight Filter"
#define FILTER_UNIQUE_NAME          L"{c0be1ebc-74b8-4ba9-8c1e-ecd227e2be3b}"
#define FILTER_SERVICE_NAME         L"xdp"

FILTER_ATTACH XdpLwfFilterAttach;
FILTER_DETACH XdpLwfFilterDetach;
FILTER_RESTART XdpLwfFilterRestart;
FILTER_PAUSE XdpLwfFilterPause;
FILTER_SET_MODULE_OPTIONS XdpLwfFilterSetOptions;
FILTER_NET_PNP_EVENT XdpLwfFilterPnpEvent;

NDIS_HANDLE XdpLwfNdisDriverHandle = NULL;
UINT32 XdpLwfNdisVersion;

VOID
XdpLwfReferenceFilter(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XdpIncrementReferenceCount(&Filter->ReferenceCount);
}

VOID
XdpLwfDereferenceFilter(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    if (XdpDecrementReferenceCount(&Filter->ReferenceCount)) {
        XdpLwfOffloadUnInitialize(Filter);
        ExFreePoolWithTag(Filter, POOLTAG_FILTER);
    }
}

NTSTATUS
XdpLwfBindStart(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    NDIS_STATUS Status;
    NDIS_FILTER_DRIVER_CHARACTERISTICS FChars;
    NDIS_STRING ServiceName = RTL_CONSTANT_STRING(FILTER_SERVICE_NAME);
    NDIS_STRING UniqueName = RTL_CONSTANT_STRING(FILTER_UNIQUE_NAME);
    NDIS_STRING FriendlyName = RTL_CONSTANT_STRING(FILTER_FRIENDLY_NAME);

    XdpLwfNdisVersion = NdisGetVersion();

    RtlZeroMemory(&FChars, sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS));

    FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_3;

    if (XdpLwfNdisVersion >= NDIS_RUNTIME_VERSION_685) {
        //
        // Fe (Iron) / 20348 / Windows Server 2022 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 85;
    } else if (XdpLwfNdisVersion >= NDIS_RUNTIME_VERSION_682) {
        //
        // RS5 / 17763 / Windows 1809 / Windows Server 2019 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 82;
    } else if (XdpLwfNdisVersion >= NDIS_RUNTIME_VERSION_660) {
        //
        // RS1 / 14393 / Windows 1607 / Windows Server 2016 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 60;
    } else {
        TraceError(TRACE_GENERIC, "NDIS version %u is not supported", XdpLwfNdisVersion);
        TraceError(TRACE_NATIVE, "NDIS version %u is not supported", XdpLwfNdisVersion);
        Status = NDIS_STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    FChars.MajorDriverVersion = XDP_MAJOR_VER;
    FChars.MinorDriverVersion = XDP_MINOR_VER;
    FChars.Flags = NDIS_FILTER_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;

    FChars.FriendlyName = FriendlyName;
    FChars.UniqueName = UniqueName;
    FChars.ServiceName = ServiceName;

    FChars.AttachHandler                    = XdpLwfFilterAttach;
    FChars.DetachHandler                    = XdpLwfFilterDetach;
    FChars.RestartHandler                   = XdpLwfFilterRestart;
    FChars.PauseHandler                     = XdpLwfFilterPause;
    FChars.SetFilterModuleOptionsHandler    = XdpLwfFilterSetOptions;
    FChars.NetPnPEventHandler               = XdpLwfFilterPnpEvent;

#if DBG
    FChars.OidRequestHandler                = XdpVfLwfOidRequest;
    FChars.OidRequestCompleteHandler        = XdpVfLwfOidRequestComplete;
    FChars.DirectOidRequestHandler          = XdpVfLwfDirectOidRequest;
    FChars.DirectOidRequestCompleteHandler  = XdpVfLwfDirectOidRequestComplete;
#else
    FChars.OidRequestHandler                = XdpLwfOidRequest;
    FChars.OidRequestCompleteHandler        = XdpLwfOidRequestComplete;
    FChars.DirectOidRequestHandler          = XdpLwfDirectOidRequest;
    FChars.DirectOidRequestCompleteHandler  = XdpLwfDirectOidRequestComplete;
#endif

    Status = NdisFRegisterFilterDriver(DriverObject, NULL, &FChars, &XdpLwfNdisDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    return XdpConvertNdisStatusToNtStatus(Status);
}

VOID
XdpLwfBindStop(
    VOID
    )
{
    if (XdpLwfNdisDriverHandle != NULL) {
        NdisFDeregisterFilterDriver(XdpLwfNdisDriverHandle);
        XdpLwfNdisDriverHandle = NULL;
    }
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfFilterAttach(
    NDIS_HANDLE NdisFilterHandle,
    NDIS_HANDLE FilterDriverContext,
    NDIS_FILTER_ATTACH_PARAMETERS *AttachParameters
    )
{
    XDP_LWF_FILTER *Filter = NULL;
    NDIS_STATUS Status;
    NTSTATUS LocalStatus = STATUS_SUCCESS;
    NDIS_FILTER_ATTRIBUTES FilterAttributes;
    XDP_ADD_INTERFACE AddIf[2];
    UINT32 IfCount = 0;
    UINT32 Index = 0;

    UNREFERENCED_PARAMETER(FilterDriverContext);

    if (AttachParameters->MiniportMediaType != NdisMedium802_3) {
        Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
        goto Exit;
    }

    NDIS_DECLARE_FILTER_MODULE_CONTEXT(XDP_LWF_FILTER);
    Filter =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Filter), POOLTAG_FILTER);
    if (Filter == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Filter->MiniportIfIndex = AttachParameters->BaseMiniportIfIndex;
    Filter->NdisFilterHandle = NdisFilterHandle;
    Filter->NdisState = FilterPaused;
    XdpInitializeReferenceCount(&Filter->ReferenceCount);

    Filter->OidWorker = IoAllocateWorkItem((DEVICE_OBJECT *)XdpLwfDriverObject);
    if (Filter->OidWorker == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Status = XdpLwfOffloadStart(Filter);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpIfCreateInterfaceSet(
            Filter->MiniportIfIndex, &XdpLwfOffloadDispatch, Filter,
            &Filter->XdpIfInterfaceSetHandle);
    if (!NT_SUCCESS(Status)) {
        ASSERT(Filter->XdpIfInterfaceSetHandle == NULL);
        Status = XdpConvertNtStatusToNdisStatus(Status);
        goto Exit;
    }

    Status =
        XdpGenericAttachInterface(
            &Filter->Generic, Filter, Filter->NdisFilterHandle, Filter->MiniportIfIndex,
            &AddIf[Index]);
    if (NT_SUCCESS(Status)) {
        IfCount++;
        Index++;
    }

    //
    // TODO: create a work item for the OIDs below.
    // The NdisFSetAttributes function enables the OID inspection path and
    // has no inverse. Therefore we must call this function only after we've
    // guaranteed the attach cannot otherwise fail.
    //

    RtlZeroMemory(&FilterAttributes, sizeof(NDIS_FILTER_ATTRIBUTES));
    FilterAttributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    FilterAttributes.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
    FilterAttributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    FilterAttributes.Flags = 0;

    Status = NdisFSetAttributes(NdisFilterHandle, Filter, &FilterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    //
    // We're now committed to succeeding, since there's no inverse to
    // NdisFSetAttributes. Any subsequent failures must be absorbed.
    //
    Status = NDIS_STATUS_SUCCESS;

    XdpLwfOffloadRssInitialize(Filter);

    LocalStatus =
        XdpNativeAttachInterface(
            &Filter->Native, Filter, Filter->NdisFilterHandle, Filter->MiniportIfIndex,
            &AddIf[Index]);
    if (NT_SUCCESS(LocalStatus)) {
        IfCount++;
        Index++;
    }

    if (IfCount == 0) {
        ASSERT(!NT_SUCCESS(LocalStatus));
        goto Exit;
    }

    //
    // N.B. We are adding both interfaces at once in order to avoid a race
    // condition where XSK bind races interface additions. At the time of XSK
    // bind, a non-optimal interface might only be available while the optimal
    // interface is not yet added. "Optimal" has a flexible definition based on
    // the XSK's requirements.
    //
    LocalStatus = XdpIfAddInterfaces(Filter->XdpIfInterfaceSetHandle, &AddIf[0], IfCount);
    if (!NT_SUCCESS(LocalStatus)) {
        goto Exit;
    }

    Filter = NULL;

Exit:

    TraceInfo(
        TRACE_GENERIC, "IfIndex=%u Status=%!STATUS! LocalStatus=%!STATUS!",
        AttachParameters->BaseMiniportIfIndex, Status, LocalStatus);

    if (Status != NDIS_STATUS_SUCCESS && Filter != NULL) {
        XdpLwfFilterDetach((NDIS_HANDLE)Filter);
    }

    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfFilterPause(
    NDIS_HANDLE FilterModuleContext,
    NDIS_FILTER_PAUSE_PARAMETERS *PauseParameters
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    ASSERT(Filter->NdisState == FilterRunning);
    Filter->NdisState = FilterPausing;

    XdpGenericPause(&Filter->Generic);

    Filter->NdisState = FilterPaused;

    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfFilterRestart(
    NDIS_HANDLE FilterModuleContext,
    NDIS_FILTER_RESTART_PARAMETERS *RestartParameters
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    XdpGenericRestart(&Filter->Generic, RestartParameters);

    Filter->NdisState = FilterRunning;

    return NDIS_STATUS_SUCCESS;
}

static
VOID
XdpLwfFilterPreDetach(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    //
    // By the time NDIS invokes the filter detach routine, many NDIS code paths
    // are disabled, so OID and status indications generated from this filter
    // cant't reach other components. During orderly teardown, NDIS provides a
    // notification that detachment is imminent while the filter can still
    // issue OID requests and status indications.
    //

    ASSERT(!Filter->PreDetached);

    Filter->PreDetached = TRUE;

    if (Filter->XdpIfInterfaceSetHandle != NULL) {
        XdpNativeDetachInterface(&Filter->Native);
        XdpGenericDetachInterface(&Filter->Generic);
        XdpIfDeleteInterfaceSet(Filter->XdpIfInterfaceSetHandle);
        XdpNativeWaitForDetachInterfaceComplete(&Filter->Native);
        XdpGenericWaitForDetachInterfaceComplete(&Filter->Generic);
    }

    XdpLwfOffloadDeactivate(Filter);
}

_Use_decl_annotations_
VOID
XdpLwfFilterDetach(
    NDIS_HANDLE FilterModuleContext
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    TraceInfo(TRACE_GENERIC, "IfIndex=%u", Filter->MiniportIfIndex);

    if (!Filter->PreDetached) {
        XdpLwfFilterPreDetach(Filter);
    }

    if (Filter->OidWorker != NULL) {
        FRE_ASSERT(Filter->OidWorkerRequest == NULL);
        IoFreeWorkItem(Filter->OidWorker);
        Filter->OidWorker = NULL;
    }

    Filter->NdisFilterHandle = NULL;

    XdpLwfDereferenceFilter(Filter);
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfFilterSetOptions(
    NDIS_HANDLE FilterModuleContext
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    return XdpGenericFilterSetOptions(&Filter->Generic);
}

_Use_decl_annotations_
NDIS_STATUS
XdpLwfFilterPnpEvent(
    NDIS_HANDLE FilterModuleContext,
    NET_PNP_EVENT_NOTIFICATION *NetPnPEventNotification
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    if (NetPnPEventNotification->NetPnPEvent.NetEvent == NetEventFilterPreDetach) {
        XdpLwfFilterPreDetach(Filter);
    }

    return NdisFNetPnPEvent(Filter->NdisFilterHandle, NetPnPEventNotification);
}
