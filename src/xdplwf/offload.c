//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offload.tmh"

typedef enum {
    //
    // Control path below the XDP LWF.
    //
    XdpOffloadEdgeLower,
    //
    // Control path above the XDP LWF.
    //
    XdpOffloadEdgeUpper,
} XDP_LWF_OFFLOAD_EDGE;

//
// Context backing the interface offload handle.
//
typedef struct _XDP_LWF_INTERFACE_OFFLOAD_CONTEXT {
    LIST_ENTRY Link;
    XDP_LWF_FILTER *Filter;
    XDP_LWF_OFFLOAD_EDGE Edge;
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS Settings;
    BOOLEAN IsInvalid;
} XDP_LWF_INTERFACE_OFFLOAD_CONTEXT;

#define RSS_HASH_SECRET_KEY_MAX_SIZE NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2

static
NTSTATUS
XdpLwfOpenInterfaceOffloadHandle(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Out_ VOID **InterfaceOffloadHandle
    );

static
VOID
XdpLwfCloseInterfaceOffloadHandle(
    _In_ VOID *InterfaceOffloadHandle
    );

static
NTSTATUS
XdpLwfGetInterfaceOffloadCapabilities(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadCapabilities,
    _Inout_ UINT32 *OffloadCapabilitiesSize
    );

static
NTSTATUS
XdpLwfGetInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadParams,
    _Inout_ UINT32 *OffloadParamsSize
    );

static
NTSTATUS
XdpLwfSetInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    );

static
NTSTATUS
XdpLwfReferenceInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType
    );

CONST XDP_OFFLOAD_DISPATCH XdpLwfOffloadDispatch = {
    .OpenInterfaceOffloadHandle = XdpLwfOpenInterfaceOffloadHandle,
    .GetInterfaceOffloadCapabilities = XdpLwfGetInterfaceOffloadCapabilities,
    .GetInterfaceOffload = XdpLwfGetInterfaceOffload,
    .SetInterfaceOffload = XdpLwfSetInterfaceOffload,
    .ReferenceInterfaceOffload = XdpLwfReferenceInterfaceOffload,
    .CloseInterfaceOffloadHandle = XdpLwfCloseInterfaceOffloadHandle,
};

static
USHORT
XdpToNdisRssFlags(
    _In_ UINT32 XdpRssFlags
    )
{
    USHORT NdisRssFlags = 0;

    if (!(XdpRssFlags & XDP_RSS_FLAG_SET_HASH_TYPE)) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED;
    }
    if (!(XdpRssFlags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY)) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED;
    }
    if (!(XdpRssFlags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE)) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED;
    }

    return NdisRssFlags;
}

static
USHORT
NdisToXdpRssFlags(
    _In_ UINT32 NdisRssFlags
    )
{
    USHORT XdpRssFlags = 0;

    if (!(NdisRssFlags & NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED)) {
        XdpRssFlags |= XDP_RSS_FLAG_SET_HASH_TYPE;
    }
    if (!(NdisRssFlags & NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED)) {
        XdpRssFlags |= XDP_RSS_FLAG_SET_HASH_SECRET_KEY;
    }
    if (!(NdisRssFlags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED)) {
        XdpRssFlags |= XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    }

    return XdpRssFlags;
}

static
ULONG
XdpToNdisRssHashType(
    _In_ ULONG XdpRssHashType
    )
{
    ULONG NdisRssHashType = 0;

    if (XdpRssHashType & XDP_RSS_HASH_TYPE_IPV4) {
        NdisRssHashType |= NDIS_HASH_IPV4;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_TCP_IPV4) {
        NdisRssHashType |= NDIS_HASH_TCP_IPV4;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_UDP_IPV4) {
        NdisRssHashType |= NDIS_HASH_UDP_IPV4;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_IPV6) {
        NdisRssHashType |= NDIS_HASH_IPV6;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_TCP_IPV6) {
        NdisRssHashType |= NDIS_HASH_TCP_IPV6;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_UDP_IPV6) {
        NdisRssHashType |= NDIS_HASH_UDP_IPV6;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_IPV6_EX) {
        NdisRssHashType |= NDIS_HASH_IPV6_EX;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_TCP_IPV6_EX) {
        NdisRssHashType |= NDIS_HASH_TCP_IPV6_EX;
    }
    if (XdpRssHashType & XDP_RSS_HASH_TYPE_UDP_IPV6_EX) {
        NdisRssHashType |= NDIS_HASH_UDP_IPV6_EX;
    }

    ASSERT(NdisRssHashType != 0);
    return NdisRssHashType;
}

static
ULONG
NdisToXdpRssHashType(
    _In_ ULONG NdisRssHashType
    )
{
    ULONG XdpRssHashType = 0;

    if (NdisRssHashType & NDIS_HASH_IPV4) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_IPV4;
    }
    if (NdisRssHashType & NDIS_HASH_TCP_IPV4) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_TCP_IPV4;
    }
    if (NdisRssHashType & NDIS_HASH_UDP_IPV4) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_UDP_IPV4;
    }
    if (NdisRssHashType & NDIS_HASH_IPV6) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_IPV6;
    }
    if (NdisRssHashType & NDIS_HASH_TCP_IPV6) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_TCP_IPV6;
    }
    if (NdisRssHashType & NDIS_HASH_UDP_IPV6) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_UDP_IPV6;
    }
    if (NdisRssHashType & NDIS_HASH_IPV6_EX) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_IPV6_EX;
    }
    if (NdisRssHashType & NDIS_HASH_TCP_IPV6_EX) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_TCP_IPV6_EX;
    }
    if (NdisRssHashType & NDIS_HASH_UDP_IPV6_EX) {
        XdpRssHashType |= XDP_RSS_HASH_TYPE_UDP_IPV6_EX;
    }

    ASSERT(XdpRssHashType != 0);
    return XdpRssHashType;
}

