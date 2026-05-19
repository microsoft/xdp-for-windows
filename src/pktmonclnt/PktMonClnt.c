//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// This module is linked into every driver that wishes to publish network
// packets into the pktmon diagnostics framework
//

#include "precomp.h"
#include "PktMonClnt.h"

#define PKTMON_RUNDOWN_TAG    'rdMP'

//
// Standard NMR client handlers
//
NPI_CLIENT_ATTACH_PROVIDER_FN PktMonAttachProvider;
NPI_CLIENT_DETACH_PROVIDER_FN PktMonDetachProvider;
NPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN PktMonCleanupBindingContext;

//
// Custom NMR client handlers
//
PKTMON_CLIENT_ENABLE      PktMonClientEnableCallback;
PKTMON_CLIENT_COMP_ENABLE PktMonCompEnableCallback;
PKTMON_CLIENT_COMP_CLOSE  PktMonCompCloseCallback;

//
// Packet monitor NMR client characteristics
//
NPI_CLIENT_CHARACTERISTICS PktMonClientNotify =
{
    .Version = 0,
    .Length = sizeof(NPI_CLIENT_CHARACTERISTICS),
    .ClientAttachProvider = PktMonAttachProvider,
    .ClientDetachProvider = PktMonDetachProvider,
    .ClientCleanupBindingContext = PktMonCleanupBindingContext,
    .ClientRegistrationInstance = {
        .Version = 0,
        .Size = sizeof(NPI_REGISTRATION_INSTANCE),
        .NpiId = &NPI_PKTMON_INTERFACE_ID,
        .ModuleId = NULL,
        .Number = 0,
        .NpiSpecificCharacteristics = NULL
    }
};

//
// NMR client dispatch table
//
PKTMON_CLIENT_DISPATCH const PktMonClientDispatch =
{
    .Size = sizeof(PKTMON_CLIENT_DISPATCH),
    .ClientEnable = PktMonClientEnableCallback,
    .CompEnable = PktMonCompEnableCallback,
    .CompClose = PktMonCompCloseCallback,
};

PKTMON_CLIENT_CONTEXT PktMon = { 0 };

//
// Local component list
//
KMUTEX     PktMonCompMutex;
LIST_ENTRY PktMonCompList;
LONG       PktMonCompCount = 0;

//
// NMR Client Attach synchronization is needed
//
BOOLEAN NmrAttachSync = TRUE;

//
// NMR client initialization
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientInitialize(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER EnumComponents
)
{
    NTSTATUS status;

    PAGED_CODE();

    KeInitializeMutex(&PktMonCompMutex, 0);
    InitializeListHead(&PktMonCompList);

    PktMonClientNotify.ClientRegistrationInstance.ModuleId = ModuleId;
    PktMon.EnumComponents = EnumComponents;

    status = NmrRegisterClient(&PktMonClientNotify,
                               &PktMon,
                               &PktMon.NmrClientHandle);
    return status;
}

//
// NMR client initialization with cleanup function during pktmon detach
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientInitializeEx(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER   EnumComponents,
    _In_opt_ PKTMON_CLIENT_CLEANUP_HANDLER     CleanupComponents,
    _In_opt_ PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent
)
{
    PktMon.CleanupComponents = CleanupComponents;
    PktMon.NotifyComponent   = NotifyComponent;

    return PktMonClientInitialize(
               ModuleId,
               EnumComponents);
}

//
// NMR client initialization with cleanup function during pktmon detach
// Caller is providing synchronization with EnumComponents callback
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientInitializeNoSync(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER EnumComponents,
    _In_opt_ PKTMON_CLIENT_CLEANUP_HANDLER CleanupComponents
    )
{
    NmrAttachSync = FALSE;

    return PktMonClientInitializeEx(
        ModuleId,
        EnumComponents,
        CleanupComponents,
        NULL);
}

