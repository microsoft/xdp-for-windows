//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "miniport.tmh"

NDIS_STRING RegRSS = NDIS_STRING_CONST("*RSS");
NDIS_STRING RegNumRssQueues = NDIS_STRING_CONST("*NumRssQueues");
NDIS_STRING RegRxQueueSizeExp = NDIS_STRING_CONST("RxQueueSizeExp");

UCHAR MpMacAddressBase[MAC_ADDR_LEN] = {0x22, 0x22, 0x22, 0x22, 0x00, 0x00};

GLOBAL_CONTEXT MpGlobalContext = {0};

static
VOID
MpCleanupAdapter(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    MpCleanupRssQueues(Adapter);

    if (Adapter->Native != NULL) {
        NativeAdapterCleanup(Adapter->Native);
    }

    if (Adapter->Generic != NULL) {
        GenericAdapterCleanup(Adapter->Generic);
    }

    MpIoctlDereference();
    TraceInfo(TRACE_CONTROL, "Adapter=%p freed", Adapter);
    ExFreePool(Adapter);
}

static
ADAPTER_CONTEXT *
MpCreateAdapter(
   _In_ NDIS_HANDLE NdisMiniportHandle,
   _In_ UINT32 IfIndex
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    NTSTATUS Status;

    if (MpIoctlReference() != NDIS_STATUS_SUCCESS) {
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(ADAPTER_CONTEXT), POOLTAG_ADAPTER);
    if (Adapter == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Adapter->MiniportHandle = NdisMiniportHandle;
    Adapter->IfIndex = IfIndex;
    Adapter->ReferenceCount = 1;
    InitializeListHead(&Adapter->AdapterListLink);
    ExInitializePushLock(&Adapter->Lock);

    Adapter->Generic = GenericAdapterCreate(Adapter);
    if (Adapter->Generic == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Adapter->Native = NativeAdapterCreate(Adapter);
    if (Adapter->Native == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Adapter != NULL) {
            MpCleanupAdapter(Adapter);
            Adapter = NULL;
        }
    }

    return Adapter;
}

static
VOID
MpReferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    FRE_ASSERT(InterlockedIncrementAcquire64(&Adapter->ReferenceCount) > 1);
}

VOID
MpDereferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    INT64 NewCount = InterlockedDecrementRelease64(&Adapter->ReferenceCount);
    FRE_ASSERT(NewCount >= 0);
    if (NewCount == 0) {
        MpCleanupAdapter(Adapter);
    }
}

ADAPTER_CONTEXT *
MpFindAdapter(
    _In_ UINT32 IfIndex
    )
{
    LIST_ENTRY *Entry;
    ADAPTER_CONTEXT *Adapter = NULL;

    RtlAcquirePushLockShared(&MpGlobalContext.Lock);

    Entry = MpGlobalContext.AdapterList.Flink;
    while (Entry != &MpGlobalContext.AdapterList) {
        ADAPTER_CONTEXT *Candidate = CONTAINING_RECORD(Entry, ADAPTER_CONTEXT, AdapterListLink);
        Entry = Entry->Flink;

        if (Candidate->IfIndex == IfIndex) {
            Adapter = Candidate;
            MpReferenceAdapter(Adapter);
            break;
        }
    }

    RtlReleasePushLockShared(&MpGlobalContext.Lock);

    return Adapter;
}

static
NDIS_STATUS
MpReadConfiguration(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    NDIS_HANDLE ConfigHandle = NULL;
    NDIS_STATUS Status;
    NDIS_CONFIGURATION_OBJECT ConfigObject;

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = Adapter->MiniportHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject, &ConfigHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Adapter->MtuSize = FNMP_DEFAULT_MTU - ETH_HDR_LEN;
    Adapter->CurrentLookAhead = 0;
    Adapter->CurrentPacketFilter = 0;

    Adapter->RssEnabled = 0;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRSS, &Adapter->RssEnabled);

    Adapter->NumRssQueues = FNMP_DEFAULT_RSS_QUEUES;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegNumRssQueues, &Adapter->NumRssQueues);
    if (Adapter->NumRssQueues == 0 || Adapter->NumRssQueues > MAX_RSS_QUEUES) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = NDIS_STATUS_SUCCESS;

Exit:

    if (ConfigHandle != NULL) {
        NdisCloseConfiguration(ConfigHandle);
    }

    return Status;
}

static
NDIS_STATUS
MpSetOffloadAttributes(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES OffloadAttributes = {0};
    NDIS_OFFLOAD DefaultConfig = {0};

    DefaultConfig.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    DefaultConfig.Header.Revision = NDIS_OFFLOAD_REVISION_6;
    DefaultConfig.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_6;

    OffloadAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
    OffloadAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
    OffloadAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES);

    OffloadAttributes.DefaultOffloadConfiguration = &DefaultConfig;
    OffloadAttributes.HardwareOffloadCapabilities = &DefaultConfig;

    return
        NdisMSetMiniportAttributes(
            Adapter->MiniportHandle,
            (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&OffloadAttributes);
}

