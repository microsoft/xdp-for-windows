//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "filter.tmh"

#define FILTER_FRIENDLY_NAME        L"XDP Functional Test Lightweight Filter"
#define FILTER_UNIQUE_NAME          L"{4b399bac-cfdf-477b-9c72-abed8717bc1a}"
#define FILTER_SERVICE_NAME         L"xdpfnlwf"

FILTER_ATTACH FilterAttach;
FILTER_DETACH FilterDetach;
FILTER_RESTART FilterRestart;
FILTER_PAUSE FilterPause;
FILTER_SET_MODULE_OPTIONS FilterSetOptions;

static NDIS_HANDLE NdisDriverHandle = NULL;
static UINT32 NdisVersion;

NTSTATUS
FilterStart(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    NDIS_STATUS Status;
    NDIS_FILTER_DRIVER_CHARACTERISTICS FChars;
    NDIS_STRING ServiceName = RTL_CONSTANT_STRING(FILTER_SERVICE_NAME);
    NDIS_STRING UniqueName = RTL_CONSTANT_STRING(FILTER_UNIQUE_NAME);
    NDIS_STRING FriendlyName = RTL_CONSTANT_STRING(FILTER_FRIENDLY_NAME);

    NdisVersion = NdisGetVersion();

    RtlZeroMemory(&FChars, sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS));

    FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_3;

    if (NdisVersion >= NDIS_RUNTIME_VERSION_685) {
        //
        // Fe (Iron) / 20348 / Windows Server 2022 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 85;
    } else if (NdisVersion >= NDIS_RUNTIME_VERSION_682) {
        //
        // RS5 / 17763 / Windows 1809 / Windows Server 2019 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 82;
    } else if (NdisVersion >= NDIS_RUNTIME_VERSION_660) {
        //
        // RS1 / 14393 / Windows 1607 / Windows Server 2016 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 60;
    } else {
        TraceError(
            TRACE_CONTROL, "NDIS version %u is not supported", NdisVersion);
        Status = NDIS_STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    FChars.MajorDriverVersion = 0;
    FChars.MinorDriverVersion = 1;
    FChars.Flags = NDIS_FILTER_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;

    FChars.FriendlyName = FriendlyName;
    FChars.UniqueName = UniqueName;
    FChars.ServiceName = ServiceName;

    FChars.AttachHandler                     = FilterAttach;
    FChars.DetachHandler                     = FilterDetach;
    FChars.RestartHandler                    = FilterRestart;
    FChars.PauseHandler                      = FilterPause;
    FChars.SetFilterModuleOptionsHandler     = FilterSetOptions;
    FChars.OidRequestHandler                 = FilterOidRequest;
    FChars.OidRequestCompleteHandler         = FilterOidRequestComplete;
    FChars.ReturnNetBufferListsHandler       = FilterReturnNetBufferLists;
    FChars.ReceiveNetBufferListsHandler      = FilterReceiveNetBufferLists;
    FChars.SendNetBufferListsHandler         = FilterSendNetBufferLists;
    FChars.SendNetBufferListsCompleteHandler = FilterSendNetBufferListsComplete;

    Status =
        NdisFRegisterFilterDriver(
            DriverObject, NULL, &FChars, &NdisDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        TraceError(
            TRACE_CONTROL, "Failed to register filter driver Status=%!STATUS!",
            Status);
        goto Exit;
    }

Exit:

    return XdpConvertNdisStatusToNtStatus(Status);
}

VOID
FilterStop(
    VOID
    )
{
    if (NdisDriverHandle != NULL) {
        NdisFDeregisterFilterDriver(NdisDriverHandle);
        NdisDriverHandle = NULL;
    }
}

_Use_decl_annotations_
NDIS_STATUS
FilterAttach(
    NDIS_HANDLE NdisFilterHandle,
    NDIS_HANDLE FilterDriverContext,
    NDIS_FILTER_ATTACH_PARAMETERS *AttachParameters
    )
{
    LWF_FILTER *Filter = NULL;
    NDIS_STATUS Status;
    NDIS_FILTER_ATTRIBUTES FilterAttributes;

    UNREFERENCED_PARAMETER(FilterDriverContext);

    if (AttachParameters->MiniportMediaType != NdisMedium802_3) {
        Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
        goto Exit;
    }

    NDIS_DECLARE_FILTER_MODULE_CONTEXT(LWF_FILTER);
    Filter =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Filter), POOLTAG_FILTER);
    if (Filter == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Filter->MiniportIfIndex = AttachParameters->BaseMiniportIfIndex;
    Filter->NdisFilterHandle = NdisFilterHandle;
    Filter->NdisState = FilterPaused;

    RtlZeroMemory(&FilterAttributes, sizeof(NDIS_FILTER_ATTRIBUTES));
    FilterAttributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    FilterAttributes.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
    FilterAttributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    FilterAttributes.Flags = 0;

    Status = NdisFSetAttributes(NdisFilterHandle, Filter, &FilterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Filter = NULL;

Exit:

    TraceInfo(
        TRACE_CONTROL, "IfIndex=%u Status=%!STATUS!",
        AttachParameters->BaseMiniportIfIndex, Status);

    if (Filter != NULL) {
        FilterDetach((NDIS_HANDLE)Filter);
    }

    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterPause(
    NDIS_HANDLE FilterModuleContext,
    NDIS_FILTER_PAUSE_PARAMETERS *PauseParameters
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    ASSERT(Filter->NdisState == FilterRunning);

    TraceInfo(TRACE_CONTROL, "IfIndex=%u", Filter->MiniportIfIndex);

    Filter->NdisState = FilterPaused;

    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
NDIS_STATUS
FilterRestart(
    NDIS_HANDLE FilterModuleContext,
    NDIS_FILTER_RESTART_PARAMETERS *RestartParameters
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    ASSERT(Filter->NdisState == FilterPaused);

    TraceInfo(TRACE_CONTROL, "IfIndex=%u", Filter->MiniportIfIndex);

    Filter->NdisState = FilterRunning;

    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID
FilterDetach(
    NDIS_HANDLE FilterModuleContext
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    TraceInfo(TRACE_CONTROL, "IfIndex=%u", Filter->MiniportIfIndex);

    ExFreePoolWithTag(Filter, POOLTAG_FILTER);
}

_Use_decl_annotations_
NDIS_STATUS
FilterSetOptions(
    NDIS_HANDLE FilterModuleContext
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    ASSERT(Filter->NdisState == FilterPaused);

    TraceInfo(TRACE_CONTROL, "IfIndex=%u", Filter->MiniportIfIndex);

    return NDIS_STATUS_SUCCESS;
}
