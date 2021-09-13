//
// Copyright (C) Microsoft Corporation. All rights reserved.
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

NDIS_HANDLE XdpLwfNdisDriverHandle = NULL;
UINT32 XdpLwfNdisVersion;

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
    FChars.OidRequestHandler                = XdpLwfOidRequest;
    FChars.OidRequestCompleteHandler        = XdpLwfOidRequestComplete;

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
    NDIS_FILTER_ATTRIBUTES FilterAttributes;

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
    Filter->FilterHandle = NdisFilterHandle;
    Filter->NdisState = FilterPaused;

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

    XdpGenericCreateBinding(&Filter->Generic, Filter->FilterHandle, Filter->MiniportIfIndex);
    XdpNativeCreateBinding(&Filter->Native, Filter->FilterHandle, Filter->MiniportIfIndex);

    Filter = NULL;

Exit:

    TraceInfo(
        TRACE_GENERIC, "IfIndex=%u Status=%!STATUS!",
        AttachParameters->BaseMiniportIfIndex, Status);

    if (Filter != NULL) {
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

_Use_decl_annotations_
VOID
XdpLwfFilterDetach(
    NDIS_HANDLE FilterModuleContext
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    TraceInfo(TRACE_GENERIC, "IfIndex=%u", Filter->MiniportIfIndex);

    // TODO: we could unbind both interfaces concurrently.
    XdpNativeDeleteBinding(&Filter->Native);
    XdpGenericDeleteBinding(&Filter->Generic);

    ExFreePoolWithTag(Filter, POOLTAG_FILTER);
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
