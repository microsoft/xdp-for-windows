//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "miniport.tmh"

NDIS_STRING RegRSS = NDIS_STRING_CONST("*RSS");
NDIS_STRING RegNumRssQueues = NDIS_STRING_CONST("*NumRssQueues");
NDIS_STRING RegRxQueueSizeExp = NDIS_STRING_CONST("RxQueueSizeExp");
NDIS_STRING RegMTU = NDIS_STRING_CONST("MTU");
NDIS_STRING RegTxRingSize = NDIS_STRING_CONST("TxRingSize");
NDIS_STRING RegTxXdpQosPct = NDIS_STRING_CONST("TxXdpQosPct");
NDIS_STRING RegNumRxBuffers = NDIS_STRING_CONST("NumRxBuffers");
NDIS_STRING RegRxBufferLength = NDIS_STRING_CONST("RxBufferLength");
NDIS_STRING RegRxDataLength = NDIS_STRING_CONST("RxDataLength");
NDIS_STRING RegRxPattern = NDIS_STRING_CONST("RxPattern");
NDIS_STRING RegRxPatternCopy = NDIS_STRING_CONST("RxPatternCopy");
NDIS_STRING RegPollProvider = NDIS_STRING_CONST("PollProvider");

PCSTR MpDriverFriendlyName = "XDPMP";
UCHAR MpMacAddressBase[MAC_ADDR_LEN] = {0x22, 0x22, 0x22, 0x22, 0x00, 0x00};

#define MIN_MTU 1500
#define DEFAULT_MTU (16 * 1024 * 1024)
#define MAX_MTU MAXUINT32

#define MIN_TX_RING_SIZE 1
#define DEFAULT_TX_RING_SIZE 256
#define MAX_TX_RING_SIZE 8192

#define MIN_TX_XDP_QOS_PCT 1
#define DEFAULT_TX_XDP_QOS_PCT 90
#define MAX_TX_XDP_QOS_PCT 99

#define MIN_NUM_RX_BUFFERS 1
#define DEFAULT_NUM_RX_BUFFERS 256
#define MAX_NUM_RX_BUFFERS 8192

#define MIN_RX_BUFFER_LENGTH 64
#define DEFAULT_RX_BUFFER_LENGTH 2048
#define MAX_RX_BUFFER_LENGTH 65536

#define MIN_RX_DATA_LENGTH 64
#define DEFAULT_RX_BUFFER_DATA_LENGTH 64
#define MAX_RX_DATA_LENGTH 65536

//
// The driver only supports the driver API version in the DDK or higher.
// Drivers can set lower values for backwards compatibility.
//
static
CONST XDP_VERSION XdpDriverApiVersion = {
    .Major = XDP_DRIVER_API_MAJOR_VER,
    .Minor = XDP_DRIVER_API_MINOR_VER,
    .Patch = XDP_DRIVER_API_PATCH_VER
};

//
// Define custom OIDs in the vendor-private range [0xFF00000, 0xFFFFFFFF].
//
#define OID_XDPMP_RATE_SIM 0xFF00000

GLOBAL_CONTEXT MpGlobalContext = {0};

NDIS_OID MpSupportedOidArray[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_SUPPORTED_GUIDS,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,

    OID_GEN_LINK_SPEED_EX,
    OID_GEN_MAX_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS_EX,
    OID_GEN_MEDIA_DUPLEX_STATE,
    OID_GEN_LINK_STATE,

    OID_GEN_STATISTICS,

    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,

    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,

    OID_OFFLOAD_ENCAPSULATION,

    OID_GEN_RECEIVE_SCALE_PARAMETERS,

    OID_XDPMP_RATE_SIM,

    OID_XDP_QUERY_CAPABILITIES,
};

static CONST NDIS_GUID MpCustomGuidArray[] = {
    {
        XdpMpRateSimGuid,
        OID_XDPMP_RATE_SIM,
        sizeof(XDPMP_RATE_SIM_WMI),
        fNDIS_GUID_TO_OID,
    },
};

MINIPORT_SUPPORTED_XDP_EXTENSIONS MpSupportedXdpExtensions = {0};

static CONST XDP_INTERFACE_DISPATCH MpXdpDispatch = {
    .Header             = {
        .Revision       = XDP_INTERFACE_DISPATCH_REVISION_1,
        .Size           = XDP_SIZEOF_INTERFACE_DISPATCH_REVISION_1
    },
    .CreateRxQueue      = MpXdpCreateRxQueue,
    .ActivateRxQueue    = MpXdpActivateRxQueue,
    .DeleteRxQueue      = MpXdpDeleteRxQueue,
    .CreateTxQueue      = MpXdpCreateTxQueue,
    .ActivateTxQueue    = MpXdpActivateTxQueue,
    .DeleteTxQueue      = MpXdpDeleteTxQueue,
};

