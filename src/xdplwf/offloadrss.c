//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offloadrss.tmh"

#define RSS_HASH_SECRET_KEY_MAX_SIZE NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2

static
BOOLEAN
XdpLwfOffloadIsNdisRssEnabled(
    _In_ CONST NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams
    )
{
    return
        NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(NdisRssParams->HashInformation) != 0 &&
        (NdisRssParams->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) == 0;
}

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
VOID
XdpLwfFreeRssSetting(
    _In_ XDP_LIFETIME_ENTRY *Entry
    )
{
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting =
        CONTAINING_RECORD(Entry, XDP_LWF_OFFLOAD_SETTING_RSS, DeleteEntry);

    ExFreePoolWithTag(RssSetting, POOLTAG_OFFLOAD);
}

typedef struct _XDP_LWF_OFFLOAD_RSS_GET_CAPABILITIES {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities;
    _Inout_ UINT32 *RssCapabilitiesLength;
} XDP_LWF_OFFLOAD_RSS_GET_CAPABILITIES;

static
_Offload_work_routine_
VOID
XdpLwfOffloadRssGetCapabilitiesWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_RSS_GET_CAPABILITIES *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_RSS_GET_CAPABILITIES, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    NDIS_RECEIVE_SCALE_CAPABILITIES *NdisRssCaps = &Filter->Offload.RssCaps;
    XDP_RSS_CAPABILITIES *RssCapabilities = Request->RssCapabilities;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    ASSERT(RssCapabilities != NULL);
    ASSERT(*Request->RssCapabilitiesLength == sizeof(*RssCapabilities));

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

    *Request->RssCapabilitiesLength = RssCapabilities->Header.Size;
    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);

    TraceExitSuccess(TRACE_LWF);
}

NTSTATUS
XdpLwfOffloadRssGetCapabilities(
    _In_ XDP_LWF_FILTER *Filter,
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesLength
    )
{
    XDP_LWF_OFFLOAD_RSS_GET_CAPABILITIES Request = {0};

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    Request.RssCapabilities = RssCapabilities;
    Request.RssCapabilitiesLength = RssCapabilitiesLength;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadRssGetCapabilitiesWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_LWF);

    return STATUS_SUCCESS;
}

typedef struct _XDP_LWF_OFFLOAD_RSS_GET {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
    _Out_ NTSTATUS Status;
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext;
    _Out_ XDP_OFFLOAD_PARAMS_RSS *RssParams;
    _Out_ UINT32 *RssParamsLength;
} XDP_LWF_OFFLOAD_RSS_GET;

static
_Offload_work_routine_
VOID
XdpLwfOffloadRssGetWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_RSS_GET *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_RSS_GET, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    XDP_LWF_OFFLOAD_SETTING_RSS *CurrentRssSetting = NULL;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    *Request->RssParamsLength = 0;

    //
    // Determine the appropriate edge.
    //
    switch (Request->OffloadContext->Edge) {
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
            "OffloadContext=%p RSS params not found", Request->OffloadContext);
        Request->Status = STATUS_INVALID_DEVICE_STATE;
    } else {
        RtlCopyMemory(
            Request->RssParams, &CurrentRssSetting->Params, sizeof(CurrentRssSetting->Params));
        *Request->RssParamsLength = sizeof(CurrentRssSetting->Params);
        Request->Status = STATUS_SUCCESS;
    }

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);

    TraceExitSuccess(TRACE_LWF);
}