static
ULONG
NdisToXdpRssHashTypeCapabilities(
    _In_ NDIS_RSS_CAPS_FLAGS NdisCapabilitiesFlags
    )
{
    ULONG XdpRssHashTypeCapabilities = 0;

    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_TCP_IPV4;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV4;
    }
    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV4) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_UDP_IPV4;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV4;
    }
    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_TCP_IPV6;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV6;
    }
    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_UDP_IPV6;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV6;
    }
    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_TCP_IPV6_EX;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV6_EX;
    }
    if (NdisCapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6_EX) {
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_UDP_IPV6_EX;
        XdpRssHashTypeCapabilities |= XDP_RSS_HASH_TYPE_IPV6_EX;
    }

    return XdpRssHashTypeCapabilities;
}

static
VOID
InheritXdpRssParams(
    _Inout_ XDP_OFFLOAD_PARAMS_RSS *XdpRssParams,
    _In_opt_ CONST XDP_OFFLOAD_PARAMS_RSS *InheritedXdpRssParams
    )
{
    if (InheritedXdpRssParams != NULL) {
        ASSERT(InheritedXdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_TYPE);
        ASSERT(InheritedXdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY);
        ASSERT(InheritedXdpRssParams->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE);

        if (!(XdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_TYPE)) {
            XdpRssParams->HashType = InheritedXdpRssParams->HashType;
            XdpRssParams->Flags |= XDP_RSS_FLAG_SET_HASH_TYPE;
        }
        if (!(XdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY)) {
            XdpRssParams->HashSecretKeySize = InheritedXdpRssParams->HashSecretKeySize;
            RtlCopyMemory(
                &XdpRssParams->HashSecretKey, &InheritedXdpRssParams->HashSecretKey,
                InheritedXdpRssParams->HashSecretKeySize);
            XdpRssParams->Flags |= XDP_RSS_FLAG_SET_HASH_SECRET_KEY;
        }
        if (!(XdpRssParams->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE)) {
            XdpRssParams->IndirectionTableSize = InheritedXdpRssParams->IndirectionTableSize;
            RtlCopyMemory(
                &XdpRssParams->IndirectionTable, &InheritedXdpRssParams->IndirectionTable,
                InheritedXdpRssParams->IndirectionTableSize);
            XdpRssParams->Flags |= XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
        }
    } else {
        ASSERT(XdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_TYPE);
        ASSERT(XdpRssParams->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY);
        ASSERT(XdpRssParams->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE);
    }
}