NDIS_STATUS
MpSetInformationHandler(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ NDIS_OID_REQUEST *NdisRequest
    );

NDIS_STATUS
MpQueryInformationHandler(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ NDIS_OID_REQUEST *NdisRequest
    );

ADAPTER_CONTEXT *
MpGetAdapter(
    _In_ NDIS_HANDLE NdisMiniportHandle
    );

VOID
MpReturnAdapter(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

ULONG
MpNodeNumberFromProcNumber(
    _In_ PROCESSOR_NUMBER ProcNumber
    );

NDIS_STATUS
MpReadConfiguration(
   _Inout_ ADAPTER_CONTEXT *Adapter
   );

NDIS_STATUS
MpSetOffloadAttributes(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

MINIPORT_PAUSE MpPause;

NDIS_STATUS
MpSetOptions(
   _In_ NDIS_HANDLE NdisMiniportDriverHandle,
   _In_ NDIS_HANDLE MiniportDriverContext
   )
{
    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(NdisMiniportDriverHandle);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
StartHwDatapath(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        Status = MpInitializeRateSim(RssQueue, Adapter);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        Status = MpStartRss(Adapter, RssQueue);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        MpStartRateSim(RssQueue);

        //
        // Only one queue is active until RSS is activated.
        //
        RssQueue->HwActiveRx = (Index == 0);
    }

Exit:

    return Status;
}

VOID
StopHwDatapath(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    for (ULONG Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        RssQueue->HwActiveRx = FALSE;

        MpCleanupRateSim(RssQueue);
    }
}

NDIS_STATUS
MpInitialize(
   _Inout_ NDIS_HANDLE NdisMiniportHandle,
   _In_ NDIS_HANDLE MiniportDriverContext,
   _In_ NDIS_MINIPORT_INIT_PARAMETERS *InitParameters
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_RSS_PROCESSOR_INFO *RssProcessorInfo = NULL;
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES AdapterAttributes;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES *RegistrationAttributes;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GeneralAttributes;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps;
    GROUP_AFFINITY Affinity = {0};
    GROUP_AFFINITY OldAffinity;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    TraceEnter(TRACE_CONTROL, "NdisMiniportHandle=%p", NdisMiniportHandle);

    //
    // Affinitize XDPMP allocations to NUMA node 0 by default.
    //
    Affinity.Group = 0;
    Affinity.Mask = 1;
    KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

    Adapter = MpGetAdapter(NdisMiniportHandle);
    if (Adapter == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    TraceInfo(
        TRACE_CONTROL, "Adapter=%p allocated IfIndex=%u", Adapter, InitParameters->IfIndex);

    NdisZeroMemory(&AdapterAttributes, sizeof(NDIS_MINIPORT_ADAPTER_ATTRIBUTES));

    Status =
        XdpInitializeCapabilities(
            &Adapter->Capabilities, &XdpDriverApiVersion);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RegistrationAttributes = &AdapterAttributes.RegistrationAttributes;

    RegistrationAttributes->Header.Type =
        NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegistrationAttributes->Header.Revision =
        NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegistrationAttributes->Header.Size =
        sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegistrationAttributes->MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegistrationAttributes->AttributeFlags =
        NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
        NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK |
        NDIS_MINIPORT_ATTRIBUTES_NO_PAUSE_ON_SUSPEND;
    RegistrationAttributes->CheckForHangTimeInSeconds = 0;
    RegistrationAttributes->InterfaceType = NdisInterfacePNPBus;

    Status = NdisMSetMiniportAttributes(NdisMiniportHandle, &AdapterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p NdisMSetMiniportAttributes failed Status=%!STATUS!",
            NdisMiniportHandle, Status);
        goto Exit;
    }

    Status = MpReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    NdisMoveMemory(Adapter->MACAddress, MpMacAddressBase, MAC_ADDR_LEN);

    Adapter->IfIndex = InitParameters->IfIndex;

    NdisZeroMemory(
        &GeneralAttributes, sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES));
    GeneralAttributes.Header.Type =
        NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GeneralAttributes.Header.Revision =
        NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    GeneralAttributes.Header.Size =
        sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);
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
    NdisMoveMemory(
        GeneralAttributes.PermanentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    NdisMoveMemory(
        GeneralAttributes.CurrentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    GeneralAttributes.PhysicalMediumType = NdisPhysicalMediumUnspecified;

    if (Adapter->RssEnabled) {
        NdisZeroMemory(&RssCaps, sizeof(NDIS_RECEIVE_SCALE_CAPABILITIES));
        RssCaps.Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
        RssCaps.Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
        RssCaps.Header.Size = sizeof(RssCaps);
        RssCaps.NumberOfInterruptMessages = MAX_RSS_QUEUES;
        RssCaps.NumberOfReceiveQueues = Adapter->NumRssQueues;
        RssCaps.NumberOfIndirectionTableEntries = MAX_RSS_INDIR_COUNT;
        RssCaps.CapabilitiesFlags =
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
            NdisHashFunctionToeplitz;

        GeneralAttributes.RecvScaleCapabilities = &RssCaps;
    } else {
        GeneralAttributes.RecvScaleCapabilities = NULL;
    }

    GeneralAttributes.AccessType = NET_IF_ACCESS_BROADCAST;
    GeneralAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GeneralAttributes.ConnectionType =
        (NET_IF_CONNECTION_TYPE)IF_CONNECTION_DEDICATED;
    GeneralAttributes.IfType = (NET_IFTYPE)IF_TYPE_ETHERNET_CSMACD;
    GeneralAttributes.IfConnectorPresent = TRUE;
    GeneralAttributes.SupportedStatistics =
        NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;
    GeneralAttributes.SupportedOidList = MpSupportedOidArray;
    GeneralAttributes.SupportedOidListLength = sizeof(MpSupportedOidArray);

    Status =
        NdisMSetMiniportAttributes(
            NdisMiniportHandle,
            (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&GeneralAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p NdisMSetMiniportAttributes failed Status=%!STATUS!",
            NdisMiniportHandle, Status);
        goto Exit;
    }

    Status = MpSetOffloadAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Adapter->MdlSize = (UINT32)MmSizeOfMdl((VOID *)(PAGE_SIZE - 1), Adapter->RxBufferLength);
    Adapter->MdlSize = ALIGN_UP(Adapter->MdlSize, MEMORY_ALLOCATION_ALIGNMENT);
    if (Adapter->MdlSize > MAXUSHORT) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p Error: MdlSize too large MdlSize=%lu",
            NdisMiniportHandle, Adapter->MdlSize);
        Status = STATUS_INVALID_BUFFER_SIZE;
        goto Exit;
    }

    NET_BUFFER_LIST_POOL_PARAMETERS NetBufferListPoolParameters = { 0 };
    NetBufferListPoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    NetBufferListPoolParameters.Header.Revision =
        NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    NetBufferListPoolParameters.Header.Size =
        sizeof(NetBufferListPoolParameters);
    NetBufferListPoolParameters.fAllocateNetBuffer = TRUE;
    NetBufferListPoolParameters.PoolTag = POOLTAG_NBL;
    NetBufferListPoolParameters.ContextSize = (UINT16)Adapter->MdlSize;

    Adapter->RxNblPool =
        NdisAllocateNetBufferListPool(
            NdisMiniportHandle,
            &NetBufferListPoolParameters);
    if (Adapter->RxNblPool == NULL) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p NdisAllocateNetBufferListPool failed",
            NdisMiniportHandle);
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    SIZE_T Size = 0;
    Status =
        NdisGetRssProcessorInformation(
            NdisMiniportHandle, RssProcessorInfo, &Size);
    NT_FRE_ASSERT(Status == NDIS_STATUS_BUFFER_TOO_SHORT);
    RssProcessorInfo = ExAllocatePoolZero(NonPagedPoolNx, Size, POOLTAG_RSS);
    if (RssProcessorInfo == NULL) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p Failed to allocate RssProcessorInfo",
            NdisMiniportHandle);
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    Status =
        NdisGetRssProcessorInformation(
            NdisMiniportHandle, RssProcessorInfo, &Size);
    if (Status != NDIS_STATUS_SUCCESS) {
        TraceError(
            TRACE_CONTROL, "NdisMiniportHandle=%p NdisGetRssProcessorInformation failed Status=%!STATUS!",
            NdisMiniportHandle, Status);
        goto Exit;
    }

    Adapter->NumRssProcs = RssProcessorInfo->RssProcessorCount;

    Status = MpPopulateRssQueues(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = StartHwDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status =
        XdpRegisterInterface(
            Adapter->IfIndex, &Adapter->Capabilities,
            Adapter, &MpXdpDispatch, &Adapter->XdpRegistration);
    if (Status != NDIS_STATUS_SUCCESS) {
        //
        // Failure to register with XDP is not fatal, but it does mean that XDP
        // cannot be used on the interface.
        //
        TraceWarn(TRACE_CONTROL, "XdpRegisterInterface failed with %!STATUS! (XDP cannot be used on this interface)", Status);
        Status = NDIS_STATUS_SUCCESS;
    }

Exit:

    if (Status != NDIS_STATUS_SUCCESS && Adapter != NULL) {
        MpReturnAdapter(Adapter);
    }

    if (RssProcessorInfo != NULL) {
        ExFreePool(RssProcessorInfo);
    }

    KeRevertToUserGroupAffinityThread(&OldAffinity);

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

VOID
MpHalt(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_HALT_ACTION HaltAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    StopHwDatapath(Adapter);

    MpReturnAdapter(Adapter);

    TraceExitSuccess(TRACE_CONTROL);
}

VOID
MpShutdown(
   _In_ NDIS_HANDLE          MiniportAdapterContext,
   _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    StopHwDatapath(Adapter);

    TraceExitSuccess(TRACE_CONTROL);
}

VOID
MpPnPEventNotify(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NET_DEVICE_PNP_EVENT *NetDevicePnPEvent
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

NDIS_STATUS
MpRestart(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_RESTART_PARAMETERS *RestartParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    ExReInitializeRundownProtectionCacheAware(Adapter->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
MpPause(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_PAUSE_PARAMETERS *PauseParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    UNREFERENCED_PARAMETER(PauseParameters);

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    ExWaitForRundownProtectionReleaseCacheAware(Adapter->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

VOID
MpCancelSend(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *CancelId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

VOID
MpCancelRequest(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *RequestId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

NDIS_STATUS
MpOidRequest(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _Inout_ NDIS_OID_REQUEST *NdisRequest
   )
{
    switch (NdisRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return MpQueryInformationHandler(MiniportAdapterContext, NdisRequest);

        case NdisRequestSetInformation:
            return MpSetInformationHandler(MiniportAdapterContext, NdisRequest);
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

VOID
MpUnload(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    if (MpGlobalContext.NdisMiniportDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(MpGlobalContext.NdisMiniportDriverHandle);
        MpGlobalContext.NdisMiniportDriverHandle = NULL;
    }

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

    XdpInitializeExtensionInfo(
        &MpSupportedXdpExtensions.VirtualAddress,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
        XDP_EXTENSION_TYPE_BUFFER);

    XdpInitializeExtensionInfo(
        &MpSupportedXdpExtensions.RxAction,
        XDP_FRAME_EXTENSION_RX_ACTION_NAME,
        XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1,
        XDP_EXTENSION_TYPE_FRAME);

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
        goto Exit;
    }

    MChars.Flags = 0;
    MChars.SetOptionsHandler = MpSetOptions;
    MChars.InitializeHandlerEx = MpInitialize;
    MChars.HaltHandlerEx = MpHalt;
    MChars.UnloadHandler = MpUnload;
    MChars.PauseHandler = MpPause;
    MChars.RestartHandler = MpRestart;
    MChars.OidRequestHandler = MpOidRequest;
    MChars.DirectOidRequestHandler = MpOidRequest;
    MChars.SendNetBufferListsHandler = MpSendNetBufferLists;
    MChars.ReturnNetBufferListsHandler = MpReturnNetBufferLists;
    MChars.CancelSendHandler = MpCancelSend;
    MChars.DevicePnPEventNotifyHandler = MpPnPEventNotify;
    MChars.ShutdownHandlerEx = MpShutdown;
    MChars.CancelOidRequestHandler = MpCancelRequest;
    MChars.CancelDirectOidRequestHandler = MpCancelRequest;

    Status =
        NdisMRegisterMiniportDriver(
            DriverObject, RegistryPath, (NDIS_HANDLE *)NULL, &MChars,
            &MpGlobalContext.NdisMiniportDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CONTROL);

    if (Status != NDIS_STATUS_SUCCESS) {
        MpUnload(DriverObject);
    }

    return Status;
}

VOID
MpFreeAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    if (Adapter->NblRundown != NULL) {
        ExFreeCacheAwareRundownProtection(Adapter->NblRundown);
        Adapter->NblRundown = NULL;
    }

    TraceInfo(TRACE_CONTROL, "Adapter=%p freed", Adapter);
    ExFreePool(Adapter);
}

ADAPTER_CONTEXT *
MpAllocateAdapter(
   _In_ NDIS_HANDLE NdisMiniportHandle
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    BOOLEAN Success = FALSE;

    Adapter =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(ADAPTER_CONTEXT), POOLTAG_ADAPTER);
    if (Adapter == NULL) {
        goto Exit;
    }

    Adapter->NblRundown =
        ExAllocateCacheAwareRundownProtection(NonPagedPoolNx,POOLTAG_ADAPTER);
    if (Adapter->NblRundown == NULL) {
        goto Exit;
    }

    ExWaitForRundownProtectionReleaseCacheAware(Adapter->NblRundown);

    Adapter->MiniportHandle = NdisMiniportHandle;

    Success = TRUE;

Exit:

    if (!Success && (Adapter != NULL)) {
        MpFreeAdapter(Adapter);
        Adapter = NULL;
    }

    return Adapter;
}

ADAPTER_CONTEXT *
MpGetAdapter(
   _In_ NDIS_HANDLE NdisMiniportHandle
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&MpGlobalContext.Lock);

    Adapter = MpAllocateAdapter(NdisMiniportHandle);

    if (Adapter != NULL) {
        InsertTailList(&MpGlobalContext.AdapterList, &Adapter->AdapterListLink);
    }

    ExReleasePushLockExclusive(&MpGlobalContext.Lock);
    KeLeaveCriticalRegion();

    return Adapter;
}

VOID
MpReturnAdapter(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    if (Adapter->XdpRegistration != NULL) {
        XdpDeregisterInterface(Adapter->XdpRegistration);
    }

    MpDepopulateRssQueues(Adapter);

    if (Adapter->RxNblPool != NULL) {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&MpGlobalContext.Lock);

    Adapter->MiniportHandle = NULL;
    RemoveEntryList(&Adapter->AdapterListLink);

    ExReleasePushLockExclusive(&MpGlobalContext.Lock);
    KeLeaveCriticalRegion();

    MpFreeAdapter(Adapter);
}

ULONG
MpNodeNumberFromProcNumber(
    _In_ PROCESSOR_NUMBER ProcNumber
    )
{
    NTSTATUS status;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info = {0};
    ULONG Length;

    Length = sizeof(Info);
    status =
        KeQueryLogicalProcessorRelationship(
            &ProcNumber, RelationNumaNode, &Info, &Length);

    NT_FRE_ASSERT(NT_SUCCESS(status) && (Info.Relationship == RelationNumaNode));

    return (ULONG)Info.NumaNode.NodeNumber;
}

NDIS_STATUS
MpHexToBin(
    _In_ WCHAR Char,
    _Out_ UCHAR *Nibble
    )
{
    Char = towlower(Char);

    if (Char >= L'0' && Char <= L'9') {
        *Nibble = (UCHAR)(Char - L'0');
        return NDIS_STATUS_SUCCESS;
    }

    if (Char >= L'a' && Char <= L'f') {
        *Nibble = (UCHAR)(10 + Char - L'a');
        return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_INVALID_PARAMETER;
}

NDIS_STATUS
MpSetRxPattern(
    _Inout_ ADAPTER_CONTEXT *Adapter,
    _In_ CONST WCHAR *Pattern,
    _In_ UINT32 Length
    )
{
    NDIS_STATUS Status;

    //
    // Parse the pattern string as hexadecimal pairs.
    //

    ASSERT(Length % sizeof(*Pattern) == 0);
    Length /= sizeof(*Pattern);

    if (Length % 2 > 0) {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (Length > sizeof(Adapter->RxPattern) * 2) {
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    for (UINT32 Index = 0; Index < Length  / 2; Index++) {
        UCHAR Byte;

        Status = MpHexToBin(Pattern[Index * 2], &Byte);
        if (Status != NDIS_STATUS_SUCCESS) {
            return Status;
        }

        Adapter->RxPattern[Index] = Byte << 4;

        Status = MpHexToBin(Pattern[Index * 2 + 1], &Byte);
        if (Status != NDIS_STATUS_SUCCESS) {
            return Status;
        }

        Adapter->RxPattern[Index] |= Byte;
    }

    Adapter->RxPatternLength = Length / 2;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
MpReadConfiguration(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    NDIS_HANDLE ConfigHandle = NULL;
    NDIS_STATUS Status;
    NDIS_CONFIGURATION_OBJECT ConfigObject;
    NDIS_CONFIGURATION_PARAMETER *ConfigParam;

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = Adapter->MiniportHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject, &ConfigHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Adapter->RssEnabled = 0;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRSS, &Adapter->RssEnabled);

    Adapter->NumRssQueues = 4;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegNumRssQueues, &Adapter->NumRssQueues);
    if (Adapter->NumRssQueues == 0 || Adapter->NumRssQueues > MAX_RSS_QUEUES) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->RxQSizeExp = 9;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRxQueueSizeExp, &Adapter->RxQSizeExp);

    Adapter->MtuSize = DEFAULT_MTU;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegMTU, &Adapter->MtuSize);
    if (Adapter->MtuSize < MIN_MTU || Adapter->MtuSize > MAX_MTU) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->TxRingSize = DEFAULT_TX_RING_SIZE;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegTxRingSize, &Adapter->TxRingSize);
    if (Adapter->TxRingSize < MIN_TX_RING_SIZE ||
        Adapter->TxRingSize > MAX_TX_RING_SIZE ||
        !RTL_IS_POWER_OF_TWO(Adapter->TxRingSize)) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->TxXdpQosPct = DEFAULT_TX_XDP_QOS_PCT;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegTxXdpQosPct, &Adapter->TxXdpQosPct);
    if (Adapter->TxXdpQosPct < MIN_TX_XDP_QOS_PCT ||
        Adapter->TxXdpQosPct > MAX_TX_XDP_QOS_PCT) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->NumRxBuffers = DEFAULT_NUM_RX_BUFFERS;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegNumRxBuffers, &Adapter->NumRxBuffers);
    if (Adapter->NumRxBuffers < MIN_NUM_RX_BUFFERS ||
        Adapter->NumRxBuffers > MAX_NUM_RX_BUFFERS ||
        !RTL_IS_POWER_OF_TWO(Adapter->NumRxBuffers)) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->RxBufferLength = DEFAULT_RX_BUFFER_LENGTH;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRxBufferLength, &Adapter->RxBufferLength);
    if (Adapter->RxBufferLength < MIN_RX_BUFFER_LENGTH ||
        Adapter->RxBufferLength > MAX_RX_BUFFER_LENGTH ||
        !RTL_IS_POWER_OF_TWO(Adapter->RxBufferLength)) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->RxDataLength = DEFAULT_RX_BUFFER_DATA_LENGTH;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRxDataLength, &Adapter->RxDataLength);
    if (Adapter->RxDataLength < MIN_RX_DATA_LENGTH ||
        Adapter->RxDataLength > MAX_RX_DATA_LENGTH ||
        Adapter->RxDataLength > Adapter->RxBufferLength) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    NdisReadConfiguration(&Status, &ConfigParam, ConfigHandle, &RegRxPattern, NdisParameterString);
    if (Status == NDIS_STATUS_SUCCESS) {
        if (ConfigParam->ParameterType != NdisParameterString) {
            Status = NDIS_STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        Status =
            MpSetRxPattern(
                Adapter, ConfigParam->ParameterData.StringData.Buffer,
                ConfigParam->ParameterData.StringData.Length);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }
    }

    Adapter->RxPatternCopy = 0;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegRxPatternCopy, &Adapter->RxPatternCopy);
    Adapter->RxPatternCopy = !!Adapter->RxPatternCopy;

    Adapter->RateSim.IntervalUs = 1000;             // 1ms
    Adapter->RateSim.RxFramesPerInterval = 1000;    // 1Mpps
    Adapter->RateSim.TxFramesPerInterval = 1000;    // 1Mpps

    Adapter->PollProvider = PollProviderNdis;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, RegPollProvider, &Adapter->PollProvider);
    if (Adapter->PollProvider < 0 ||
        Adapter->PollProvider >= PollProviderMax) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Adapter->CurrentLookAhead = 0;
    Adapter->CurrentPacketFilter = 0;

    Status = NDIS_STATUS_SUCCESS;