NTSTATUS
XdpLwfOffloadRssGet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _Inout_ UINT32 *RssParamsLength
    )
{
    XDP_LWF_OFFLOAD_RSS_GET Request = {0};
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    ASSERT(RssParams != NULL);
    ASSERT(*RssParamsLength == sizeof(*RssParams));

    Request.OffloadContext = OffloadContext;
    Request.RssParams = RssParams;
    Request.RssParamsLength = RssParamsLength;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadRssGetWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    Status = Request.Status;

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
_Offload_work_routine_
_Requires_offload_rundown_ref_
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
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR, NdisRequestSetInformation,
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
_Offload_work_routine_
_Requires_offload_rundown_ref_
VOID
DereferenceRssSetting(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting
    )
{
    if (XdpDecrementReferenceCount(&RssSetting->ReferenceCount)) {
        if (Filter->Offload.LowerEdge.Rss == RssSetting) {
            RemoveLowerEdgeRssSetting(Filter);
        }

        XdpLifetimeDelete(XdpLwfFreeRssSetting, &RssSetting->DeleteEntry);
    }
}

typedef struct _XDP_LWF_OFFLOAD_RSS_SET {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
    _Out_ NTSTATUS Status;
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext;
    _In_ XDP_OFFLOAD_PARAMS_RSS *RssParams;
    _In_ UINT32 RssParamsLength;
    _Out_ BOOLEAN AttachRxDatapath;
} XDP_LWF_OFFLOAD_RSS_SET;

static
_Offload_work_routine_
VOID
XdpLwfOffloadRssSetWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_RSS_SET *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_RSS_SET, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    XDP_OFFLOAD_PARAMS_RSS *RssParams = Request->RssParams;
    ULONG *BitmapBuffer = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *RssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting = NULL;
    XDP_LWF_OFFLOAD_SETTING_RSS *CurrentRssSetting = NULL;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    UINT32 NdisRssParamsLength;
    ULONG BytesReturned;
    XDP_LWF_GENERIC_INDIRECTION_STORAGE Indirection = {0};

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    //
    // Determine the appropriate edge.
    //
    switch (Request->OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        CurrentRssSetting = Filter->Offload.LowerEdge.Rss;
        break;
    case XdpOffloadEdgeUpper:
        //
        // Don't allow setting RSS on the upper edge.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS params not found", Request->OffloadContext);
        Request->Status = STATUS_INVALID_DEVICE_REQUEST;
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
            "OffloadContext=%p RSS cannot be set without upper layer being set",
            Request->OffloadContext);
        Request->Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    //
    // Ensure compatibility with existing settings. Currently, only allow
    // overwrite of a configuration plumbed by the same offload context.
    //
    if (CurrentRssSetting != NULL) {
        if (CurrentRssSetting != Request->OffloadContext->Settings.Rss) {
            //
            // TODO: ref count the edge setting and make this policy less restrictive
            //
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Existing RSS params", Request->OffloadContext);
            Request->Status = STATUS_INVALID_DEVICE_STATE;
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
                Request->OffloadContext, RssParams->HashType);
            Request->Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    if (RssParams->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY) {
        if (RssParams->HashSecretKeySize > RSS_HASH_SECRET_KEY_MAX_SIZE) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Unsupported hash secret key size HashSecretKeySize=%u Max=%u",
                Request->OffloadContext, RssParams->HashSecretKeySize,
                RSS_HASH_SECRET_KEY_MAX_SIZE);
            Request->Status = STATUS_NOT_SUPPORTED;
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
                Request->OffloadContext, NumEntries,
                Filter->Offload.RssCaps.NumberOfIndirectionTableEntries);
            Request->Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }

        BitmapBuffer = ExAllocatePoolZero(NonPagedPoolNx, BufferSize, POOLTAG_OFFLOAD);
        if (BitmapBuffer == NULL) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Failed to allocate bitmap", Request->OffloadContext);
            Request->Status = STATUS_NO_MEMORY;
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
                    Request->OffloadContext, RssParams->IndirectionTable[Index].Group,
                    RssParams->IndirectionTable[Index].Number);
                Request->Status = STATUS_INVALID_PARAMETER;
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
                Request->OffloadContext, ProcessorCount,
                Filter->Offload.RssCaps.NumberOfReceiveQueues);
            Request->Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    //
    // Clone the input params.
    //

    RssSetting = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RssSetting), POOLTAG_OFFLOAD);
    if (RssSetting == NULL) {
        TraceError(
            TRACE_LWF, "OffloadContext=%p Failed to allocate XDP RSS setting",
            Request->OffloadContext);
        Request->Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&RssSetting->ReferenceCount);
    RtlCopyMemory(&RssSetting->Params, RssParams, Request->RssParamsLength);

    //
    // Inherit unspecified parameters from the current RSS settings.
    //
    InheritXdpRssParams(
        &RssSetting->Params,
        (CurrentRssSetting != NULL) ? &CurrentRssSetting->Params : NULL);

    //
    // Form and issue the OID.
    //

    Request->Status =
        CreateNdisRssParamsFromXdpRssParams(
            &RssSetting->Params, &NdisRssParams, &NdisRssParamsLength);
    if (!NT_SUCCESS(Request->Status)) {
        goto Exit;
    }

    Request->Status =
        XdpGenericRssCreateIndirection(
            &Filter->Generic, NdisRssParams, NdisRssParamsLength, &Indirection);
    if (!NT_SUCCESS(Request->Status)) {
        goto Exit;
    }

    Request->Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR, NdisRequestSetInformation,
            OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, NdisRssParamsLength, 0, 0,
            &BytesReturned);
    if (!NT_SUCCESS(Request->Status)) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Failed OID_GEN_RECEIVE_SCALE_PARAMETERS Status=%!STATUS!",
            Request->OffloadContext, Request->Status);
        goto Exit;
    }

    XdpGenericRssApplyIndirection(&Filter->Generic, &Indirection);

    //
    // Update the tracking RSS state for the edge.
    //

    OldRssSetting = Filter->Offload.LowerEdge.Rss;
    Filter->Offload.LowerEdge.Rss = RssSetting;
    Request->OffloadContext->Settings.Rss = RssSetting;
    RssSetting = NULL;

    if (OldRssSetting == NULL) {
        Request->AttachRxDatapath = TRUE;
    } else {
        DereferenceRssSetting(Filter, OldRssSetting);
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

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);

    TraceExitSuccess(TRACE_LWF);
}