//
// NMR client uninitialization and cleanup
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PktMonClientUninitialize()
{
    NTSTATUS status;

    PAGED_CODE();

    PktMon.ProviderDispatch = NULL;

    if (PktMon.NmrClientHandle != NULL)
    {
        status = NmrDeregisterClient(PktMon.NmrClientHandle);
        if (status == STATUS_PENDING)
        {
            NmrWaitForClientDeregisterComplete(PktMon.NmrClientHandle);
        }
        PktMon.NmrClientHandle = NULL;
    }

    if (PktMon.RundownRef != NULL)
    {
        ExFreeCacheAwareRundownProtection(PktMon.RundownRef);
        PktMon.RundownRef = NULL;
    }
}

//
// NMR provider attach
//
__declspec(code_seg("PAGE"))
NTSTATUS
NTAPI
PktMonAttachProvider(
    _In_ HANDLE NmrBindingHandle,
    _In_ PVOID  ClientContext,
    _In_ PNPI_REGISTRATION_INSTANCE  ProviderRegistrationInstance
    )
{
    NTSTATUS status;
    PVOID ProviderContext = NULL;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ClientContext);
    UNREFERENCED_PARAMETER(ProviderRegistrationInstance);

    //
    // Attach only one provider
    //
    if (PktMon.ProviderDispatch != NULL)
    {
        status = STATUS_NOINTERFACE;
        goto Cleanup;
    }

    if (PktMon.RundownRef == NULL)
    {
        PktMon.RundownRef = ExAllocateCacheAwareRundownProtection(NonPagedPoolNx,
                                                                  PKTMON_RUNDOWN_TAG);
        if (PktMon.RundownRef == NULL)
        {
            status = STATUS_NO_MEMORY;
            goto Cleanup;
        }
    }
    else
    {
        ExReInitializeRundownProtectionCacheAware(PktMon.RundownRef);
    }

    status = NmrClientAttachProvider(
        NmrBindingHandle,
        ClientContext,
        &PktMonClientDispatch,
        &ProviderContext,
        (const VOID**)&PktMon.ProviderDispatch);
    if (status != STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    if (NmrAttachSync)
    {
        (VOID)KeWaitForSingleObject(&PktMonCompMutex, Executive, KernelMode, FALSE, NULL);
    }

    PktMon.ProviderContext = ProviderContext;

    //
    // Register client components
    //
    PktMon.EnumComponents();

    if (NmrAttachSync)
    {
        KeReleaseMutex(&PktMonCompMutex, FALSE);
    }

Cleanup:

    if (status != STATUS_SUCCESS)
    {
        if (PktMon.RundownRef != NULL)
        {
            ExFreeCacheAwareRundownProtection(PktMon.RundownRef);
            PktMon.RundownRef = NULL;
        }
    }
    return status;
}

//
// NMR provider detach
//
__declspec(code_seg("PAGE"))
NTSTATUS
NTAPI
PktMonDetachProvider(
    _In_ PVOID  ClientContext
    )
{
    LIST_ENTRY* entry = NULL;
    PKTMON_COMPONENT_CONTEXT* compContext = NULL;
    PKTMON_EDGE_CONTEXT* edgeContext = NULL;
    BOOLEAN compEnable = FALSE;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ClientContext);

    PktMon.Enabled = FALSE;

    ExWaitForRundownProtectionReleaseCacheAware(PktMon.RundownRef);

    PktMon.ProviderContext = NULL;
    PktMon.ProviderDispatch = NULL;

    KeWaitForSingleObject(&PktMonCompMutex, Executive, KernelMode, FALSE, NULL);

    while (!IsListEmpty(&PktMonCompList))
    {
        entry = RemoveTailList(&PktMonCompList);
        compContext = CONTAINING_RECORD(entry, PKTMON_COMPONENT_CONTEXT, ListLink);
        PktMonCompCount--;

        while (!IsListEmpty(&compContext->EdgeList))
        {
            entry = RemoveTailList(&compContext->EdgeList);
            edgeContext = CONTAINING_RECORD(entry, PKTMON_EDGE_CONTEXT, ListLink);
            RtlZeroMemory(edgeContext, sizeof(*edgeContext));
        }

        compEnable = (compContext->FlowEnabled || compContext->DropEnabled);
        compContext->FlowEnabled = FALSE;
        compContext->DropEnabled = FALSE;

        if (compEnable && (PktMon.NotifyComponent != NULL))
        {
            PktMon.NotifyComponent(compContext);
        }

        RtlZeroMemory(compContext, sizeof(*compContext));
    }

    KeReleaseMutex(&PktMonCompMutex, FALSE);

    if (PktMon.CleanupComponents != NULL)
    {
        PktMon.CleanupComponents();
    }

    return STATUS_SUCCESS;
}