Exit:

    if (ConfigHandle != NULL) {
        NdisCloseConfiguration(ConfigHandle);
    }

    return Status;
}

NDIS_STATUS
MpSetOffloadAttributes(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES OffloadAttributes = {0};
    NDIS_OFFLOAD Offload = {0};

    Offload.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    Offload.Header.Revision = NDIS_OFFLOAD_REVISION_7;
    Offload.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_7;

    Offload.Checksum.IPv4Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    Offload.Checksum.IPv4Transmit.IpOptionsSupported = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv4Transmit.TcpOptionsSupported = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv4Transmit.TcpChecksum = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv4Transmit.UdpChecksum = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv4Transmit.IpChecksum = NDIS_OFFLOAD_SET_ON;

    Offload.Checksum.IPv6Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    Offload.Checksum.IPv6Transmit.IpExtensionHeadersSupported = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv6Transmit.TcpOptionsSupported = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv6Transmit.TcpChecksum = NDIS_OFFLOAD_SET_ON;
    Offload.Checksum.IPv6Transmit.UdpChecksum = NDIS_OFFLOAD_SET_ON;

    OffloadAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
    OffloadAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
    OffloadAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES);

    OffloadAttributes.DefaultOffloadConfiguration = &Offload;
    OffloadAttributes.HardwareOffloadCapabilities = &Offload;

    return
        NdisMSetMiniportAttributes(
            Adapter->MiniportHandle,
            (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&OffloadAttributes);
}