NTSTATUS
XdpLwfOffloadRssSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _In_ UINT32 RssParamsLength
    )
{
    XDP_LWF_OFFLOAD_RSS_SET Request = {0};
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    ASSERT(RssParams != NULL);
    ASSERT(RssParamsLength == sizeof(*RssParams));

    Request.OffloadContext = OffloadContext;
    Request.RssParams = RssParams;
    Request.RssParamsLength = RssParamsLength;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadRssSetWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    Status = Request.Status;

    if (Request.AttachRxDatapath) {
        XdpGenericAttachDatapath(&Filter->Generic, TRUE, FALSE);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}

static
_Offload_work_routine_
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

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

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

    TraceExitStatus(TRACE_LWF);

    return Status;
}

typedef struct _XDP_LWF_OFFLOAD_RSS_INITIALIZE {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
} XDP_LWF_OFFLOAD_RSS_INITIALIZE;

static
_Offload_work_routine_
VOID
XdpLwfOffloadRssInitializeWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_RSS_INITIALIZE *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_RSS_INITIALIZE, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;
    NTSTATUS Status;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps = {0};
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    ULONG BytesReturned = 0;

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    //
    // TODO: Merge generic RSS and RSS offload modules.
    //

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_CAPABILITIES, &RssCaps,
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
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams, 0, 0, 0,
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
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRssParams,
            BytesReturned, 0, 0, &BytesReturned);
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

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);

    TraceExitStatus(TRACE_LWF);
}

VOID
XdpLwfOffloadRssInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_RSS_INITIALIZE Request = {0};

    TraceEnter(TRACE_LWF, "Filter=%p", Filter);

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    XdpLwfOffloadQueueWorkItem(Filter, &Request.WorkItem, XdpLwfOffloadRssInitializeWorker);
    KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

    TraceExitSuccess(TRACE_LWF);
}

_Offload_work_routine_
_Requires_offload_rundown_ref_
VOID
XdpLwfOffloadRssDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    XDP_LWF_OFFLOAD_SETTING_RSS *OldRssSetting;

    //
    // All offload handles should be closed, and therefore the lower edge should
    // already be cleaned up.
    //
    ASSERT(Filter->Offload.LowerEdge.Rss == NULL);

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

typedef struct _XDP_LWF_OFFLOAD_RSS_DEREFERENCE {
    _In_ XDP_LWF_OFFLOAD_WORKITEM WorkItem;
    _Inout_ KEVENT Event;
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext;
} XDP_LWF_OFFLOAD_RSS_DEREFERENCE;

static
_Offload_work_routine_
VOID
XdpLwfOffloadRssCloseInterfaceOffloadHandleWorker(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    )
{
    XDP_LWF_OFFLOAD_RSS_DEREFERENCE *Request =
        CONTAINING_RECORD(WorkItem, XDP_LWF_OFFLOAD_RSS_DEREFERENCE, WorkItem);
    XDP_LWF_FILTER *Filter = WorkItem->Filter;

    DereferenceRssSetting(Filter, Request->OffloadContext->Settings.Rss);

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

VOID
XdpLwfOffloadRssCloseInterfaceOffloadHandle(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext
    )
{
    if (OffloadContext->Settings.Rss != NULL) {
        XDP_LWF_OFFLOAD_RSS_DEREFERENCE Request = {0};

        Request.OffloadContext = OffloadContext;

        KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
        XdpLwfOffloadQueueWorkItem(
            Filter, &Request.WorkItem, XdpLwfOffloadRssCloseInterfaceOffloadHandleWorker);
        KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, NULL);

        OffloadContext->Settings.Rss = NULL;
    }
}

NTSTATUS
XdpLwfOffloadRssInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _Out_ XDP_OID_ACTION *Action,
    _Out_ NDIS_STATUS *CompletionStatus
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    UINT32 NdisRssParamsLength;

    TraceEnter(TRACE_LWF, "Filter=%p Oid=%x", Filter, Request->DATA.Oid);

    *Action = XdpOidActionPass;
    *CompletionStatus = NDIS_STATUS_SUCCESS;

    if (Request->DATA.Oid != OID_GEN_RECEIVE_SCALE_PARAMETERS) {
        goto Exit;
    }

    if (Request->RequestType == NdisRequestSetInformation) {

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
    } else if (Request->RequestType == NdisRequestQueryInformation) {

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

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
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