static
NTSTATUS
CreateNdisRssParamsFromXdpRssParams(
    _In_ CONST XDP_OFFLOAD_PARAMS_RSS *XdpRssParams,
    _Out_ NDIS_RECEIVE_SCALE_PARAMETERS **NdisRssParamsOut,
    _Out_ UINT32 *NdisRssParamsLengthOut
    )
{
    NTSTATUS Status;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams;
    UINT32 NdisRssParamsLength;
    UINT32 ExtraLength;

    ExtraLength = XdpRssParams->IndirectionTableSize + XdpRssParams->HashSecretKeySize;

    //
    // TODO: use revision 3 with DefaultProcessorNumber if adapter supports it.
    //

    NdisRssParamsLength = NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2 + ExtraLength;

    NdisRssParams =
        ExAllocatePoolZero(NonPagedPoolNx, NdisRssParamsLength, POOLTAG_OFFLOAD);
    if (NdisRssParams == NULL) {
        TraceError(TRACE_LWF, "Failed to allocate NDIS RSS params");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    NdisRssParams->Header.Type = NDIS_OBJECT_TYPE_RSS_PARAMETERS;
    NdisRssParams->Header.Revision = NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2;
    NdisRssParams->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2;

    if (XdpRssParams->State == XdpOffloadStateEnabled) {
        PROCESSOR_NUMBER *NdisIndirectionTable;
        UCHAR *NdisHashSecretKey;

        NdisRssParams->Flags =
            XdpToNdisRssFlags(XdpRssParams->Flags) | NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED;

        NdisRssParams->HashInformation =
            NDIS_RSS_HASH_INFO_FROM_TYPE_AND_FUNC(
                XdpToNdisRssHashType(XdpRssParams->HashType),
                NdisHashFunctionToeplitz);
        NdisRssParams->IndirectionTableSize = XdpRssParams->IndirectionTableSize;
        NdisRssParams->IndirectionTableOffset = NdisRssParams->Header.Size;
        NdisRssParams->HashSecretKeySize = XdpRssParams->HashSecretKeySize;
        NdisRssParams->HashSecretKeyOffset =
            NdisRssParams->IndirectionTableOffset + NdisRssParams->IndirectionTableSize;

        NdisIndirectionTable = RTL_PTR_ADD(NdisRssParams, NdisRssParams->IndirectionTableOffset);
        RtlCopyMemory(
            NdisIndirectionTable, &XdpRssParams->IndirectionTable,
            XdpRssParams->IndirectionTableSize);

        NdisHashSecretKey = RTL_PTR_ADD(NdisRssParams, NdisRssParams->HashSecretKeyOffset);
        RtlCopyMemory(
            NdisHashSecretKey, &XdpRssParams->HashSecretKey, XdpRssParams->HashSecretKeySize);
    } else {
        NdisRssParams->Flags =
            NDIS_RSS_PARAM_FLAG_DISABLE_RSS |
            NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED |
            NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED |
            NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED |
            NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED;
    }

    *NdisRssParamsOut = NdisRssParams;
    *NdisRssParamsLengthOut = NdisRssParamsLength;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
NTSTATUS
InitializeXdpRssParamsFromNdisRssParams(
    _In_ CONST NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams,
    _In_ UINT32 NdisRssParamsLength,
    _Out_ XDP_OFFLOAD_PARAMS_RSS *XdpRssParams
    )
{
    NTSTATUS Status;

    RtlZeroMemory(XdpRssParams, sizeof(*XdpRssParams));

    if (NdisRssParamsLength < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        NdisRssParams->Header.Type != NDIS_OBJECT_TYPE_RSS_PARAMETERS ||
        NdisRssParams->Header.Revision < NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2 ||
        NdisRssParams->Header.Size < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2) {
        //
        // For simplicity, require RSS revision 2 which uses PROCESSOR_NUMBER
        // entries in the indirection table.
        //
        // TODO: support revision 1.
        //
        // TODO: handle revision 3 DefaultProcessorNumber.
        //
        TraceError(
            TRACE_LWF,
            "Unsupported NDIS RSS params NdisRssParamsLength=%u Type=%u Revision=%u Size=%u",
            NdisRssParamsLength, NdisRssParams->Header.Type,
            NdisRssParams->Header.Revision, NdisRssParams->Header.Size);
        ASSERT(FALSE);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (NdisRssParamsLength < (NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2 +
            NdisRssParams->IndirectionTableSize + NdisRssParams->HashSecretKeySize)) {
        TraceError(
            TRACE_LWF,
            "NDIS RSS param length too small NdisRssParamsLength=%u",
            NdisRssParamsLength);
        ASSERT(FALSE);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (XdpLwfOffloadIsNdisRssEnabled(NdisRssParams)) {
        PROCESSOR_NUMBER *NdisIndirectionTable;
        UCHAR *NdisHashSecretKey;

        ASSERT(NdisRssParams->IndirectionTableSize <= sizeof(XdpRssParams->IndirectionTable));
        ASSERT(NdisRssParams->HashSecretKeySize <= sizeof(XdpRssParams->HashSecretKey));

        XdpRssParams->State = XdpOffloadStateEnabled;
        XdpRssParams->Flags = NdisToXdpRssFlags(NdisRssParams->Flags);
        XdpRssParams->HashType =
            NdisToXdpRssHashType(
                NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(NdisRssParams->HashInformation));
        XdpRssParams->IndirectionTableSize = NdisRssParams->IndirectionTableSize;
        XdpRssParams->HashSecretKeySize = NdisRssParams->HashSecretKeySize;

        NdisIndirectionTable = RTL_PTR_ADD(NdisRssParams, NdisRssParams->IndirectionTableOffset);
        RtlCopyMemory(
            &XdpRssParams->IndirectionTable, NdisIndirectionTable,
            NdisRssParams->IndirectionTableSize);

        NdisHashSecretKey = RTL_PTR_ADD(NdisRssParams, NdisRssParams->HashSecretKeyOffset);
        RtlCopyMemory(
            &XdpRssParams->HashSecretKey, NdisHashSecretKey,
            NdisRssParams->HashSecretKeySize);

        // Revision 2
        // TODO: handle ProcessorMasks

        // Revision 3
        // TODO: handle DefaultProcessorNumber
    } else {
        XdpRssParams->State = XdpOffloadStateDisabled;
        ASSERT(NdisRssParams->IndirectionTableSize == 0);
        ASSERT(NdisRssParams->HashSecretKeySize == 0);
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
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
VOID
XdpLwfFreeRssSetting(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting =
        CONTAINING_RECORD(Entry, XDP_LWF_OFFLOAD_SETTING_RSS, DeleteEntry);

    ExFreePoolWithTag(RssSetting, POOLTAG_OFFLOAD);
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
NTSTATUS
XdpLwfOffloadRssGetCapabilities(
    _In_ XDP_LWF_FILTER *Filter,
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesLength
    )
{
    NDIS_RECEIVE_SCALE_CAPABILITIES *NdisRssCaps = &Filter->Offload.RssCaps;
    ASSERT(RssCapabilities != NULL);
    ASSERT(*RssCapabilitiesLength == sizeof(*RssCapabilities));

    RtlZeroMemory(RssCapabilities, sizeof(*RssCapabilities));

    if (NdisRssCaps->Header.Revision >= NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2) {
        RssCapabilities->Header.Revision = XDP_RSS_CAPABILITIES_REVISION_2;
        RssCapabilities->Header.Size = XDP_SIZEOF_RSS_CAPABILITIES_REVISION_2;
    } else {
        RssCapabilities->Header.Revision = XDP_RSS_CAPABILITIES_REVISION_1;
        RssCapabilities->Header.Size = XDP_SIZEOF_RSS_CAPABILITIES_REVISION_1;
    }

    RssCapabilities->HashTypes =
        NdisToXdpRssHashTypeCapabilities(NdisRssCaps->CapabilitiesFlags);
    RssCapabilities->HashSecretKeySize = RSS_HASH_SECRET_KEY_MAX_SIZE;
    RssCapabilities->NumberOfReceiveQueues = NdisRssCaps->NumberOfReceiveQueues;
    if (NdisRssCaps->Header.Revision >= NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2) {
        RssCapabilities->NumberOfIndirectionTableEntries =
            NdisRssCaps->NumberOfIndirectionTableEntries;
    }

    *RssCapabilitiesLength = RssCapabilities->Header.Size;

    return STATUS_SUCCESS;
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
NTSTATUS
XdpLwfOffloadRssGet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _Inout_ UINT32 *RssParamsLength
    )
{
    NTSTATUS Status;
    XDP_LWF_OFFLOAD_SETTING_RSS *CurrentRssSetting = NULL;

    ASSERT(RssParams != NULL);
    ASSERT(*RssParamsLength == sizeof(*RssParams));

    *RssParamsLength = 0;

    //
    // Determine the appropriate edge.
    //
    switch (OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        if (Filter->Offload.LowerEdge.Rss != NULL) {
            CurrentRssSetting = Filter->Offload.LowerEdge.Rss;
        } else {
            //
            // No lower edge state implies lower and upper edge state are the same.
            //
            CurrentRssSetting = Filter->Offload.UpperEdge.Rss;
        }
        break;
    case XdpOffloadEdgeUpper:
        CurrentRssSetting = Filter->Offload.UpperEdge.Rss;
        break;
    default:
        ASSERT(FALSE);
        CurrentRssSetting = NULL;
        break;
    }

    if (CurrentRssSetting == NULL) {
        //
        // RSS not initialized yet.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS params not found", OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
    } else {
        RtlCopyMemory(RssParams, &CurrentRssSetting->Params, sizeof(CurrentRssSetting->Params));
        *RssParamsLength = sizeof(CurrentRssSetting->Params);
        Status = STATUS_SUCCESS;
    }

    return Status;
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
VOID
RemoveLowerEdgeRssSetting(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    NTSTATUS Status;
    XDP_OFFLOAD_PARAMS_RSS *XdpRssParams;
    UINT32 NdisRssParamsLength;
    ULONG BytesReturned;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    XDP_LWF_GENERIC_INDIRECTION_STORAGE Indirection = {0};

    ASSERT(Filter->Offload.LowerEdge.Rss != NULL);

    XdpGenericDetachDatapath(&Filter->Generic, TRUE, FALSE);

    //
    // Clear the lower edge setting to imply lower edge has no independent settings.
    //
    Filter->Offload.LowerEdge.Rss = NULL;

    if (Filter->Offload.UpperEdge.Rss == NULL) {
        //
        // Upper edge RSS is not initialized.
        //
        TraceError(TRACE_LWF, "Filter=%p Upper edge RSS params not present", Filter);
        goto Exit;
    }

    //
    // Plumb the lower edge with the upper edge settings.
    //
    // The lower and upper edge settings could have diverged, so we must
    // specify all parameters. None can be indicated as unchanged.
    //
    // TODO: When encountering failures here, we need to try our best to
    //       not leave the NDIS stack in an inconsistent state. Add
    //       retry logic.
    //

    XdpRssParams = &Filter->Offload.UpperEdge.Rss->Params;

    InheritXdpRssParams(XdpRssParams, NULL);

    Status =
        CreateNdisRssParamsFromXdpRssParams(
            XdpRssParams, &NdisRssParams, &NdisRssParamsLength);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed to create NDIS RSS params Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    Status =
        XdpGenericRssCreateIndirection(
            &Filter->Generic, NdisRssParams, NdisRssParamsLength, &Indirection);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, NdisRequestSetInformation,
            OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, NdisRssParamsLength, 0, 0,
            &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed OID_GEN_RECEIVE_SCALE_PARAMETERS Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    XdpGenericRssApplyIndirection(&Filter->Generic, &Indirection);

Exit:

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    XdpGenericRssFreeIndirection(&Indirection);
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
VOID
ReferenceRssSetting(
    _In_ XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting,
    _In_ XDP_LWF_FILTER *Filter
    )
{
    UNREFERENCED_PARAMETER(Filter);
    XdpIncrementReferenceCount(&RssSetting->ReferenceCount);
}

static
_When_(Filter != NULL, _Requires_lock_held_(&Filter->Offload.Lock))
VOID
DereferenceRssSetting(
    _In_ XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting,
    _In_opt_ XDP_LWF_FILTER *Filter
    )
{
    if (XdpDecrementReferenceCount(&RssSetting->ReferenceCount)) {
        if (Filter != NULL && Filter->Offload.LowerEdge.Rss == RssSetting) {
            RemoveLowerEdgeRssSetting(Filter);
        }

        XdpLifetimeDelete(XdpLwfFreeRssSetting, &RssSetting->DeleteEntry);
    }
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
NTSTATUS
XdpLwfOffloadRssSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _In_ UINT32 RssParamsLength,
    _Out_ BOOLEAN *AttachRxDatapath
    )
{
    NTSTATUS Status;
    ULONG *BitmapBuffer = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *CurrentRssSetting = NULL;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    UINT32 NdisRssParamsLength;
    ULONG BytesReturned;
    XDP_LWF_GENERIC_INDIRECTION_STORAGE Indirection = {0};

    ASSERT(RssParams != NULL);
    ASSERT(RssParamsLength == sizeof(*RssParams));

    *AttachRxDatapath = FALSE;

    //
    // Determine the appropriate edge.
    //
    switch (OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        CurrentRssSetting = Filter->Offload.LowerEdge.Rss;
        break;
    case XdpOffloadEdgeUpper:
        //
        // Don't allow setting RSS on the upper edge.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS params not found", OffloadContext);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Exit;
    default:
        ASSERT(FALSE);
        CurrentRssSetting = NULL;
        break;
    }

    //
    // There is an NDIS bug where LWFs setting the RSS configuration can cause
    // a bugcheck if no upper level has previously set the RSS configuration.
    // To avoid this scenario, prevent setting RSS configuration until the
    // upper layer has set the configuration.
    //
    if (Filter->Offload.UpperEdge.Rss == NULL) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS cannot be set without upper layer being set", OffloadContext);
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    //
    // Ensure compatibility with existing settings. Currently, only allow
    // overwrite of a configuration plumbed by the same offload context.
    //
    if (CurrentRssSetting != NULL) {
        if (CurrentRssSetting != OffloadContext->Settings.Rss) {
            //
            // TODO: ref count the edge setting and make this policy less restrictive
            //
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Existing RSS params", OffloadContext);
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }
    } else {
        CurrentRssSetting = Filter->Offload.UpperEdge.Rss;
    }

    //
    // Validate input.
    //

    if (RssParams->Flags & XDP_RSS_FLAG_SET_HASH_TYPE) {
        if (((RssParams->HashType & XDP_RSS_HASH_TYPE_TCP_IPV4) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4)) ||
            ((RssParams->HashType & XDP_RSS_HASH_TYPE_TCP_IPV6) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6)) ||
            ((RssParams->HashType & XDP_RSS_HASH_TYPE_TCP_IPV6_EX) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX)) ||
            ((RssParams->HashType & XDP_RSS_HASH_TYPE_UDP_IPV4) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV4)) ||
            ((RssParams->HashType & XDP_RSS_HASH_TYPE_UDP_IPV6) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6)) ||
            ((RssParams->HashType & XDP_RSS_HASH_TYPE_UDP_IPV6_EX) &&
                !(Filter->Offload.RssCaps.CapabilitiesFlags & NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6_EX))) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Unsupported hash type HashType=0x%08x",
                OffloadContext, RssParams->HashType);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    if (RssParams->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY) {
        if (RssParams->HashSecretKeySize > RSS_HASH_SECRET_KEY_MAX_SIZE) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Unsupported hash secret key size HashSecretKeySize=%u Max=%u",
                OffloadContext, RssParams->HashSecretKeySize,
                RSS_HASH_SECRET_KEY_MAX_SIZE);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    if (RssParams->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE) {
        UINT32 ProcessorCount = 0;
        CONST UINT32 NumEntries =
            RssParams->IndirectionTableSize / sizeof(RssParams->IndirectionTable[0]);
        ULONG BufferSize =
            ALIGN_UP_BY(
                (KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS) + 7) / 8, sizeof(ULONG));
        RTL_BITMAP ProcessorBitmap;

        if (NumEntries > Filter->Offload.RssCaps.NumberOfIndirectionTableEntries) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Unsupported indirection table entry count NumEntries=%u Max=%u",
                OffloadContext, NumEntries, Filter->Offload.RssCaps.NumberOfIndirectionTableEntries);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }

        BitmapBuffer = ExAllocatePoolZero(NonPagedPoolNx, BufferSize, POOLTAG_OFFLOAD);
        if (BitmapBuffer == NULL) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Failed to allocate bitmap", OffloadContext);
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        RtlInitializeBitMap(
            &ProcessorBitmap, BitmapBuffer,
            KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS));

        for (UINT32 Index = 0; Index < NumEntries; Index++) {
            ULONG ProcessorIndex =
                KeGetProcessorIndexFromNumber(&RssParams->IndirectionTable[Index]);

            if (ProcessorIndex == INVALID_PROCESSOR_INDEX) {
                TraceError(
                    TRACE_LWF,
                    "OffloadContext=%p Invalid processor number Group=%u Number=%u",
                    OffloadContext, RssParams->IndirectionTable[Index].Group,
                    RssParams->IndirectionTable[Index].Number);
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }

            RtlSetBit(&ProcessorBitmap, ProcessorIndex);
        }

        ProcessorCount = RtlNumberOfSetBits(&ProcessorBitmap);

        //
        // RSS allows more processors than receive queues, but the RSS
        // implementation in TCPIP constrains the number of processors to the
        // number of receive queues. The NDIS API to query the actual maximum
        // processor count is unavailable to LWFs, so simply use the number of
        // receive queues instead.
        //
        // TODO: can we query the max processor count some other way? (It can be
        // queried via miniport and protocol driver handles, and WMI.)
        //
        if (ProcessorCount > Filter->Offload.RssCaps.NumberOfReceiveQueues) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Unsupported processor count ProcessorCount=%u Max=%u",
                OffloadContext, ProcessorCount, Filter->Offload.RssCaps.NumberOfReceiveQueues);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    //
    // Clone the input params.
    //

    RssSetting = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RssSetting), POOLTAG_OFFLOAD);
    if (RssSetting == NULL) {
        TraceError(
            TRACE_LWF, "OffloadContext=%p Failed to allocate XDP RSS setting", OffloadContext);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&RssSetting->ReferenceCount);
    RtlCopyMemory(&RssSetting->Params, RssParams, RssParamsLength);

    //
    // Inherit unspecified parameters from the current RSS settings.
    //
    InheritXdpRssParams(
        &RssSetting->Params,
        (CurrentRssSetting != NULL) ? &CurrentRssSetting->Params : NULL);

    //
    // Form and issue the OID.
    //

    Status =
        CreateNdisRssParamsFromXdpRssParams(
            &RssSetting->Params, &NdisRssParams, &NdisRssParamsLength);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpGenericRssCreateIndirection(
            &Filter->Generic, NdisRssParams, NdisRssParamsLength, &Indirection);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, NdisRequestSetInformation,
            OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, NdisRssParamsLength, 0, 0,
            &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Failed OID_GEN_RECEIVE_SCALE_PARAMETERS Status=%!STATUS!",
            OffloadContext, Status);
        goto Exit;
    }

    XdpGenericRssApplyIndirection(&Filter->Generic, &Indirection);

    //
    // Update the tracking RSS state for the edge.
    //

    OldRssSetting = Filter->Offload.LowerEdge.Rss;
    Filter->Offload.LowerEdge.Rss = RssSetting;
    OffloadContext->Settings.Rss = RssSetting;
    RssSetting = NULL;

    if (OldRssSetting == NULL) {
        *AttachRxDatapath = TRUE;
    } else {
        DereferenceRssSetting(OldRssSetting, Filter);
    }

    TraceInfo(TRACE_LWF, "Filter=%p updated lower edge RSS settings", Filter);

