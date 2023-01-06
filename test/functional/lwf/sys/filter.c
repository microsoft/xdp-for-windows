//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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

GLOBAL_CONTEXT LwfGlobalContext;

static
VOID
FilterReferenceFilter(
    _In_ LWF_FILTER *Filter
    )
{
    XdpIncrementReferenceCount(&Filter->ReferenceCount);
}

VOID
FilterDereferenceFilter(
    _In_ LWF_FILTER *Filter
    )
{
    if (XdpDecrementReferenceCount(&Filter->ReferenceCount)) {
        if (Filter->NblPool != NULL) {
            NdisFreeNetBufferListPool(Filter->NblPool);
        }
        ExFreePoolWithTag(Filter, POOLTAG_FILTER);
    }
}

LWF_FILTER *
FilterFindAndReferenceFilter(
    _In_ UINT32 IfIndex
    )
{
    LIST_ENTRY *Entry;
    LWF_FILTER *Filter = NULL;

    RtlAcquirePushLockShared(&LwfGlobalContext.Lock);

    Entry = LwfGlobalContext.FilterList.Flink;
    while (Entry != &LwfGlobalContext.FilterList) {
        LWF_FILTER *Candidate = CONTAINING_RECORD(Entry, LWF_FILTER, FilterListLink);
        Entry = Entry->Flink;

        if (Candidate->MiniportIfIndex == IfIndex) {
            Filter = Candidate;
            FilterReferenceFilter(Filter);
            break;
        }
    }

    RtlReleasePushLockShared(&LwfGlobalContext.Lock);

    return Filter;
}

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

    LwfGlobalContext.NdisVersion = NdisGetVersion();
    KeInitializeSpinLock(&LwfGlobalContext.Lock);
    InitializeListHead(&LwfGlobalContext.FilterList);

    RtlZeroMemory(&FChars, sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS));

    FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_3;

    if (LwfGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_685) {
        //
        // Fe (Iron) / 20348 / Windows Server 2022 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 85;
    } else if (LwfGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_682) {
        //
        // RS5 / 17763 / Windows 1809 / Windows Server 2019 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 82;
    } else if (LwfGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_660) {
        //
        // RS1 / 14393 / Windows 1607 / Windows Server 2016 or higher.
        //
        FChars.MajorNdisVersion = 6;
        FChars.MinorNdisVersion = 60;
    } else {
        TraceError(
            TRACE_CONTROL, "NDIS version %u is not supported",
            LwfGlobalContext.NdisVersion);
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
    FChars.StatusHandler                     = FilterStatus;
    FChars.ReturnNetBufferListsHandler       = FilterReturnNetBufferLists;
    FChars.ReceiveNetBufferListsHandler      = FilterReceiveNetBufferLists;
    FChars.SendNetBufferListsHandler         = FilterSendNetBufferLists;
    FChars.SendNetBufferListsCompleteHandler = FilterSendNetBufferListsComplete;

    Status =
        NdisFRegisterFilterDriver(
            DriverObject, NULL, &FChars, &LwfGlobalContext.NdisDriverHandle);
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
    if (LwfGlobalContext.NdisDriverHandle != NULL) {
        NdisFDeregisterFilterDriver(LwfGlobalContext.NdisDriverHandle);
        LwfGlobalContext.NdisDriverHandle = NULL;
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
    XdpInitializeReferenceCount(&Filter->ReferenceCount);
    InitializeListHead(&Filter->FilterListLink);
    KeInitializeSpinLock(&Filter->Lock);
    InitializeListHead(&Filter->RxFilterList);
    InitializeListHead(&Filter->StatusFilterList);
    ExInitializeRundownProtection(&Filter->NblRundown);
    ExWaitForRundownProtectionRelease(&Filter->NblRundown);

    //
    // The filter can initiate OIDs as soon as the filter attributes are set.
    // The filter cannot inject OIDs beforehand, but any of those code paths
    // are unreachable prior to attributes being set and the filter being
    // inserted in the global list.
    //
    ExInitializeRundownProtection(&Filter->OidRundown);

    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams = {0};
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = POOLTAG_NBL;
    PoolParams.ContextSize = FNIO_ENQUEUE_NBL_CONTEXT_SIZE;

    Filter->NblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
    if (Filter->NblPool == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(&FilterAttributes, sizeof(NDIS_FILTER_ATTRIBUTES));
    FilterAttributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    FilterAttributes.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
    FilterAttributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    FilterAttributes.Flags = 0;

    Status = NdisFSetAttributes(NdisFilterHandle, Filter, &FilterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&LwfGlobalContext.Lock);
    ASSERT(IsListEmpty(&Filter->FilterListLink));
    InsertTailList(&LwfGlobalContext.FilterList, &Filter->FilterListLink);
    RtlReleasePushLockExclusive(&LwfGlobalContext.Lock);

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
    ExWaitForRundownProtectionRelease(&Filter->NblRundown);

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

    ExReInitializeRundownProtection(&Filter->NblRundown);
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

    Filter->NdisState = FilterDetached;

    RtlAcquirePushLockExclusive(&LwfGlobalContext.Lock);
    if (!IsListEmpty(&Filter->FilterListLink)) {
        RemoveEntryList(&Filter->FilterListLink);
    }
    RtlReleasePushLockExclusive(&LwfGlobalContext.Lock);

    ExWaitForRundownProtectionRelease(&Filter->OidRundown);

    FilterDereferenceFilter(Filter);
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

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FilterIrpGetDatapathState(
    _In_ LWF_FILTER *Filter,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;
    BOOLEAN DatapathActive = Filter->NdisState == FilterRunning;
    NTSTATUS Status;

    TraceEnter(TRACE_CONTROL, "IfIndex=%u", Filter->MiniportIfIndex);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DatapathActive)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &DatapathActive, sizeof(DatapathActive));
    *BytesReturned = sizeof(DatapathActive);
    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}