//
// NMR provider context cleanup
//
__declspec(code_seg("PAGE"))
VOID
NTAPI
PktMonCleanupBindingContext(
    _In_ PVOID  ClientContext
    )
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ClientContext);
}

//
// Enable monitoring for NMR client
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PktMonClientEnableCallback(
    _In_ BOOLEAN Enable
    )
{
    PAGED_CODE();

    PktMon.Enabled = Enable;
}

//
// Enable monitoring for component
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PktMonCompEnableCallback(
    _In_ PKTMON_CLIENT_COMP_ENABLE_IN *Enable
    )
{
    PKTMON_COMPONENT_CONTEXT *compContext;

    PAGED_CODE();

    if (PktMon.ProviderContext != NULL)
    {
        compContext = (PKTMON_COMPONENT_CONTEXT*)Enable->CompContext;

        compContext->FlowEnabled = Enable->FlowEnabled;
        compContext->DropEnabled = Enable->DropEnabled;

        if (PktMon.NotifyComponent != NULL)
        {
            PktMon.NotifyComponent(compContext);
        }
    }
}

//
// Close component registration
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PktMonCompCloseCallback(
    _In_ HANDLE CompContext
    )
{
    UNREFERENCED_PARAMETER(CompContext);
}

static
PKTMON_PACKET_TYPE
MediaTypeToPacketType(
    _In_ NDIS_MEDIUM MediaType
)
{
    PKTMON_PACKET_TYPE ret;

    switch (MediaType)
    {
    case NdisMedium802_3:           ret = PktMonPayload_Ethernet;   break;
    case NdisMediumNative802_11:    ret = PktMonPayload_WiFi;       break;
    case NdisMediumIP:              ret = PktMonPayload_IP;         break;
    case NdisMediumWirelessWan:     ret = PktMonPayload_IP;         break;
    default:                        ret = PktMonPayload_Unknown;    break;
    }

    return ret;
}