static
NDIS_STATUS
MiniportInitializeHandler(
   _Inout_ NDIS_HANDLE NdisMiniportHandle,
   _In_ NDIS_HANDLE MiniportDriverContext,
   _In_ NDIS_MINIPORT_INIT_PARAMETERS *InitParameters
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES AdapterAttributes;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES *RegAttributes;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GeneralAttributes;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    TraceEnter(TRACE_CONTROL, "NdisMiniportHandle=%p", NdisMiniportHandle);

    Adapter = MpCreateAdapter(NdisMiniportHandle, InitParameters->IfIndex);
    if (Adapter == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    TraceInfo(
        TRACE_CONTROL, "Adapter=%p allocated IfIndex=%u", Adapter, InitParameters->IfIndex);

    NdisMoveMemory(Adapter->MACAddress, MpMacAddressBase, MAC_ADDR_LEN);

    NdisZeroMemory(&AdapterAttributes, sizeof(NDIS_MINIPORT_ADAPTER_ATTRIBUTES));

    RegAttributes = &AdapterAttributes.RegistrationAttributes;
    RegAttributes->Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttributes->Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttributes->Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegAttributes->MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegAttributes->AttributeFlags =
        NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
        NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK |
        NDIS_MINIPORT_ATTRIBUTES_NO_PAUSE_ON_SUSPEND;
    RegAttributes->CheckForHangTimeInSeconds = 0;
    RegAttributes->InterfaceType = NdisInterfacePNPBus;

    Status = NdisMSetMiniportAttributes(NdisMiniportHandle, &AdapterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    NdisZeroMemory(&GeneralAttributes, sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES));
    GeneralAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    GeneralAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);
    GeneralAttributes.MediaType = MpGlobalContext.Medium;
    GeneralAttributes.MtuSize = Adapter->MtuSize;
    GeneralAttributes.MaxXmitLinkSpeed = MpGlobalContext.MaxXmitLinkSpeed;
    GeneralAttributes.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
    GeneralAttributes.MaxRcvLinkSpeed = MpGlobalContext.MaxRecvLinkSpeed;
    GeneralAttributes.RcvLinkSpeed = MpGlobalContext.RecvLinkSpeed;
    GeneralAttributes.MediaConnectState = MediaConnectStateConnected;
    GeneralAttributes.MediaDuplexState = MediaDuplexStateFull;
    GeneralAttributes.LookaheadSize = Adapter->MtuSize;
    GeneralAttributes.MacOptions = (ULONG)
        NDIS_MAC_OPTION_NO_LOOPBACK |
        NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
        NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
        NDIS_MAC_OPTION_FULL_DUPLEX |
        NDIS_MAC_OPTION_8021Q_VLAN |
        NDIS_MAC_OPTION_8021P_PRIORITY;
    GeneralAttributes.SupportedPacketFilters = MpGlobalContext.PacketFilter;
    GeneralAttributes.MaxMulticastListSize = MAX_MULTICAST_ADDRESSES;
    GeneralAttributes.MacAddressLength = MAC_ADDR_LEN;
    NdisMoveMemory(GeneralAttributes.PermanentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    NdisMoveMemory(GeneralAttributes.CurrentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    GeneralAttributes.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    GeneralAttributes.AccessType = NET_IF_ACCESS_BROADCAST;
    GeneralAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GeneralAttributes.ConnectionType = (NET_IF_CONNECTION_TYPE)IF_CONNECTION_DEDICATED;
    GeneralAttributes.IfType = (NET_IFTYPE)IF_TYPE_ETHERNET_CSMACD;
    GeneralAttributes.IfConnectorPresent = TRUE;
    GeneralAttributes.SupportedStatistics = NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;
    GeneralAttributes.SupportedOidList = (NDIS_OID *)MpSupportedOidArray;
    GeneralAttributes.SupportedOidListLength = MpSupportedOidArraySize;

    if (Adapter->RssEnabled) {
        NdisZeroMemory(&RssCaps, sizeof(NDIS_RECEIVE_SCALE_CAPABILITIES));
        RssCaps.Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
        RssCaps.Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
        RssCaps.Header.Size = sizeof(RssCaps);
        RssCaps.NumberOfInterruptMessages = MAX_RSS_QUEUES;
        RssCaps.NumberOfReceiveQueues = Adapter->NumRssQueues;
        RssCaps.NumberOfIndirectionTableEntries = FNMP_MAX_RSS_INDIR_COUNT;
        RssCaps.CapabilitiesFlags =
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
            NdisHashFunctionToeplitz;

        GeneralAttributes.RecvScaleCapabilities = &RssCaps;
    } else {
        GeneralAttributes.RecvScaleCapabilities = NULL;
    }

    NDIS_DECLARE_MINIPORT_ADAPTER_CONTEXT(ADAPTER_CONTEXT);
    Status =
        NdisMSetMiniportAttributes(
            NdisMiniportHandle, (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&GeneralAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpSetOffloadAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpCreateRssQueues(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&MpGlobalContext.Lock);
    InsertTailList(&MpGlobalContext.AdapterList, &Adapter->AdapterListLink);
    RtlReleasePushLockExclusive(&MpGlobalContext.Lock);

Exit:

    if (Status != NDIS_STATUS_SUCCESS && Adapter != NULL) {
        MpDereferenceAdapter(Adapter);
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
VOID
MiniportHaltHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_HALT_ACTION HaltAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(HaltAction);

    RtlAcquirePushLockExclusive(&MpGlobalContext.Lock);
    Adapter->MiniportHandle = NULL;
    if (!IsListEmpty(&Adapter->AdapterListLink)) {
        RemoveEntryList(&Adapter->AdapterListLink);
    }
    RtlReleasePushLockExclusive(&MpGlobalContext.Lock);

    MpDereferenceAdapter(Adapter);

    TraceExitSuccess(TRACE_CONTROL);
}

static
VOID
MiniportShutdownHandler(
   _In_ NDIS_HANDLE          MiniportAdapterContext,
   _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

static
VOID
MiniportPnPEventNotifyHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NET_DEVICE_PNP_EVENT *NetDevicePnPEvent
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
MiniportUnloadHandler(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    if (MpGlobalContext.NdisMiniportDriverHandle != NULL) {
        NdisMDeregisterMiniportDriver(MpGlobalContext.NdisMiniportDriverHandle);
        MpGlobalContext.NdisMiniportDriverHandle = NULL;
    }

    MpNativeCleanup();
    MpIoctlCleanup();

    TraceExitSuccess(TRACE_CONTROL);

    WPP_CLEANUP(DriverObject);
}

_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DriverEntry(
    _In_ struct _DRIVER_OBJECT *DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MChars;

#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    ExInitializePushLock(&MpGlobalContext.Lock);
    InitializeListHead(&MpGlobalContext.AdapterList);

    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    MpGlobalContext.NdisVersion = NdisGetVersion();
    MpGlobalContext.Medium = NdisMedium802_3;
    MpGlobalContext.LinkSpeed = MAXULONG;
    MpGlobalContext.MaxRecvLinkSpeed = 100000000000;
    MpGlobalContext.RecvLinkSpeed = 100000000000;
    MpGlobalContext.MaxXmitLinkSpeed = 100000000000;
    MpGlobalContext.XmitLinkSpeed = 100000000000;
    MpGlobalContext.PacketFilter =
        NDIS_PACKET_TYPE_DIRECTED  |
        NDIS_PACKET_TYPE_MULTICAST |
        NDIS_PACKET_TYPE_BROADCAST |
        NDIS_PACKET_TYPE_PROMISCUOUS |
        NDIS_PACKET_TYPE_ALL_MULTICAST;

    NdisZeroMemory(&MChars, sizeof(MChars));
    MChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    MChars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    MChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    MChars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    MChars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    MChars.Flags = 0;

    if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_685) {
        //
        // Fe (Iron) / 20348 / Windows Server 2022 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 85;
    } else if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_682) {
        //
        // RS5 / 17763 / Windows 1809 / Windows Server 2019 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 82;
    } else if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_660) {
        //
        // RS1 / 14393 / Windows 1607 / Windows Server 2016 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 60;
    } else {
        Status = NDIS_STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    MChars.InitializeHandlerEx = MiniportInitializeHandler;
    MChars.HaltHandlerEx = MiniportHaltHandler;
    MChars.UnloadHandler = MiniportUnloadHandler;
    MChars.PauseHandler = MiniportPauseHandler;
    MChars.RestartHandler = MiniportRestartHandler;
    MChars.OidRequestHandler = MiniportRequestHandler;
    MChars.DirectOidRequestHandler = MiniportRequestHandler;
    MChars.SendNetBufferListsHandler = MpSendNetBufferLists;
    MChars.ReturnNetBufferListsHandler = MpReturnNetBufferLists;
    MChars.CancelSendHandler = MiniportCancelSendHandler;
    MChars.DevicePnPEventNotifyHandler = MiniportPnPEventNotifyHandler;
    MChars.ShutdownHandlerEx = MiniportShutdownHandler;
    MChars.CancelOidRequestHandler = MiniportCancelRequestHandler;
    MChars.CancelDirectOidRequestHandler = MiniportCancelRequestHandler;

    Status =
        NdisMRegisterMiniportDriver(
            DriverObject, RegistryPath, (NDIS_HANDLE *)NULL, &MChars,
            &MpGlobalContext.NdisMiniportDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Cleanup;
    }

    Status = MpIoctlStart();
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Cleanup;
    }

    Status = MpNativeStart();
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Cleanup;
    }

Cleanup:

    TraceExitStatus(TRACE_CONTROL);

    if (Status != NDIS_STATUS_SUCCESS) {
        MiniportUnloadHandler(DriverObject);
    }

    return Status;
}