NDIS_STATUS
MpQueryInformationHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _Inout_ NDIS_OID_REQUEST *NdisRequest
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NDIS_STATUS Status;
    NDIS_OID Oid = NdisRequest->DATA.QUERY_INFORMATION.Oid;
    VOID *InformationBuffer = NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG InformationBufferLength = NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
    UINT *BytesWritten = &NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
    UINT *BytesNeeded = &NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
    BOOLEAN DoCopy = TRUE;
    ULONG DataLength = sizeof(ULONG);
    ULONG Data = 0;
    VOID *DataPointer = &Data;
    NDIS_LINK_SPEED LinkSpeed;
    NDIS_LINK_STATE LinkState;
    NDIS_STATISTICS_INFO StatisticsInfo;

    *BytesWritten = 0;

    Status = NDIS_STATUS_SUCCESS;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            DoCopy = FALSE;
            DataLength = sizeof(MpSupportedOidArray);
            if (InformationBufferLength < DataLength)
            {
                *BytesNeeded = DataLength;
                Status = NDIS_STATUS_BUFFER_TOO_SHORT;
                break;
            }
            NdisMoveMemory(InformationBuffer,
                            MpSupportedOidArray,
                            sizeof(MpSupportedOidArray));

            *BytesWritten = DataLength;
            break;

        case OID_GEN_SUPPORTED_GUIDS:
            DoCopy = FALSE;
            DataLength = sizeof(MpCustomGuidArray);

            if (InformationBufferLength < DataLength) {
                *BytesNeeded = DataLength;
                Status = NDIS_STATUS_BUFFER_TOO_SHORT;
                break;
            }

            NdisMoveMemory(InformationBuffer, MpCustomGuidArray, DataLength);

            *BytesWritten = DataLength;
            break;

        case OID_GEN_HARDWARE_STATUS:
            Data = NdisHardwareStatusReady;
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            DataPointer = &MpGlobalContext.Medium;
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
            DataPointer = &Adapter->MtuSize;
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            DataPointer = &Adapter->CurrentLookAhead;
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            DataPointer = &Adapter->MtuSize;
            break;

        case OID_GEN_LINK_SPEED:
            DataPointer = &MpGlobalContext.LinkSpeed;
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data = Adapter->MtuSize + ETH_HDR_LEN;
            break;

        case OID_GEN_VENDOR_ID:
            Data = 0x00FFFFFF;
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            DataLength = (ULONG)(sizeof(VOID*));
            DataPointer = (VOID *)MpDriverFriendlyName;
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            DataPointer = &Adapter->CurrentPacketFilter;
            break;

        case OID_GEN_MAC_OPTIONS:
            Data  = (ULONG)(NDIS_MAC_OPTION_NO_LOOPBACK |
                         NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND);
            break;

        case OID_GEN_LINK_STATE:
            LinkState.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
            LinkState.RcvLinkSpeed  = MpGlobalContext.RecvLinkSpeed;
            LinkState.MediaConnectState = MediaConnectStateConnected;
            DataLength = sizeof(NDIS_LINK_STATE);
            DataPointer = &LinkState;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data = NdisMediaStateConnected;
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            DataLength  = MAC_ADDR_LEN;
            DataPointer = Adapter->MACAddress;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            DataLength = MAC_ADDR_LEN;
            DataPointer = Adapter->MACAddress;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data = MAX_MULTICAST_ADDRESSES;
            break;

        case OID_802_3_MULTICAST_LIST:
            DataLength = Adapter->NumMulticastAddresses * MAC_ADDR_LEN;
            if (MpGlobalContext.Medium != NdisMedium802_3)
            {
                Status = NDIS_STATUS_INVALID_OID;
                break;
            }
            else if ((InformationBufferLength % ETH_LENGTH_OF_ADDRESS) != 0)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            DataPointer = Adapter->MulticastAddressList;
            break;

        case OID_PNP_QUERY_POWER:
            break;

        case OID_GEN_LINK_SPEED_EX:
            LinkSpeed.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
            LinkSpeed.RcvLinkSpeed  = MpGlobalContext.RecvLinkSpeed;
            DataLength = sizeof(NDIS_LINK_SPEED);
            DataPointer = &LinkSpeed;
            break;

        case OID_GEN_MAX_LINK_SPEED:
            LinkSpeed.XmitLinkSpeed = MpGlobalContext.MaxXmitLinkSpeed;
            LinkSpeed.RcvLinkSpeed  = MpGlobalContext.MaxRecvLinkSpeed;
            DataLength = sizeof(NDIS_LINK_SPEED);
            DataPointer = &LinkSpeed;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS_EX:
            Data = MediaConnectStateConnected;
            break;

        case OID_GEN_MEDIA_DUPLEX_STATE:
            Data = MediaDuplexStateFull;
            break;

        case OID_GEN_STATISTICS:
            RtlZeroMemory(&StatisticsInfo, sizeof(StatisticsInfo));
            StatisticsInfo.Header.Revision = NDIS_OBJECT_REVISION_1;
            StatisticsInfo.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            StatisticsInfo.Header.Size = sizeof(NDIS_STATISTICS_INFO);
            StatisticsInfo.SupportedStatistics =
                            NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                            NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS |
                            NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                            NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_XMIT;

            for (UINT32 Index = 0; Index < Adapter->NumRssQueues; Index++) {
                CONST ADAPTER_QUEUE *Queue = &Adapter->RssQueues[Index];

                StatisticsInfo.ifHCInUcastPkts += Queue->Rq.Stats.RxFrames;
                StatisticsInfo.ifHCInOctets += Queue->Rq.Stats.RxBytes;
                StatisticsInfo.ifInDiscards += Queue->Rq.Stats.RxDrops;
                StatisticsInfo.ifHCOutUcastPkts += Queue->Tq.Stats.TxFrames;
                StatisticsInfo.ifHCOutOctets += Queue->Tq.Stats.TxBytes;
                StatisticsInfo.ifOutDiscards += Queue->Tq.Stats.TxDrops;
            }

            DataPointer = &StatisticsInfo;
            DataLength = sizeof(NDIS_STATISTICS_INFO);
            break;

        case OID_XDPMP_RATE_SIM:
            DataPointer = &Adapter->RateSim;
            DataLength = sizeof(Adapter->RateSim);
            break;

        case OID_XDP_QUERY_CAPABILITIES:
            DataPointer = &Adapter->Capabilities;
            DataLength = sizeof(Adapter->Capabilities);
            break;

        default:
            DoCopy = FALSE;
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (DoCopy)
    {
        if (InformationBufferLength < DataLength)
        {
            *BytesNeeded = DataLength;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, DataPointer, DataLength);
            *BytesWritten = DataLength;
        }
    }

    return Status;
}