//
// Component registration with PacketType
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientComponentRegisterEx(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PUNICODE_STRING Name,
    _In_ PUNICODE_STRING Description,
    _In_ PKTMON_COMPONENT_TYPE Type,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_DIRECTION DirTagIn,
    _In_ PKTMON_DIRECTION DirTagOut
    )
{
    NTSTATUS status;
    BOOLEAN mutexOwner = FALSE;
    PKTMON_COMPONENT_IN compIn = { 0 };

    PAGED_CODE();

    if (PktMon.ProviderContext == NULL)
    {
        //
        // Provider is not up yet
        //
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    KeWaitForSingleObject(&PktMonCompMutex, Executive, KernelMode, FALSE, NULL);
    mutexOwner = TRUE;

    if (PktMon.ProviderContext == NULL)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    if (CompContext->CompHandle != NULL)
    {
        //
        // Component is already registered
        //
        status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    CompContext->EdgeCount = 0;
    InitializeListHead(&CompContext->EdgeList);

    compIn.Header.Size = sizeof(compIn);

    compIn.CompContext = CompContext;
    compIn.Name = Name;
    compIn.Description = Description;
    compIn.Type = Type;
    compIn.DirTagIn = DirTagIn;
    compIn.DirTagOut = DirTagOut;

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }

    status = PktMon.ProviderDispatch->ComponentRegister(
        PktMon.ProviderContext,
        &compIn,
        &CompContext->CompHandle);

    if (status == STATUS_SUCCESS)
    {
        CompContext->CompType = Type;
        CompContext->PacketType = PacketType;

        InsertHeadList(&PktMonCompList, &CompContext->ListLink);
        PktMonCompCount++;
    }

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:

    if (mutexOwner)
    {
        KeReleaseMutex(&PktMonCompMutex, FALSE);
    }

    return status;
}

//
// Component registration with NDIS MediaType
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientComponentRegister(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PUNICODE_STRING Name,
    _In_ PUNICODE_STRING Description,
    _In_ PKTMON_COMPONENT_TYPE Type,
    _In_ NDIS_MEDIUM MediaType,
    _In_ PKTMON_DIRECTION DirTagIn,
    _In_ PKTMON_DIRECTION DirTagOut
    )
{
    return PktMonClientComponentRegisterEx(
                CompContext,
                Name,
                Description,
                Type,
                MediaTypeToPacketType(MediaType),
                DirTagIn,
                DirTagOut);
}


//
// Unregister network component
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PktMonClientComponentUnregister(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext
    )
{
    LIST_ENTRY* entry = NULL;
    PKTMON_EDGE_CONTEXT* edgeContext = NULL;

    PAGED_CODE();

    if (CompContext->CompHandle == NULL)
    {
        goto Cleanup;
    }

    KeWaitForSingleObject(&PktMonCompMutex, Executive, KernelMode, FALSE, NULL);

    if (CompContext->CompHandle != NULL)
    {
        if (ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
        {
            PktMon.ProviderDispatch->ComponentUnregister(
                PktMon.ProviderContext,
                CompContext->CompHandle);

            ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);
        }

        RemoveEntryList(&CompContext->ListLink);
        PktMonCompCount--;

        while (!IsListEmpty(&CompContext->EdgeList))
        {
            entry = RemoveTailList(&CompContext->EdgeList);
            edgeContext = CONTAINING_RECORD(entry, PKTMON_EDGE_CONTEXT, ListLink);
            RtlZeroMemory(edgeContext, sizeof(*edgeContext));
        }

        RtlZeroMemory(CompContext, sizeof(*CompContext));
    }

    KeReleaseMutex(&PktMonCompMutex, FALSE);

Cleanup:
    return;
}

//
// Set component property
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientSetCompProperty(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PKTMON_COMPONENT_PROPERTY_ID Id,
    _In_ PVOID Value,
    _In_ USHORT Size
    )
{
    NTSTATUS status;
    PKTMON_COMP_PROPERTY_IN propIn = { 0 };

    PAGED_CODE();

    propIn.Header.Size = sizeof(propIn);

    propIn.Id = Id;
    propIn.Value = Value;
    propIn.Size = Size;

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    status = PktMon.ProviderDispatch->SetCompProperty(
        PktMon.ProviderContext,
        CompContext->CompHandle,
        &propIn);

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:
    return status;
}

//
// Edge (boundary) registration with PacketType
//
__declspec(code_seg("PAGE"))
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientAddEdgeEx(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PUNICODE_STRING Name,
    _In_ PKTMON_DIRECTION DirTagIn,
    _In_ PKTMON_DIRECTION DirTagOut,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _Out_ PKTMON_EDGE_CONTEXT *EdgeContext
    )
{
    NTSTATUS status;
    PKTMON_EDGE_IN edgeIn = { 0 };
    HANDLE EdgeHandle = NULL;

    PAGED_CODE();

    RtlZeroMemory(EdgeContext, sizeof(*EdgeContext));

    edgeIn.Header.Size = sizeof(edgeIn);

    edgeIn.Name = Name;
    edgeIn.DirTagIn = DirTagIn;
    edgeIn.DirTagOut = DirTagOut;

    KeWaitForSingleObject(&PktMonCompMutex, Executive, KernelMode, FALSE, NULL);

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    status = PktMon.ProviderDispatch->EdgeAdd(
        PktMon.ProviderContext,
        CompContext->CompHandle,
        &edgeIn,
        &EdgeHandle);

    if (status == STATUS_SUCCESS)
    {
        InsertHeadList(&CompContext->EdgeList, &EdgeContext->ListLink);
        CompContext->EdgeCount++;

        EdgeContext->CompContext = CompContext;
        EdgeContext->EdgeHandle = EdgeHandle;
        EdgeContext->PacketType = PacketType;
    }

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:

    KeReleaseMutex(&PktMonCompMutex, FALSE);

    return status;
}

//
// Edge (boundary) registration with NDIS MediaType
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClientAddEdge(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PUNICODE_STRING Name,
    _In_ PKTMON_DIRECTION DirTagIn,
    _In_ PKTMON_DIRECTION DirTagOut,
    _In_ NDIS_MEDIUM MediaType,
    _Out_ PKTMON_EDGE_CONTEXT *EdgeContext
    )
{
    return PktMonClientAddEdgeEx(
                CompContext,
                Name,
                DirTagIn,
                DirTagOut,
                MediaTypeToPacketType(MediaType),
                EdgeContext);
}

//
// Log NBL with packet type
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientNblLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFO *PacketHeaderInfo,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction
    )
{
    PKTMON_PACKET_LOG_IN pktIn;
    PKTMON_COMPONENT_CONTEXT* CompContext;

    CompContext = EdgeContext->CompContext;

    if (!PktMon.Enabled || (CompContext == NULL) || !CompContext->FlowEnabled)
    {
        goto Cleanup;
    }

    if (NdisTestNblFlag(NetBufferList, NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET))
    {
        goto Cleanup;
    }

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        goto Cleanup;
    }

    pktIn.Header.Size = sizeof(pktIn);

    pktIn.Buffer = NetBufferList;
    pktIn.BufferType = UseOnlyFirstNbl ? PktMonBuffer_NblSingle : PktMonBuffer_NblChain;
    pktIn.PacketType = PacketType;
    pktIn.Direction = Direction;
    pktIn.PacketHeaderInfo = PacketHeaderInfo;

    PktMon.ProviderDispatch->PacketLog(
        PktMon.ProviderContext,
        EdgeContext->EdgeHandle,
        &pktIn,
        NULL); // Packet context

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:
    return;
}

