//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements interface file object routines.
//

#include "precomp.h"
#include "offload.tmh"

static XDP_FILE_IRP_ROUTINE XdpIrpInterfaceDeviceIoControl;
static XDP_FILE_IRP_ROUTINE XdpIrpInterfaceClose;
static XDP_FILE_DISPATCH XdpInterfaceFileDispatch = {
    .IoControl  = XdpIrpInterfaceDeviceIoControl,
    .Close = XdpIrpInterfaceClose,
};

static
NTSTATUS
XdpIrpInterfaceOffloadRssGetCapabilities(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_RSS_CAPABILITIES RssCapabilities;
    UINT32 RssCapabilitiesSize = sizeof(RssCapabilities);
    VOID *OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

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
        XdpIfGetInterfaceOffloadCapabilities(
            InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle,
            XdpOffloadRss, &RssCapabilities, &RssCapabilitiesSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = RssCapabilitiesSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBufferLength < RssCapabilitiesSize) {
        TraceError(
            TRACE_CORE,
            "Interface=%p Output buffer length too small OutputBufferLength=%llu RequiredSize=%u",
            InterfaceObject, (UINT64)OutputBufferLength, RssCapabilitiesSize);
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    RtlCopyMemory(OutputBuffer, &RssCapabilities, RssCapabilitiesSize);
    *BytesReturned = RssCapabilitiesSize;

Exit:

    TraceInfo(
        TRACE_CORE, "Interface=%p Status=%!STATUS! OutputBuffer=%!HEXDUMP!",
        InterfaceObject, Status, WppHexDump(OutputBuffer, OutputBufferLength));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
XdpIrpInterfaceOffloadRssGet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_RSS_CONFIGURATION *RssConfiguration;
    XDP_OFFLOAD_PARAMS_RSS RssParams;
    UINT32 RssParamsSize = sizeof(RssParams);
    UINT32 RequiredSize;
    VOID *OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

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
            InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle,
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

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = RequiredSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBufferLength < RequiredSize) {
        TraceError(
            TRACE_CORE,
            "Interface=%p Output buffer length too small OutputBufferLength=%llu RequiredSize=%u",
            InterfaceObject, (UINT64)OutputBufferLength, RequiredSize);
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    *BytesReturned = RequiredSize;

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

Exit:

    TraceInfo(
        TRACE_CORE, "Interface=%p Status=%!STATUS! OutputBuffer=%!HEXDUMP!",
        InterfaceObject, Status, WppHexDump(OutputBuffer, OutputBufferLength));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
XdpIrpInterfaceOffloadRssSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
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

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

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
            "Interface=%p Input buffer length too small InputBufferLength=%llu",
            InterfaceObject, (UINT64)InputBufferLength);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    RssConfiguration = InputBuffer;

    if (RssConfiguration->Header.Revision < XDP_RSS_CONFIGURATION_REVISION_1 ||
        RssConfiguration->Header.Size < XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1) {
        TraceError(
            TRACE_CORE, "Interface=%p Unsupported revision Revision=%u Size=%u",
            InterfaceObject, RssConfiguration->Header.Revision, RssConfiguration->Header.Size);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if ((RssConfiguration->Flags & ~XDP_RSS_VALID_FLAGS) != 0) {
        TraceError(
            TRACE_CORE, "Interface=%p Invalid flags Flags=%u",
            InterfaceObject, RssConfiguration->Flags);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if ((RssConfiguration->HashType & ~XDP_RSS_VALID_HASH_TYPES) != 0) {
        TraceError(
            TRACE_CORE, "Interface=%p Invalid hash type HashType=%u",
            InterfaceObject, RssConfiguration->HashType);
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
            "Interface=%p Hash secret key overflow HashSecretKeyOffset=%u HashSecretKeySize=%u",
            InterfaceObject, RssConfiguration->HashSecretKeyOffset, RssConfiguration->HashSecretKeySize);
        goto Exit;
    }

    Status =
        RtlUInt32Add(
            RssConfiguration->IndirectionTableOffset,
            RssConfiguration->IndirectionTableSize, &Size2);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "Interface=%p Indirection table overflow IndirectionTableOffset=%u IndirectionTableSize=%u",
            InterfaceObject, RssConfiguration->IndirectionTableOffset, RssConfiguration->IndirectionTableSize);
        goto Exit;
    }

    if (InputBufferLength < max(Size1, Size2)) {
        TraceError(
            TRACE_CORE,
            "Interface=%p Input buffer length too small InputBufferLength=%llu RequiredSize=%u",
            InterfaceObject, (UINT64)InputBufferLength, max(Size1, Size2));
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (RssConfiguration->Flags & XDP_RSS_FLAG_SET_HASH_SECRET_KEY) {
        if (RssConfiguration->HashSecretKeySize > sizeof(RssParams.HashSecretKey)) {
            TraceError(
                TRACE_CORE,
                "Interface=%p Hash secret key size too large HashSecretKeySize=%u",
                InterfaceObject, RssConfiguration->HashSecretKeySize);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        HashSecretKey = RTL_PTR_ADD(RssConfiguration, RssConfiguration->HashSecretKeyOffset);
    }

    if (RssConfiguration->Flags & XDP_RSS_FLAG_SET_INDIRECTION_TABLE) {
        if (RssConfiguration->IndirectionTableSize > sizeof(RssParams.IndirectionTable)) {
            TraceError(
                TRACE_CORE,
                "Interface=%p Indirection table size too large IndirectionTableSize=%u",
                InterfaceObject, RssConfiguration->IndirectionTableSize);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (RssConfiguration->IndirectionTableSize % sizeof(RssParams.IndirectionTable[0]) != 0 ||
            !RTL_IS_POWER_OF_TWO(RssConfiguration->IndirectionTableSize)) {
            TraceError(
                TRACE_CORE,
                "Interface=%p Invalid indirection table size IndirectionTableSize=%u",
                InterfaceObject, RssConfiguration->IndirectionTableSize);
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
            InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle,
            XdpOffloadRss, &RssParams, sizeof(RssParams));

Exit:

    TraceInfo(
        TRACE_CORE, "Interface=%p Status=%!STATUS! InputBuffer=%!HEXDUMP!",
        InterfaceObject, Status, WppHexDump(InputBuffer, InputBufferLength));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpOffloadInitializeIfSettings(
    _Out_ XDP_OFFLOAD_IF_SETTINGS *OffloadIfSettings
    )
{
    XdpOffloadQeoInitializeSettings(&OffloadIfSettings->Qeo);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateInterface(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    CONST XDP_INTERFACE_OPEN *Params = NULL;
    XDP_IFSET_HANDLE IfSetHandle = NULL;
    XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle = NULL;
    XDP_INTERFACE_OBJECT *InterfaceObject = NULL;
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

    IfSetHandle = XdpIfFindAndReferenceIfSet(Params->IfIndex, &HookId, 1, NULL);
    if (IfSetHandle == NULL) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Failed to find interface set", Params->IfIndex);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status = XdpIfOpenInterfaceOffloadHandle(IfSetHandle, &HookId, &InterfaceOffloadHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    InterfaceObject =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*InterfaceObject), XDP_POOLTAG_INTERFACE);
    if (InterfaceObject == NULL) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Failed to allocate interface object", Params->IfIndex);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    InterfaceObject->Header.ObjectType = XDP_OBJECT_TYPE_INTERFACE;
    InterfaceObject->Header.Dispatch = &XdpInterfaceFileDispatch;
    InterfaceObject->IfSetHandle = IfSetHandle;
    InterfaceObject->InterfaceOffloadHandle = InterfaceOffloadHandle;
    IrpSp->FileObject->FsContext = InterfaceObject;
    IfSetHandle = NULL;
    InterfaceOffloadHandle = NULL;
    InterfaceObject = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (InterfaceObject != NULL) {
        ExFreePoolWithTag(InterfaceObject, XDP_POOLTAG_INTERFACE);
    }
    if (InterfaceOffloadHandle != NULL) {
        XdpIfCloseInterfaceOffloadHandle(IfSetHandle, InterfaceOffloadHandle);
    }
    if (IfSetHandle != NULL) {
        XdpIfDereferenceIfSet(IfSetHandle);
    }

    if (Params != NULL) {
        TraceInfo(
            TRACE_CORE,
            "Interface=%p create IfIndex=%u Status=%!STATUS!",
            InterfaceObject, Params->IfIndex, Status);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpOffloadRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    )
{
    XdpOffloadQeoRevertSettings(IfSetHandle, InterfaceOffloadHandle);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpInterfaceClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_INTERFACE_OBJECT *InterfaceObject = IrpSp->FileObject->FsContext;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

    UNREFERENCED_PARAMETER(Irp);

    ASSERT(InterfaceObject->IfSetHandle != NULL);
    ASSERT(InterfaceObject->InterfaceOffloadHandle != NULL);

    XdpIfCloseInterfaceOffloadHandle(
        InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle);
    XdpIfDereferenceIfSet(InterfaceObject->IfSetHandle);

    TraceInfo(TRACE_CORE, "Interface=%p delete", InterfaceObject);

    ExFreePoolWithTag(InterfaceObject, XDP_POOLTAG_INTERFACE);

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
XdpIrpInterfaceDeviceIoControl(
    IRP *Irp,
    IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    XDP_INTERFACE_OBJECT *InterfaceObject = IrpSp->FileObject->FsContext;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

    Irp->IoStatus.Information = 0;

    switch (IoControlCode) {
    case IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES:
        Status = XdpIrpInterfaceOffloadRssGetCapabilities(InterfaceObject, Irp, IrpSp);
        break;
    case IOCTL_INTERFACE_OFFLOAD_RSS_GET:
        Status = XdpIrpInterfaceOffloadRssGet(InterfaceObject, Irp, IrpSp);
        break;
    case IOCTL_INTERFACE_OFFLOAD_RSS_SET:
        Status =
            XdpIrpInterfaceOffloadRssSet(
                InterfaceObject, Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        break;
    case IOCTL_INTERFACE_OFFLOAD_QEO_SET:
        Status = XdpIrpInterfaceOffloadQeoSet(InterfaceObject, Irp, IrpSp);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    TraceInfo(TRACE_CORE, "Interface=%p Ioctl=%u Status=%!STATUS!", InterfaceObject, IoControlCode, Status);

    TraceExitStatus(TRACE_CORE);

    return Status;
}