NDIS_STATUS
MpSetInformationHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _Inout_ NDIS_OID_REQUEST *NdisRequest
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NDIS_STATUS Status;

    NDIS_OID Oid = NdisRequest->DATA.SET_INFORMATION.Oid;
    VOID *InformationBuffer = NdisRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG InformationBufferLength = NdisRequest->DATA.SET_INFORMATION.InformationBufferLength;
    UINT *BytesRead = &NdisRequest->DATA.SET_INFORMATION.BytesRead;

    Status = NDIS_STATUS_SUCCESS;

    switch (Oid)
    {
        case OID_OFFLOAD_ENCAPSULATION:

            if (InformationBufferLength < NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            NDIS_OFFLOAD_ENCAPSULATION *offloadEncapsulation;
            offloadEncapsulation = (NDIS_OFFLOAD_ENCAPSULATION *)InformationBuffer;

            if ((offloadEncapsulation->Header.Type != NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION) ||
                (offloadEncapsulation->Header.Revision < NDIS_OFFLOAD_ENCAPSULATION_REVISION_1) ||
                (offloadEncapsulation->Header.Size < NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1))
            {
                Status = NDIS_STATUS_INVALID_PARAMETER;
                break;
            }

            if (offloadEncapsulation->IPv6.Enabled == NDIS_OFFLOAD_SET_ON)
            {
                if (offloadEncapsulation->IPv6.EncapsulationType != NDIS_ENCAPSULATION_IEEE_802_3 ||
                    offloadEncapsulation->IPv6.HeaderSize != ETH_HDR_LEN)
                {
                    Status = NDIS_STATUS_NOT_SUPPORTED;
                    break;
                }
            }

            if (offloadEncapsulation->IPv4.Enabled == NDIS_OFFLOAD_SET_ON)
            {
                if (offloadEncapsulation->IPv4.EncapsulationType != NDIS_ENCAPSULATION_IEEE_802_3 ||
                    offloadEncapsulation->IPv4.HeaderSize != ETH_HDR_LEN)
                {
                    Status = NDIS_STATUS_NOT_SUPPORTED;
                    break;
                }
            }

            break;

        case OID_GEN_CURRENT_PACKET_FILTER:

            if (InformationBufferLength < sizeof(ULONG))
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            ULONG PacketFilter = *(UNALIGNED ULONG *)InformationBuffer;
            Adapter->CurrentPacketFilter = PacketFilter;
            *BytesRead = InformationBufferLength;

            break;

        case OID_GEN_CURRENT_LOOKAHEAD:

            if (InformationBufferLength < sizeof(ULONG))
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            ULONG CurrentLookahead = *(UNALIGNED ULONG *)InformationBuffer;
            if (CurrentLookahead > Adapter->MtuSize)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else if (CurrentLookahead >= Adapter->CurrentLookAhead)
            {
                Adapter->CurrentLookAhead = CurrentLookahead;
                *BytesRead = sizeof(ULONG);
            }

            break;

        case OID_802_3_MULTICAST_LIST:

            if (MpGlobalContext.Medium != NdisMedium802_3)
            {
                Status = NDIS_STATUS_INVALID_OID;
                break;
            }

            if ((InformationBufferLength % ETH_LENGTH_OF_ADDRESS) != 0 ||
                InformationBufferLength  > (MAX_MULTICAST_ADDRESSES * MAC_ADDR_LEN))
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            NdisMoveMemory(Adapter->MulticastAddressList,
                        InformationBuffer,
                        InformationBufferLength);
            Adapter->NumMulticastAddresses = InformationBufferLength / MAC_ADDR_LEN;

            break;

        case OID_PNP_SET_POWER:

            if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);

            break;

        case OID_GEN_RECEIVE_SCALE_PARAMETERS:

            if (InformationBufferLength < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            MpSetRss(Adapter, InformationBuffer, InformationBufferLength);
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_XDPMP_RATE_SIM:
        {
            Status = MpUpdateRateSim(Adapter, InformationBuffer);
            break;
        }

        default:

            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;

    }

    return Status;
}