//
// Log NBL
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientNblLogNdis(
    _In_ PKTMON_EDGE_CONTEXT *EdgeContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction
    )
{
    PKTMON_COMPONENT_CONTEXT *CompContext = EdgeContext->CompContext;

    if (!PktMon.Enabled || (CompContext == NULL) || !CompContext->FlowEnabled)
    {
        goto Cleanup;
    }

    PktMonClientNblLog(EdgeContext,
                       NetBufferList,
                       EdgeContext->PacketType,
                       NULL,
                       UseOnlyFirstNbl,
                       Direction);
Cleanup:
    return;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientNblDrop(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFO *PacketHeaderInfo,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode
    )
{
    PKTMON_PACKET_LOG_IN pktIn;
    PKTMON_DROP_REPORT_IN dropIn;

    if (!PktMon.Enabled || !CompContext->DropEnabled)
    {
        goto Cleanup;
    }

    if (NdisTestNblFlag(NetBufferList, NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET))
    {
        goto Cleanup;
    }

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        goto Cleanup;
    }

    pktIn.Header.Size = sizeof(pktIn);

    pktIn.Buffer = NetBufferList;
    pktIn.BufferType = UseOnlyFirstNbl ? PktMonBuffer_NblSingle : PktMonBuffer_NblChain;
    pktIn.PacketType = PacketType;
    pktIn.Direction = Direction;
    pktIn.PacketHeaderInfo = PacketHeaderInfo;

    dropIn.Header.Size = sizeof(dropIn);

    dropIn.DropReason = DropReason;
    dropIn.LocationCode = (ULONG)LocationCode;

    PktMon.ProviderDispatch->PacketDrop(
        PktMon.ProviderContext,
        CompContext->CompHandle,
        &pktIn,
        &dropIn,
        NULL); // Packet context

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:
    return;
}