Exit:

    if (BitmapBuffer != NULL) {
        ExFreePoolWithTag(BitmapBuffer, POOLTAG_OFFLOAD);
    }

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    if (RssSetting != NULL) {
        ExFreePoolWithTag(RssSetting, POOLTAG_OFFLOAD);
    }

    XdpGenericRssFreeIndirection(&Indirection);

    return Status;
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
NTSTATUS
XdpLwfOffloadRssReference(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ BOOLEAN *AttachRxDatapath
    )
{
    NTSTATUS Status;
    XDP_LWF_OFFLOAD_SETTING_RSS *CurrentRssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting = NULL;

    *AttachRxDatapath = FALSE;

    //
    // Always use lower edge. Upper edge hook points (RX inject, TX inspect)
    // still rely on lower edge RSS settings.
    //
    CurrentRssSetting = Filter->Offload.LowerEdge.Rss;

    if (OffloadContext->Settings.Rss != NULL) {
        //
        // Don't allow referencing when it has already been set or previously
        // referenced by this handle.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS params already set", OffloadContext);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Exit;
    }

    if (CurrentRssSetting != NULL) {
        //
        // Simply reference the existing settings.
        //
        ReferenceRssSetting(CurrentRssSetting, Filter);
        OffloadContext->Settings.Rss = CurrentRssSetting;
    } else {
        //
        // Make the lower edge independent.
        //

        CurrentRssSetting = Filter->Offload.UpperEdge.Rss;
        if (CurrentRssSetting == NULL) {
            TraceError(
                TRACE_LWF, "OffloadContext=%p Upper edge RSS settings not present", OffloadContext);
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }

        RssSetting = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RssSetting), POOLTAG_OFFLOAD);
        if (RssSetting == NULL) {
            TraceError(
                TRACE_LWF, "OffloadContext=%p Failed to allocate XDP RSS setting", OffloadContext);
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        XdpInitializeReferenceCount(&RssSetting->ReferenceCount);
        RtlCopyMemory(&RssSetting->Params, CurrentRssSetting, sizeof(*CurrentRssSetting));

        OldRssSetting = Filter->Offload.LowerEdge.Rss;
        Filter->Offload.LowerEdge.Rss = RssSetting;
        OffloadContext->Settings.Rss = RssSetting;
        RssSetting = NULL;
        ASSERT(OldRssSetting == NULL);

        *AttachRxDatapath = TRUE;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (RssSetting != NULL) {
        ExFreePoolWithTag(RssSetting, POOLTAG_OFFLOAD);
    }

    return Status;
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
NTSTATUS
XdpLwfOffloadRssUpdate(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams,
    _In_ ULONG NdisRssParamsLength
    )
{
    NTSTATUS Status;
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting = NULL;

    RssSetting = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RssSetting), POOLTAG_OFFLOAD);
    if (RssSetting == NULL) {
        TraceError(
            TRACE_LWF, "Filter=%p Failed to allocate XDP RSS setting", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&RssSetting->ReferenceCount);

    Status =
        InitializeXdpRssParamsFromNdisRssParams(
            NdisRssParams, NdisRssParamsLength, &RssSetting->Params);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Inherit unspecified parameters from the current upper edge settings.
    //
    if (Filter->Offload.UpperEdge.Rss != NULL) {
        InheritXdpRssParams(&RssSetting->Params, &Filter->Offload.UpperEdge.Rss->Params);
    }

    OldRssSetting = Filter->Offload.UpperEdge.Rss;
    Filter->Offload.UpperEdge.Rss = RssSetting;
    RssSetting = NULL;
    Status = STATUS_SUCCESS;

    TraceInfo(TRACE_LWF, "Filter=%p updated upper edge RSS settings", Filter);

Exit:

    if (OldRssSetting != NULL) {
        XdpLifetimeDelete(XdpLwfFreeRssSetting, &OldRssSetting->DeleteEntry);
    }

    if (RssSetting != NULL) {
        ExFreePoolWithTag(RssSetting, POOLTAG_OFFLOAD);
    }

    return Status;
}

