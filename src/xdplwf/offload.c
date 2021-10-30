//
// Copyright (C) Microsoft Corporation. All rights reserved.
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

CONST XDP_OFFLOAD_DISPATCH XdpLwfOffloadDispatch = {
    .OpenInterfaceOffloadHandle = XdpLwfOpenInterfaceOffloadHandle,
    .GetInterfaceOffload = XdpLwfGetInterfaceOffload,
    .SetInterfaceOffload = XdpLwfSetInterfaceOffload,
    .CloseInterfaceOffloadHandle = XdpLwfCloseInterfaceOffloadHandle,
};

static
USHORT
XdpToNdisRssFlags(
    _In_ UINT32 XdpRssFlags
    )
{
    USHORT NdisRssFlags = 0;

    if (XdpRssFlags & XDP_RSS_FLAG_HASH_TYPE_UNCHANGED) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED;
    }
    if (XdpRssFlags & XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED;
    }
    if (XdpRssFlags & XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED) {
        NdisRssFlags |= NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED;
    }

    return NdisRssFlags;
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
VOID
InheritXdpRssParams(
    _Inout_ XDP_OFFLOAD_PARAMS_RSS *XdpRssParams,
    _In_opt_ CONST XDP_OFFLOAD_PARAMS_RSS *InheritedXdpRssParams
    )
{
    if (InheritedXdpRssParams != NULL) {
        ASSERT((InheritedXdpRssParams->Flags & XDP_RSS_FLAG_HASH_TYPE_UNCHANGED) == 0);
        ASSERT((InheritedXdpRssParams->Flags & XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED) == 0);
        ASSERT((InheritedXdpRssParams->Flags & XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED) == 0);

        if (XdpRssParams->Flags & XDP_RSS_FLAG_HASH_TYPE_UNCHANGED) {
            XdpRssParams->HashType = InheritedXdpRssParams->HashType;
            XdpRssParams->Flags &= ~XDP_RSS_FLAG_HASH_TYPE_UNCHANGED;
        }
        if (XdpRssParams->Flags & XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED) {
            XdpRssParams->HashSecretKeySize = InheritedXdpRssParams->HashSecretKeySize;
            RtlCopyMemory(
                &XdpRssParams->HashSecretKey, &InheritedXdpRssParams->HashSecretKey,
                InheritedXdpRssParams->HashSecretKeySize);
            XdpRssParams->Flags &= ~XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED;
        }
        if (XdpRssParams->Flags & XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED) {
            XdpRssParams->IndirectionTableSize = InheritedXdpRssParams->IndirectionTableSize;
            RtlCopyMemory(
                &XdpRssParams->IndirectionTable, &InheritedXdpRssParams->IndirectionTable,
                InheritedXdpRssParams->IndirectionTableSize);
            XdpRssParams->Flags &= ~XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED;
        }
    } else {
        ASSERT(
            (XdpRssParams->Flags &
            (XDP_RSS_FLAG_HASH_TYPE_UNCHANGED |
                XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED |
                XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED)) == 0);
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
CreateXdpRssParamsFromNdisRssParams(
    _In_ CONST NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams,
    _In_ UINT32 NdisRssParamsLength,
    _Out_ XDP_OFFLOAD_PARAMS_RSS **XdpRssParamsOut
    )
{
    NTSTATUS Status;
    XDP_OFFLOAD_PARAMS_RSS *XdpRssParams;

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

    //
    // TODO: pre-allocate params buffer during init.
    //
    XdpRssParams = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*XdpRssParams), POOLTAG_OFFLOAD);
    if (XdpRssParams == NULL) {
        TraceError(TRACE_LWF, "Failed to allocate XDP RSS params");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    if (XdpLwfOffloadIsNdisRssEnabled(NdisRssParams)) {
        PROCESSOR_NUMBER *NdisIndirectionTable;
        UCHAR *NdisHashSecretKey;

        ASSERT(NdisRssParams->IndirectionTableSize <= sizeof(XdpRssParams->IndirectionTable));
        ASSERT(NdisRssParams->HashSecretKeySize <= sizeof(XdpRssParams->HashSecretKey));

        XdpRssParams->State = XdpOffloadStateEnabled;
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

    *XdpRssParamsOut = XdpRssParams;
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
NTSTATUS
XdpLwfOffloadRssGet(
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _Inout_ UINT32 *RssParamsLength
    )
{
    NTSTATUS Status;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;
    XDP_OFFLOAD_PARAMS_RSS *XdpRssParams;

    ASSERT(RssParams != NULL);
    ASSERT(*RssParamsLength == sizeof(*RssParams));

    *RssParamsLength = 0;

    //
    // Determine the appropriate edge.
    //
    switch (OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        if (Filter->Offload.LowerEdge.Rss != NULL) {
            XdpRssParams = Filter->Offload.LowerEdge.Rss;
        } else {
            //
            // No lower edge state implies lower and upper edge state are the same.
            //
            XdpRssParams = Filter->Offload.UpperEdge.Rss;
        }
        break;
    case XdpOffloadEdgeUpper:
        XdpRssParams = Filter->Offload.UpperEdge.Rss;
        break;
    default:
        ASSERT(FALSE);
        XdpRssParams = NULL;
        break;
    }

    if (XdpRssParams == NULL) {
        //
        // RSS not initialized yet.
        //
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p RSS params not found", OffloadContext);
        Status = STATUS_INVALID_DEVICE_STATE;
    } else {
        RtlCopyMemory(RssParams, XdpRssParams, sizeof(*XdpRssParams));
        *RssParamsLength = sizeof(*XdpRssParams);
        Status = STATUS_SUCCESS;
    }

    return Status;
}

static
NTSTATUS
XdpLwfOffloadRssSet(
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _In_ UINT32 RssParamsLength
    )
{
    NTSTATUS Status;
    XDP_LWF_FILTER *Filter = OffloadContext->Filter;
    ULONG *BitmapBuffer = NULL;
    XDP_OFFLOAD_PARAMS_RSS *CurrentXdpRssParams;
    XDP_OFFLOAD_PARAMS_RSS *OldXdpRssParams = NULL;
    XDP_OFFLOAD_PARAMS_RSS *XdpRssParams = NULL;
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;
    UINT32 NdisRssParamsLength;
    ULONG BytesReturned;

    ASSERT(RssParams != NULL);
    ASSERT(RssParamsLength == sizeof(*RssParams));

    //
    // Determine the appropriate edge.
    //
    switch (OffloadContext->Edge) {
    case XdpOffloadEdgeLower:
        CurrentXdpRssParams = Filter->Offload.LowerEdge.Rss;
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
        CurrentXdpRssParams = NULL;
        break;
    }

    //
    // Ensure compatibility with existing settings. Currently, only allow
    // overwrite of a configuration plumbed by the same offload context.
    //
    if (CurrentXdpRssParams != NULL) {
        if (CurrentXdpRssParams != OffloadContext->Settings.Rss) {
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
        CurrentXdpRssParams = Filter->Offload.UpperEdge.Rss;
    }

    //
    // Validate input.
    //
    if ((RssParams->Flags & XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED) == 0) {
        UINT32 ProcessorCount = 0;
        CONST UINT32 NumEntries =
            RssParams->IndirectionTableSize / sizeof(RssParams->IndirectionTable[0]);
        ULONG BufferSize =
            ALIGN_UP_BY(
                (KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS) + 7) / 8, sizeof(ULONG));
        RTL_BITMAP ProcessorBitmap;

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

        if (ProcessorCount > Filter->Offload.Rss.MaxReceiveQueueCount) {
            TraceError(
                TRACE_LWF,
                "OffloadContext=%p Too many processors ProcessorCount=%u MaxReceiveQueueCount=%u",
                OffloadContext, ProcessorCount, Filter->Offload.Rss.MaxReceiveQueueCount);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
    }

    //
    // Clone the input params.
    //
    XdpRssParams = ExAllocatePoolZero(NonPagedPoolNx, RssParamsLength, POOLTAG_OFFLOAD);
    if (XdpRssParams == NULL) {
        TraceError(
            TRACE_LWF, "OffloadContext=%p Failed to allocate XDP RSS params", OffloadContext);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    RtlCopyMemory(XdpRssParams, RssParams, RssParamsLength);

    //
    // Inherit from the current RSS settings to support *UNCHANGED flags.
    //
    InheritXdpRssParams(XdpRssParams, CurrentXdpRssParams);

    //
    // Form and issue the OID.
    //

    Status = CreateNdisRssParamsFromXdpRssParams(XdpRssParams, &NdisRssParams, &NdisRssParamsLength);
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

    //
    // Update the generic state.
    //
    Status = XdpGenericRssUpdateIndirection(&Filter->Generic, NdisRssParams, NdisRssParamsLength);
    // TODO: Need to guarantee both miniport and generic are in the same state.
    ASSERT(NT_SUCCESS(Status));

    //
    // Update the tracking RSS state for the edge.
    //

    OldXdpRssParams = InterlockedExchangePointer(&Filter->Offload.LowerEdge.Rss, XdpRssParams);
    OffloadContext->Settings.Rss = XdpRssParams;
    XdpRssParams = NULL;

    TraceInfo(TRACE_LWF, "Filter=%p updated lower edge RSS settings", Filter);

Exit:

    if (BitmapBuffer != NULL) {
         ExFreePoolWithTag(BitmapBuffer, POOLTAG_OFFLOAD);
    }

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    if (XdpRssParams != NULL) {
        ExFreePoolWithTag(XdpRssParams, POOLTAG_OFFLOAD);
    }

    if (OldXdpRssParams != NULL) {
        ExFreePoolWithTag(OldXdpRssParams, POOLTAG_OFFLOAD);
    }

    return Status;
}

static
NTSTATUS
XdpLwfOffloadRssUpdate(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams,
    _In_ ULONG NdisRssParamsLength
    )
{
    NTSTATUS Status;
    XDP_OFFLOAD_PARAMS_RSS *XdpRssParams = NULL;
    XDP_OFFLOAD_PARAMS_RSS *OldXdpRssParams = NULL;

    Status = CreateXdpRssParamsFromNdisRssParams(NdisRssParams, NdisRssParamsLength, &XdpRssParams);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Inherit from the current upper edge settings to support *UNCHANGED flags.
    //
    InheritXdpRssParams(XdpRssParams, Filter->Offload.UpperEdge.Rss);

    OldXdpRssParams = InterlockedExchangePointer(&Filter->Offload.UpperEdge.Rss, XdpRssParams);
    XdpRssParams = NULL;
    Status = STATUS_SUCCESS;

    TraceInfo(TRACE_LWF, "Filter=%p updated upper edge RSS settings", Filter);

Exit:

    if (OldXdpRssParams != NULL) {
        ExFreePoolWithTag(OldXdpRssParams, POOLTAG_OFFLOAD);
    }

    if (XdpRssParams != NULL) {
        ExFreePoolWithTag(XdpRssParams, POOLTAG_OFFLOAD);
    }

    return Status;
}

static
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

    Filter->Offload.Rss.MaxReceiveQueueCount = 1;

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
        Filter->Offload.Rss.MaxReceiveQueueCount = max(1, RssCaps.NumberOfReceiveQueues);
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
VOID
XdpLwfOffloadRssUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    //
    // TODO: Refcounting on upper/lower edge to set offloads based on upper edge settings.
    //
    if (Filter->Offload.UpperEdge.Rss != NULL) {
        ExFreePoolWithTag(Filter->Offload.UpperEdge.Rss, POOLTAG_OFFLOAD);
        Filter->Offload.UpperEdge.Rss = NULL;
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

    OffloadContext =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*OffloadContext), POOLTAG_OFFLOAD);
    if (OffloadContext == NULL) {
        TraceError(
            TRACE_LWF,
            "Filter=%p Failed to allocate interface offload context", Filter);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    InsertTailList(&Filter->Offload.InterfaceOffloadHandleListHead, &OffloadContext->Link);
    OffloadContext->Filter = Filter;
    OffloadContext->Edge = XdpLwfConvertHookIdToOffloadEdge(HookId);
    *InterfaceOffloadHandle = OffloadContext;
    OffloadContext = NULL;
    Status = STATUS_SUCCESS;

    TraceVerbose(TRACE_LWF, "Filter=%p OffloadContext=%p Created", Filter, OffloadContext);

Exit:

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
    NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams = NULL;

    TraceEnter(TRACE_LWF, "OffloadContext=%p", OffloadContext);

    if (OffloadContext->IsInvalid) {
        goto Exit;
    }

    RemoveEntryList(&OffloadContext->Link);

    //
    // Revert the handle's configured offloads.
    //

    if (OffloadContext->Settings.Rss != NULL) {

        //
        // Clear the lower edge setting to imply lower edge has no independent settings.
        //
        ASSERT(Filter->Offload.LowerEdge.Rss == OffloadContext->Settings.Rss);
        InterlockedExchangePointer(&Filter->Offload.LowerEdge.Rss, NULL);

        if (Filter->Offload.UpperEdge.Rss != NULL) {
            NTSTATUS Status;
            XDP_OFFLOAD_PARAMS_RSS *XdpRssParams;
            UINT32 NdisRssParamsLength;
            ULONG BytesReturned;

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

            XdpRssParams = Filter->Offload.UpperEdge.Rss;

            InheritXdpRssParams(XdpRssParams, NULL);

            Status =
                CreateNdisRssParamsFromXdpRssParams(
                    XdpRssParams, &NdisRssParams, &NdisRssParamsLength);
            if (!NT_SUCCESS(Status)) {
                TraceError(
                    TRACE_LWF,
                    "OffloadContext=%p Failed to create NDIS RSS params Status=%!STATUS!",
                    OffloadContext, Status);
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
                ASSERT(FALSE);
                goto Exit;
            }

            //
            // Update the generic state.
            //
            Status = XdpGenericRssUpdateIndirection(&Filter->Generic, NdisRssParams, NdisRssParamsLength);
            if (!NT_SUCCESS(Status)) {
                TraceError(
                    TRACE_LWF,
                    "OffloadContext=%p Failed to update generic indirection table Status=%!STATUS!",
                    OffloadContext, Status);
                ASSERT(FALSE);
            }
        } else {
            //
            // Upper edge RSS is not initialized.
            //
            ASSERT(FALSE);
        }
    }

Exit:

    TraceVerbose(TRACE_LWF, "OffloadContext=%p Deleted", OffloadContext);

    if (NdisRssParams != NULL) {
        ExFreePoolWithTag(NdisRssParams, POOLTAG_OFFLOAD);
    }

    if (OffloadContext->Settings.Rss != NULL) {
        ExFreePoolWithTag(OffloadContext->Settings.Rss, POOLTAG_OFFLOAD);
    }

    ExFreePoolWithTag(OffloadContext, POOLTAG_OFFLOAD);

    TraceExit(TRACE_LWF);
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

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

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
        Status = XdpLwfOffloadRssGet(OffloadContext, OffloadParams, OffloadParamsSize);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

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

    TraceEnter(TRACE_LWF, "OffloadContext=%p OffloadType=%u", OffloadContext, OffloadType);

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
        Status = XdpLwfOffloadRssSet(OffloadContext, OffloadParams, OffloadParamsSize);
        break;
    default:
        TraceError(TRACE_LWF, "OffloadContext=%p Unsupported offload", OffloadContext);
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

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

    *Action = XdpOidActionPass;
    *CompletionStatus = NDIS_STATUS_SUCCESS;

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
        Request->DATA.SET_INFORMATION.Oid == OID_GEN_RECEIVE_SCALE_PARAMETERS) {
        //
        // TODO: Complete the OID and return upper edge settings, if any.
        //
        ASSERT(FALSE);
    }

    Status = STATUS_SUCCESS;

Exit:

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

    InitializeListHead(&Filter->Offload.InterfaceOffloadHandleListHead);

    XdpLwfOffloadRssInitialize(Filter);
}

VOID
XdpLwfOffloadUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    )
{
    //
    // Invalidate all interface offload handles.
    //
    // TODO: Tie offload handle validity to XDPIF interface set lifetime and
    //       notify XDPIF interface set clients upon interface set deletion.
    //
    while (!IsListEmpty(&Filter->Offload.InterfaceOffloadHandleListHead)) {
        XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext =
            CONTAINING_RECORD(Filter->Offload.InterfaceOffloadHandleListHead.Flink, XDP_LWF_INTERFACE_OFFLOAD_CONTEXT, Link);

        RemoveEntryList(&OffloadContext->Link);
        InitializeListHead(&OffloadContext->Link);
        OffloadContext->IsInvalid = TRUE;
        OffloadContext->Filter = NULL;

        //
        // TODO: Revert offload settings to the upper edge settings.
        //
    }

    XdpLwfOffloadRssUnInitialize(Filter);
}