//
// Log header drop with context
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientHeaderInfoDropWithContext(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_PACKET_HEADER_INFO* PacketHeaderInfo,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN* Context
    )
{
    PKTMON_PACKET_LOG_IN pktIn;
    PKTMON_DROP_REPORT_IN dropIn;

    if (!PktMon.Enabled || !CompContext->DropEnabled)
    {
        goto Cleanup;
    }

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        goto Cleanup;
    }

    pktIn.Header.Size = sizeof(pktIn);

    pktIn.Buffer = NULL;
    pktIn.BufferType = PktMonBuffer_None;
    pktIn.PacketType = PacketType;
    pktIn.Direction = Direction;
    pktIn.Flags = 0;
    pktIn.PacketHeaderInfo = PacketHeaderInfo;

    dropIn.Header.Size = sizeof(dropIn);

    dropIn.DropReason = DropReason;
    dropIn.LocationCode = (ULONG)LocationCode;

    PktMon.ProviderDispatch->PacketDrop(
        PktMon.ProviderContext,
        CompContext->CompHandle,
        &pktIn,
        &dropIn,
        Context);

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:
    return;
}

//
// Log header drop
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientHeaderInfoDrop(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_PACKET_HEADER_INFO* PacketHeaderInfo,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode
    )
{
    PktMonClientHeaderInfoDropWithContext(
        CompContext,
        PacketType,
        PacketHeaderInfo,
        Direction,
        DropReason,
        LocationCode,
        NULL); // context
}


//
// Log header info
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
PktMonClientHeaderInfoLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PKTMON_PACKET_HEADER_INFO* PacketHeaderInfo,
    _In_ PKTMON_DIRECTION Direction,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN* Context
    )
{
    PKTMON_PACKET_LOG_IN pktIn;
    PKTMON_COMPONENT_CONTEXT* CompContext;

    CompContext = EdgeContext->CompContext;

    if (!PktMon.Enabled || (CompContext == NULL) || !CompContext->FlowEnabled)
    {
        goto Cleanup;
    }

    if (!ExAcquireRundownProtectionCacheAware(PktMon.RundownRef))
    {
        goto Cleanup;
    }

    pktIn.Header.Size = sizeof(pktIn);

    pktIn.Buffer = NULL;
    pktIn.BufferType = PktMonBuffer_None;
    pktIn.PacketType = EdgeContext->PacketType;
    pktIn.Direction = Direction;
    pktIn.Flags = 0;
    pktIn.PacketHeaderInfo = PacketHeaderInfo;

    PktMon.ProviderDispatch->PacketLog(
        PktMon.ProviderContext,
        EdgeContext->EdgeHandle,
        &pktIn,
        Context);

    ExReleaseRundownProtectionCacheAware(PktMon.RundownRef);

Cleanup:
    return;
}

VOID
SockAddrToPktMonIpAddress(_Out_ PKTMON_IP_ADDRESS* PktmonIpAddress, _In_ SOCKADDR_INET* SockAddr)
{
    switch (SockAddr->si_family)
    {
    case AF_INET:
        memcpy(PktmonIpAddress->IPv4_bytes, &SockAddr->Ipv4.sin_addr, PKTMON_IPV4_ADDRESS_SIZE);
        break;
    case AF_INET6:
        memcpy(PktmonIpAddress->IPv6_bytes, &SockAddr->Ipv6.sin6_addr, PKTMON_IPV6_ADDRESS_SIZE);
        break;
    default:
        RtlZeroMemory(PktmonIpAddress, sizeof(*PktmonIpAddress));
        break;
    }
}