VOID
XdpLwfOffloadRssInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    NTSTATUS Status;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps = {0};
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    ULONG BytesReturned = 0;

    //
    // TODO: Merge generic RSS and RSS offload modules.
    //

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, NdisRequestQueryInformation,
            OID_GEN_RECEIVE_SCALE_CAPABILITIES, &RssCaps,
            sizeof(RssCaps), 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed OID_GEN_RECEIVE_SCALE_CAPABILITIES Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    if (NT_SUCCESS(Status) &&
        BytesReturned >= NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1 &&
        RssCaps.Header.Type == NDIS_OBJECT_TYPE_RSS_CAPABILITIES &&
        RssCaps.Header.Revision >= NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_1 &&
        RssCaps.Header.Size >= NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1) {
        Filter->Offload.RssCaps = RssCaps;
    }

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, NdisRequestQueryInformation,
            OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, 0, 0, 0,
            &BytesReturned);
    if (Status != STATUS_BUFFER_TOO_SMALL || BytesReturned == 0) {
        if (Status == STATUS_NOT_SUPPORTED) {
            // Consider RSS as unspecified.
            Status = STATUS_SUCCESS;
        }
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed OID_GEN_RECEIVE_SCALE_PARAMETERS Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    NdisRssParams =
        ExAllocatePoolZero(NonPagedPoolNx, BytesReturned, POOLTAG_OFFLOAD);
    if (NdisRssParams == NULL) {
        TraceError(
            TRACE_LWF, "Filter=%p Failed to allocate NDIS RSS params", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, NdisRequestQueryInformation,
            OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, BytesReturned, 0, 0,
            &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed OID_GEN_RECEIVE_SCALE_PARAMETERS Status=%!STATUS!",
            Filter, Status);
        goto Exit;
    }

    Status = XdpLwfOffloadRssUpdate(Filter, NdisRssParams, BytesReturned);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }
}

