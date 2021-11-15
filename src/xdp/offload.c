//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

//
// This module implements RSS file object routines.
//

#include "precomp.h"
#include "offload.tmh"

typedef struct _XDP_RSS_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_BINDING_HANDLE IfHandle;
    VOID *InterfaceOffloadHandle;
} XDP_RSS_OBJECT;

static XDP_FILE_IRP_ROUTINE XdpIrpRssDeviceIoControl;
static XDP_FILE_IRP_ROUTINE XdpIrpRssClose;
static XDP_FILE_DISPATCH XdpRssFileDispatch = {
    .IoControl  = XdpIrpRssDeviceIoControl,
    .Close = XdpIrpRssClose,
};

static
NTSTATUS
XdpIrpRssGet(
    _In_ XDP_RSS_OBJECT *RssObject,
    _Out_opt_ VOID *OutputBuffer,
    _In_ SIZE_T OutputBufferLength,
    _Out_ SIZE_T *BytesReturned
    )
{
    NTSTATUS Status;
    XDP_RSS_CONFIGURATION *RssConfiguration;
    XDP_OFFLOAD_PARAMS_RSS RssParams;
    UINT32 RssParamsSize = sizeof(RssParams);
    UINT32 RequiredSize;

    TraceEnter(TRACE_CORE, "Rss=%p", RssObject);

    //
    // TODO: sync with interface work queue?
    // If we require GENERIC binding and use work queue, we can guarantee sync.
    //

    RtlZeroMemory(OutputBuffer, OutputBufferLength);
    *BytesReturned = 0;

    //
    // Issue the internal request.
    //
    Status =
        XdpIfGetInterfaceOffload(
            RssObject->IfHandle, RssObject->InterfaceOffloadHandle,
            XdpOffloadRss, &RssParams, &RssParamsSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Translate the internal result to a user mode result.
    //

    ASSERT(RssParamsSize == sizeof(RssParams));

    RequiredSize =
        sizeof(*RssConfiguration) + RssParams.HashSecretKeySize +
        RssParams.IndirectionTableSize;

    *BytesReturned = RequiredSize;

    if (OutputBufferLength < RequiredSize) {
        TraceError(
            TRACE_CORE,
            "Rss=%p Output buffer length too small OutputBufferLength=%llu RequiredSize=%u",
            RssObject, (UINT64)OutputBufferLength, RequiredSize);
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBuffer != NULL) {
        UCHAR *HashSecretKey;
        PROCESSOR_NUMBER *IndirectionTable;

        RssConfiguration = OutputBuffer;
        RssConfiguration->Header.Revision = XDP_RSS_CONFIGURATION_REVISION_1;
        RssConfiguration->Header.Size = XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1;
        RssConfiguration->HashType = RssParams.HashType;
        RssConfiguration->HashSecretKeyOffset = sizeof(*RssConfiguration);
        RssConfiguration->HashSecretKeySize = RssParams.HashSecretKeySize;
        RssConfiguration->IndirectionTableOffset =
            RssConfiguration->HashSecretKeyOffset + RssConfiguration->HashSecretKeySize;
        RssConfiguration->IndirectionTableSize = RssParams.IndirectionTableSize;

        HashSecretKey = RTL_PTR_ADD(RssConfiguration, RssConfiguration->HashSecretKeyOffset);
        IndirectionTable = RTL_PTR_ADD(RssConfiguration, RssConfiguration->IndirectionTableOffset);

        RtlCopyMemory(HashSecretKey, &RssParams.HashSecretKey, RssParams.HashSecretKeySize);
        RtlCopyMemory(IndirectionTable, &RssParams.IndirectionTable, RssParams.IndirectionTableSize);
    }

Exit:

    TraceInfo(
        TRACE_CORE, "Rss=%p Status=%!STATUS! OutputBuffer=%!HEXDUMP!",
        RssObject, Status, WppHexDump(OutputBuffer, OutputBufferLength));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
XdpIrpRssSet(
    _In_ XDP_RSS_OBJECT *RssObject,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    XDP_RSS_CONFIGURATION *RssConfiguration;
    XDP_OFFLOAD_PARAMS_RSS RssParams = {0};
    UCHAR *HashSecretKey = NULL;
    PROCESSOR_NUMBER *IndirectionTable = NULL;
    UINT32 Size1;
    UINT32 Size2;

    TraceEnter(TRACE_CORE, "Rss=%p", RssObject);

    //
    // TODO: sync with interface work queue?
    // If we require GENERIC binding and use work queue, we can guarantee sync.
    //

    //
    // Validate input.
    //

    if (InputBufferLength < sizeof(*RssConfiguration)) {
        TraceError(
            TRACE_CORE,
            "Rss=%p Input buffer length too small InputBufferLength=%llu",
            RssObject, (UINT64)InputBufferLength);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    RssConfiguration = InputBuffer;

    if (RssConfiguration->Header.Revision < XDP_RSS_CONFIGURATION_REVISION_1 ||
        RssConfiguration->Header.Size < XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1) {
        TraceError(
            TRACE_CORE, "Rss=%p Unsupported revision Revision=%u Size=%u",
            RssObject, RssConfiguration->Header.Revision, RssConfiguration->Header.Size);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if ((RssConfiguration->Flags & ~XDP_RSS_VALID_FLAGS) != 0) {
        TraceError(
            TRACE_CORE, "Rss=%p Invalid flags Flags=%u",
            RssObject, RssConfiguration->Flags);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if ((RssConfiguration->HashType & ~XDP_RSS_VALID_HASH_TYPES) != 0) {
        TraceError(
            TRACE_CORE, "Rss=%p Invalid hash type HashType=%u",
            RssObject, RssConfiguration->HashType);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status =
        RtlUInt32Add(
            RssConfiguration->HashSecretKeyOffset,
            RssConfiguration->HashSecretKeySize, &Size1);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "Rss=%p Hash secret key overflow HashSecretKeyOffset=%u HashSecretKeySize=%u",
            RssObject, RssConfiguration->HashSecretKeyOffset, RssConfiguration->HashSecretKeySize);
        goto Exit;
    }

    Status =
        RtlUInt32Add(
            RssConfiguration->IndirectionTableOffset,
            RssConfiguration->IndirectionTableSize, &Size2);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "Rss=%p Indirection table overflow IndirectionTableOffset=%u IndirectionTableSize=%u",
            RssObject, RssConfiguration->IndirectionTableOffset, RssConfiguration->IndirectionTableSize);
        goto Exit;
    }

    if (InputBufferLength < max(Size1, Size2)) {
        TraceError(
            TRACE_CORE,
            "Rss=%p Input buffer length too small InputBufferLength=%llu RequiredSize=%u",
            RssObject, (UINT64)InputBufferLength, max(Size1, Size2));
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (RssConfiguration->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY) {
        if (RssConfiguration->HashSecretKeySize > sizeof(RssParams.HashSecretKey)) {
            TraceError(
                TRACE_CORE,
                "Rss=%p Hash secret key size too large HashSecretKeySize=%u",
                RssObject, RssConfiguration->HashSecretKeySize);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        HashSecretKey = RTL_PTR_ADD(RssConfiguration, RssConfiguration->HashSecretKeyOffset);
    }

    if (RssConfiguration->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE) {
        if (RssConfiguration->IndirectionTableSize > sizeof(RssParams.IndirectionTable)) {
            TraceError(
                TRACE_CORE,
                "Rss=%p Indirection table size too large IndirectionTableSize=%u",
                RssObject, RssConfiguration->IndirectionTableSize);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (RssConfiguration->IndirectionTableSize % sizeof(RssParams.IndirectionTable[0]) != 0 ||
            !RTL_IS_POWER_OF_TWO(RssConfiguration->IndirectionTableSize)) {
            TraceError(
                TRACE_CORE,
                "Rss=%p Invalid indirection table size IndirectionTableSize=%u",
                RssObject, RssConfiguration->IndirectionTableSize);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        IndirectionTable = RTL_PTR_ADD(RssConfiguration, RssConfiguration->IndirectionTableOffset);
    }


    //
    // Translate the user mode request to an internal request.
    //

    RssConfiguration = InputBuffer;
    RssParams.State = XdpOffloadStateEnabled; // TODO: support disable
    RssParams.Flags = RssConfiguration->Flags;
    RssParams.HashType = RssConfiguration->HashType;
    if (HashSecretKey != NULL) {
        RssParams.HashSecretKeySize = RssConfiguration->HashSecretKeySize;
        RtlCopyMemory(
            &RssParams.HashSecretKey, HashSecretKey, RssConfiguration->HashSecretKeySize);
    }
    if (IndirectionTable != NULL) {
        RssParams.IndirectionTableSize = RssConfiguration->IndirectionTableSize;
        RtlCopyMemory(
            &RssParams.IndirectionTable, IndirectionTable, RssConfiguration->IndirectionTableSize);
    }

    //
    // Issue the internal request to the interface.
    //
    Status =
        XdpIfSetInterfaceOffload(
            RssObject->IfHandle, RssObject->InterfaceOffloadHandle,
            XdpOffloadRss, &RssParams, sizeof(RssParams));

Exit:

    TraceInfo(
        TRACE_CORE, "Rss=%p Status=%!STATUS! InputBuffer=%!HEXDUMP!",
        RssObject, Status, WppHexDump(InputBuffer, InputBufferLength));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateRss(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
#if !DBG
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);
    UNREFERENCED_PARAMETER(Disposition);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    return STATUS_NOT_SUPPORTED;
#else
    NTSTATUS Status;
    CONST XDP_RSS_OPEN *Params = NULL;
    XDP_BINDING_HANDLE BindingHandle = NULL;
    VOID *InterfaceOffloadHandle = NULL;
    XDP_RSS_OBJECT *RssObject = NULL;
    CONST XDP_HOOK_ID HookId = {
        .Layer = XDP_HOOK_L2,
        .Direction = XDP_HOOK_RX,
        .SubLayer = XDP_HOOK_INSPECT,
    };

    UNREFERENCED_PARAMETER(Irp);


    if (Disposition != FILE_CREATE || InputBufferLength < sizeof(*Params)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Params = InputBuffer;

    TraceEnter(TRACE_CORE, "IfIndex=%u", Params->IfIndex);

    BindingHandle =
        XdpIfFindAndReferenceBinding(Params->IfIndex, &HookId, 1, NULL);
    if (BindingHandle == NULL) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Failed to find binding", Params->IfIndex);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status =
        XdpIfOpenInterfaceOffloadHandle(
            BindingHandle, &HookId, &InterfaceOffloadHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RssObject =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RssObject), XDP_POOLTAG_RSS);
    if (RssObject == NULL) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Failed to allocate RSS object", Params->IfIndex);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    RssObject->Header.ObjectType = XDP_OBJECT_TYPE_RSS;
    RssObject->Header.Dispatch = &XdpRssFileDispatch;
    RssObject->IfHandle = BindingHandle;
    RssObject->InterfaceOffloadHandle = InterfaceOffloadHandle;
    IrpSp->FileObject->FsContext = RssObject;
    BindingHandle = NULL;
    InterfaceOffloadHandle = NULL;
    RssObject = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (RssObject != NULL) {
        ExFreePoolWithTag(RssObject, XDP_POOLTAG_RSS);
    }
    if (InterfaceOffloadHandle != NULL) {
        XdpIfCloseInterfaceOffloadHandle(BindingHandle, InterfaceOffloadHandle);
    }
    if (BindingHandle != NULL) {
        XdpIfDereferenceBinding(BindingHandle);
    }

    if (Params != NULL) {
        TraceInfo(
            TRACE_CORE,
            "Rss=%p create IfIndex=%u Status=%!STATUS!",
            RssObject, Params->IfIndex, Status);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
#endif
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpRssClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_RSS_OBJECT *RssObject = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    TraceEnter(TRACE_CORE, "Rss=%p", RssObject);

    ASSERT(RssObject->IfHandle != NULL);
    ASSERT(RssObject->InterfaceOffloadHandle != NULL);

    XdpIfCloseInterfaceOffloadHandle(
        RssObject->IfHandle, RssObject->InterfaceOffloadHandle);
    XdpIfDereferenceBinding(RssObject->IfHandle);

    TraceInfo(TRACE_CORE, "Rss=%p delete", RssObject);

    ExFreePoolWithTag(RssObject, XDP_POOLTAG_RSS);

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
XdpIrpRssDeviceIoControl(
    IRP *Irp,
    IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    XDP_RSS_OBJECT *RssObject = IrpSp->FileObject->FsContext;

    TraceEnter(TRACE_CORE, "Rss=%p", RssObject);

    Irp->IoStatus.Information = 0;

    switch (IoControlCode) {
    case IOCTL_RSS_GET:
        Status =
            XdpIrpRssGet(
                RssObject, Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
                &Irp->IoStatus.Information);
        break;
    case IOCTL_RSS_SET:
        Status =
            XdpIrpRssSet(
                RssObject, Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    TraceInfo(TRACE_CORE, "Rss=%p Ioctl=%u Status=%!STATUS!", RssObject, IoControlCode, Status);

    TraceExitStatus(TRACE_CORE);

    return Status;
}