VOID
ToInternalPacketHeaderInfo(_Out_ PKTMON_PACKET_HEADER_INFO* InternalInfo, _In_ PKTMON_PACKET_HEADER_INFORMATION* PublicInfo)
{
    InternalInfo->AddrFamily = PublicInfo->SockAddrLocal.si_family;

    SockAddrToPktMonIpAddress(&InternalInfo->IpAddrLocal, &PublicInfo->SockAddrLocal);
    SockAddrToPktMonIpAddress(&InternalInfo->IpAddrRemote, &PublicInfo->SockAddrRemote);

    InternalInfo->IpProtocol = PublicInfo->IpProtocol;

    switch (PublicInfo->IpProtocol) {
        case IPPROTO_TCP:
            InternalInfo->Transport.Tcp.PortLocal = PublicInfo->Transport.Tcp.PortLocal;
            InternalInfo->Transport.Tcp.PortRemote = PublicInfo->Transport.Tcp.PortRemote;
            InternalInfo->Transport.Tcp.Flags = PublicInfo->Transport.Tcp.Flags;
            break;

        case IPPROTO_UDP:
            InternalInfo->Transport.Udp.PortLocal = PublicInfo->Transport.Udp.PortLocal;
            InternalInfo->Transport.Udp.PortRemote = PublicInfo->Transport.Udp.PortRemote;
            break;

        case IPPROTO_ICMP:
            InternalInfo->Transport.Icmp.Type = PublicInfo->Transport.Icmp.Type;
            InternalInfo->Transport.Icmp.Code = PublicInfo->Transport.Icmp.Type;
            break;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntInitialize(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER EnumComponents,
    _In_opt_ PKTMON_CLIENT_CLEANUP_HANDLER CleanupComponents,
    _In_opt_ PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent
    )
{
    if (NotifyComponent)
    {
        return PktMonClientInitializeEx(
            ModuleId,
            EnumComponents,
            CleanupComponents,
            NotifyComponent
        );
    }
    else
    {
        return PktMonClientInitializeNoSync(
            ModuleId,
            EnumComponents,
            CleanupComponents
        );
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NTAPI
PktMonClntUninitialize()
{
    PktMonClientUninitialize();
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PktMonClntComponentRegister(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PCUNICODE_STRING Name,
    _In_ PCUNICODE_STRING Description,
    _In_ PKTMON_COMPONENT_TYPE Type,
    _In_ PKTMON_PACKET_TYPE PacketType
    )
{
    // The internal static lib has two flavors of PktMonClientComponentRegister.
    // We want to call the newest one.
    // This shared lib only has one flavor, so no Ex in its name.
    return PktMonClientComponentRegisterEx(
        CompContext,
        (PUNICODE_STRING)Name,
        (PUNICODE_STRING)Description,
        Type,
        PacketType,
        PktMonDir_In,
        PktMonDir_Out
    );
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NTAPI
PktMonClntComponentUnregister(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext
    )
{
    PktMonClientComponentUnregister(CompContext);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntSetComponentProperty(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PKTMON_COMPONENT_PROPERTY* CompProperty
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID value = NULL;
    USHORT size = 0;
    size_t cbLength = 0;

    switch (CompProperty->Id) {
        case PktMonCompProp_IfIndex: // Fall through
        case PktMonCompProp_MiniportIfIndex: // Fall through
        case PktMonCompProp_LowerIfIndex: // Fall through
        case PktMonCompProp_VmsExtIfIndex: // Fall through
        case PktMonCompProp_LowestIfIndex: // Fall through
        case PktMonCompProp_NdisMedium: // Fall through
        case PktMonCompProp_IpIfIndex: // Fall through
        case PktMonCompProp_Vsid: // Fall through
        case PktMonCompProp_Vlan: // Fall through
        case PktMonCompProp_CompartmentId:
            value = &CompProperty->IfIndex;
            size = sizeof(ULONG);
            break;

        case PktMonCompProp_OptDataPath: // Fall through
        case PktMonCompProp_NdisObject: // Fall through
        case PktMonCompProp_EtherType:
            value = &CompProperty->OptDataPath;
            size = sizeof(USHORT);
            break;

        case PktMonCompProp_IfGuid:
            value = &CompProperty->IfGuid;
            size = sizeof(GUID);
            break;

        case PktMonCompProp_PhysAddress:
            value = CompProperty->MacAddress;
            size = PKTMON_MAC_ADDRESS_SIZE;
            break;

        case PktMonCompProp_VMSwitchName:
            status = RtlStringCbLengthA(
                CompProperty->VMSwitchName,
                PKTMON_MAX_PROPERTY_LENGTH_BYTES,
                &cbLength
                );
            if (status != STATUS_SUCCESS) {
                return status;
            }
            value = CompProperty->VMSwitchName;
            size = (USHORT)cbLength;
            break;

        case PktMonCompProp_IpAddress:
            switch (CompProperty->SockAddr.si_family) {
                case AF_INET:
                    value = &CompProperty->SockAddr.Ipv4.sin_addr;
                    size = sizeof(CompProperty->SockAddr.Ipv4.sin_addr);
                    break;

                case AF_INET6:
                    value = &CompProperty->SockAddr.Ipv6.sin6_addr;
                    size = sizeof(CompProperty->SockAddr.Ipv6.sin6_addr);
                    break;

                default:
                    // Invalid address family
                    return STATUS_INVALID_PARAMETER;
            }
            break;

        default:
            // Invalid property ID
            return STATUS_INVALID_PARAMETER;
    }

    status = PktMonClientSetCompProperty(
        CompContext,
        CompProperty->Id,
        value,
        size
    );

    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntAddEdge(
    _In_  PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_  PCUNICODE_STRING Name,
    _In_  PKTMON_PACKET_TYPE PacketType,
    _Out_ PKTMON_EDGE_CONTEXT *EdgeContext
    )
{
    // The internal static lib has two flavors of PktMonClientAddEdge.
    // We want to call the newest one.
    // This shared lib only has one flavor, so no Ex in its name.
    return PktMonClientAddEdgeEx(
        CompContext,
        (PUNICODE_STRING)Name,
        PktMonDir_In,
        PktMonDir_Out,
        PacketType,
        EdgeContext
    );
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntNblLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction
    )
{
    PKTMON_PACKET_HEADER_INFO PacketHeaderInfo;
    if (PacketHeaderInformation) {
        ToInternalPacketHeaderInfo(&PacketHeaderInfo, PacketHeaderInformation);
    }

    PktMonClientNblLog(
        EdgeContext,
        NetBufferList,
        PacketType,
        PacketHeaderInformation ? &PacketHeaderInfo : NULL,
        UseOnlyFirstNbl,
        Direction
    );
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntNblDrop(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode
    )
{
    PKTMON_PACKET_HEADER_INFO PacketHeaderInfo;
    if (PacketHeaderInformation) {
        ToInternalPacketHeaderInfo(&PacketHeaderInfo, PacketHeaderInformation);
    }

    PktMonClientNblDrop(
        CompContext,
        NetBufferList,
        PacketType,
        PacketHeaderInformation ? &PacketHeaderInfo : NULL,
        UseOnlyFirstNbl,
        Direction,
        DropReason,
        LocationCode
    );
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntHeaderInfoLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN *Context
    )
{
    PKTMON_PACKET_HEADER_INFO PacketHeaderInfo;
    ToInternalPacketHeaderInfo(&PacketHeaderInfo, PacketHeaderInformation);

    PktMonClientHeaderInfoLog(
        EdgeContext,
        &PacketHeaderInfo,
        Direction,
        Context
    );
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntHeaderInfoDrop(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN *Context
    )
{
    PKTMON_PACKET_HEADER_INFO PacketHeaderInfo;
    ToInternalPacketHeaderInfo(&PacketHeaderInfo, PacketHeaderInformation);

    // The internal static lib has two flavors of PktMonClientHeaderInfoDrop.
    // We want to call the newest one.
    // This shared lib only has one flavor, so no WithContext in its name.
    PktMonClientHeaderInfoDropWithContext(
        CompContext,
        PacketType,
        &PacketHeaderInfo,
        Direction,
        DropReason,
        LocationCode,
        Context
    );
}