static
_Requires_lock_held_(&Filter->Offload.Lock)
VOID
XdpLwfOffloadRssDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting;

    //
    // Restore the miniport's RSS settings to what the upper layers expect.
    //
    if (Filter->Offload.LowerEdge.Rss != NULL) {
        RemoveLowerEdgeRssSetting(Filter);
    }

    //
    // Free the upper edge RSS settings.
    //
    if (Filter->Offload.UpperEdge.Rss != NULL) {
        OldRssSetting = Filter->Offload.UpperEdge.Rss;
        Filter->Offload.UpperEdge.Rss = NULL;
        if (OldRssSetting != NULL) {
            XdpLifetimeDelete(XdpLwfFreeRssSetting, &OldRssSetting->DeleteEntry);
        }
    }
}

static
NTSTATUS
XdpLwfOpenInterfaceOffloadHandle(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Out_ VOID **InterfaceOffloadHandle
    )
{
    NTSTATUS Status;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = NULL;

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (Filter->NdisFilterHandle == NULL) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Detached filter", Filter);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

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

    InsertTailList(&Filter->Offload.InterfaceOffloadHandleListHead, &OffloadContext->Link);
    XdpLwfReferenceFilter(Filter);
    OffloadContext->Filter = Filter;
    OffloadContext->Edge = XdpLwfConvertHookIdToOffloadEdge(HookId);
    *InterfaceOffloadHandle = OffloadContext;
    OffloadContext = NULL;
    Status = STATUS_SUCCESS;

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

    if (OffloadContext != NULL) {
        ExFreePoolWithTag(OffloadContext, POOLTAG_OFFLOAD);
    }

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

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    //
    // Revert the handle's configured offloads.
    //

    if (OffloadContext->Settings.Rss != NULL) {
        DereferenceRssSetting(
            OffloadContext->Settings.Rss, (OffloadContext->IsInvalid) ? NULL : Filter);
        OffloadContext->Settings.Rss = NULL;
    }

    if (OffloadContext->IsInvalid) {
        goto Exit;
    }

    RemoveEntryList(&OffloadContext->Link);

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

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

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (OffloadContext->IsInvalid) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Interface offload context invalidated",
            OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

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

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

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

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (OffloadContext->IsInvalid) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Interface offload context invalidated",
            OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

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

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

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
    BOOLEAN AttachRxDatapath = FALSE;

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (OffloadContext->IsInvalid) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Interface offload context invalidated",
            OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    switch (OffloadType) {
    case XdpOffloadRss:
        Status =
            XdpLwfOffloadRssSet(
                Filter, OffloadContext, OffloadParams, OffloadParamsSize, &AttachRxDatapath);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

    if (AttachRxDatapath) {
        XdpGenericAttachDatapath(&Filter->Generic, TRUE, FALSE);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
NTSTATUS
XdpLwfReferenceInterfaceOffload(
    _In_ VOID *InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType
    )
{
    NTSTATUS Status;
    XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext = InterfaceOffloadHandle;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;
    BOOLEAN AttachRxDatapath = FALSE;

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (OffloadContext->IsInvalid) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Interface offload context invalidated",
            OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    switch (OffloadType) {
    case XdpOffloadRss:
        Status = XdpLwfOffloadRssReference(Filter, OffloadContext, &AttachRxDatapath);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

    if (AttachRxDatapath) {
        XdpGenericAttachDatapath(&Filter->Generic, TRUE, FALSE);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

NDIS_STATUS
XdpLwfOffloadInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _Out_ XDP_OID_ACTION *Action,
    _Out_ NDIS_STATUS *CompletionStatus
    )
{
    NTSTATUS Status;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    UINT32 NdisRssParamsLength;

    *Action = XdpOidActionPass;
    *CompletionStatus = NDIS_STATUS_SUCCESS;

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    if (Request->RequestType == NdisRequestSetInformation &&
        Request->DATA.SET_INFORMATION.Oid == OID_GEN_RECEIVE_SCALE_PARAMETERS) {

        //
        // Update the upper edge RSS state.
        //
        Status =
            XdpLwfOffloadRssUpdate(
                Filter,
                Request->DATA.SET_INFORMATION.InformationBuffer,
                Request->DATA.SET_INFORMATION.InformationBufferLength);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        if (Filter->Offload.LowerEdge.Rss != NULL) {
            //
            // The lower edge has independently managed RSS state. Complete the
            // OID set request to maintain independence.
            //
            TraceInfo(
                TRACE_LWF,
                "Filter=%p Completing upper edge OID_GEN_RECEIVE_SCALE_PARAMETERS set",
                Filter);
            *Action = XdpOidActionComplete;
            *CompletionStatus = NDIS_STATUS_SUCCESS;
        } else {
            //
            // The lower edge does not have independently managed RSS state.
            // Pass the OID set request through.
            //
            // TODO: Need to revert any offload state changes from this OID if
            // subsequent OID inspection (generic module) fails the OID. Or better,
            // consolidate the RSS OID processing to one place.
            //
            TraceVerbose(
                TRACE_LWF,
                "Filter=%p Passing upper edge OID_GEN_RECEIVE_SCALE_PARAMETERS set",
                Filter);
            *Action = XdpOidActionPass;
        }
    } else if (Request->RequestType == NdisRequestQueryInformation &&
        Request->DATA.QUERY_INFORMATION.Oid == OID_GEN_RECEIVE_SCALE_PARAMETERS) {

        //
        // Complete the OID and return upper edge settings, if any.
        //

        *Action = XdpOidActionComplete;

        if (Filter->Offload.UpperEdge.Rss == NULL) {
            TraceError(TRACE_LWF, "Filter=%p Upper edge RSS params not present", Filter);
            *CompletionStatus = NDIS_STATUS_FAILURE;
            Status = STATUS_SUCCESS;
            goto Exit;
        }

        Status =
            CreateNdisRssParamsFromXdpRssParams(
                &Filter->Offload.UpperEdge.Rss->Params, &NdisRssParams, &NdisRssParamsLength);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                TRACE_LWF,
                "Filter=%p Failed to create NDIS RSS params Status=%!STATUS!",
                Filter, Status);
            *CompletionStatus = NDIS_STATUS_FAILURE;
            Status = STATUS_SUCCESS;
            goto Exit;
        }

        if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < NdisRssParamsLength) {
            Request->DATA.QUERY_INFORMATION.BytesNeeded = NdisRssParamsLength;
            *CompletionStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
            Status = STATUS_SUCCESS;
            goto Exit;
        }

        RtlCopyMemory(
            Request->DATA.QUERY_INFORMATION.InformationBuffer,
            NdisRssParams, NdisRssParamsLength);
        Request->DATA.QUERY_INFORMATION.BytesWritten = NdisRssParamsLength;
        *CompletionStatus = NDIS_STATUS_SUCCESS;
    }

    Status = STATUS_SUCCESS;

Exit:

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    return XdpConvertNtStatusToNdisStatus(Status);
}

VOID
XdpLwfOffloadRssNblTransform(
    _In_ XDP_LWF_FILTER *Filter,
    _Inout_ NET_BUFFER_LIST *Nbl
    )
{
    //
    // Ideally the transformation would be to re-hash and schedule the NBLs onto
    // the appropriate RSS processors according to the upper edge RSS settings.
    // As a minimal effort transformation that should still work, clear the hash
    // OOB fields.
    //
    UNREFERENCED_PARAMETER(Filter);
    NET_BUFFER_LIST_SET_HASH_FUNCTION(Nbl, 0);
    NET_BUFFER_LIST_SET_HASH_TYPE(Nbl, 0);
    NET_BUFFER_LIST_SET_HASH_VALUE(Nbl, 0);
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

VOID
XdpLwfOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    RtlAcquirePushLockExclusive(&Filter->Offload.Lock);

    //
    // Invalidate all interface offload handles.
    //
    // TODO: Tie offload handle validity to XDPIF interface set lifetime and
    //       notify XDPIF interface set clients upon interface set deletion.
    //
    while (!IsListEmpty(&Filter->Offload.InterfaceOffloadHandleListHead)) {
        XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext =
            CONTAINING_RECORD(
                Filter->Offload.InterfaceOffloadHandleListHead.Flink,
                XDP_LWF_INTERFACE_OFFLOAD_CONTEXT, Link);

        RemoveEntryList(&OffloadContext->Link);
        InitializeListHead(&OffloadContext->Link);
        OffloadContext->IsInvalid = TRUE;
    }

    XdpLwfOffloadRssDeactivate(Filter);

    RtlReleasePushLockExclusive(&Filter->Offload.Lock);

    TraceExitSuccess(TRACE_LWF);
}

VOID
XdpLwfOffloadInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    //
    // Initialize the existing upper edge settings by querying the miniport.
    //
    // TODO: how do we avoid a race with the protocol driver plumbing offload
    // OIDS? The OIDs might already be in flight before this LWF has a chance to
    // inspect it and might still be in flight after we query NDIS.
    //

    ExInitializePushLock(&Filter->Offload.Lock);
    InitializeListHead(&Filter->Offload.InterfaceOffloadHandleListHead);
}

VOID
XdpLwfOffloadUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    UNREFERENCED_PARAMETER(Filter);
}
