//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

static
CONST XDP_VERSION XdpDriverApiVersion = {
    .Major = XDP_DRIVER_API_MAJOR_VER,
    .Minor = XDP_DRIVER_API_MINOR_VER,
    .Patch = XDP_DRIVER_API_PATCH_VER
};

static EX_PUSH_LOCK NativeContextListLock;
static LIST_ENTRY NativeContextList;
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

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpXdpNotify(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    )
{
    UNREFERENCED_PARAMETER(InterfaceQueue);
    UNREFERENCED_PARAMETER(Flags);
}

static
BOOLEAN
NativeAddExclusiveContext(
    NATIVE_CONTEXT *Native,
    UINT32 IfIndex
    )
{
    LIST_ENTRY *Entry;
    BOOLEAN Success = TRUE;

    ExAcquirePushLockShared(&NativeContextListLock);

    Entry = NativeContextList.Flink;
    while (Entry != &NativeContextList) {
        NATIVE_CONTEXT *Candidate = CONTAINING_RECORD(Entry, NATIVE_CONTEXT, ContextListLink);
        Entry = Entry->Flink;

        if (Candidate->Adapter->IfIndex == IfIndex) {
            Success = FALSE;
            break;
        }
    }

    if (Success) {
        InsertTailList(&NativeContextList, &Native->ContextListLink);
    }

    ExReleasePushLockShared(&NativeContextListLock);

    return Success;
}

static
BOOLEAN
NativeRemoveContext(
    NATIVE_CONTEXT *Native
    )
{
    LIST_ENTRY *Entry;
    BOOLEAN Success = FALSE;

    ExAcquirePushLockShared(&NativeContextListLock);

    Entry = NativeContextList.Flink;
    while (Entry != &NativeContextList) {
        NATIVE_CONTEXT *Candidate = CONTAINING_RECORD(Entry, NATIVE_CONTEXT, ContextListLink);
        Entry = Entry->Flink;

        if (Candidate == Native) {
            RemoveEntryList(&Native->ContextListLink);
            Success = TRUE;
            break;
        }
    }

    ExReleasePushLockShared(&NativeContextListLock);

    return Success;
}

NDIS_STATUS
NativeHandleXdpOid(
    _In_ ADAPTER_NATIVE *AdapterNative,
    _Inout_ VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _Out_ UINT *BytesNeeded,
    _Out_ UINT *BytesWritten
    )
{
    NDIS_STATUS Status;
    CONST UINT32 CapabilitiesSize = sizeof(AdapterNative->Capabilities);

    *BytesNeeded = 0;
    *BytesWritten = 0;

    if (InformationBufferLength < CapabilitiesSize) {
        *BytesNeeded = CapabilitiesSize;
        Status = NDIS_STATUS_BUFFER_TOO_SHORT;
    } else {
        RtlCopyMemory(
            InformationBuffer,
            &AdapterNative->Capabilities,
            CapabilitiesSize);
        *BytesWritten = CapabilitiesSize;
        Status = NDIS_STATUS_SUCCESS;
    }

    return Status;
}

VOID
NativeAdapterCleanup(
    _In_ ADAPTER_NATIVE *AdapterNative
    )
{
    ExFreePoolWithTag(AdapterNative, POOLTAG_NATIVE);
}

ADAPTER_NATIVE *
NativeAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    ADAPTER_NATIVE *AdapterNative;
    NTSTATUS Status;

    AdapterNative = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*AdapterNative), POOLTAG_NATIVE);
    if (AdapterNative == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = XdpInitializeCapabilities(&AdapterNative->Capabilities, &XdpDriverApiVersion);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    AdapterNative->Adapter = Adapter;

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (AdapterNative != NULL) {
            NativeAdapterCleanup(AdapterNative);
            AdapterNative = NULL;
        }
    }

    return AdapterNative;
}

static
NTSTATUS
NativeIrpXdpRegister(
    _In_ NATIVE_CONTEXT *Native,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    ExAcquirePushLockExclusive(&Native->Lock);

    if (Native->XdpRegistration != NULL) {
        Status = STATUS_INVALID_DEVICE_STATE;
    } else {
        Status =
            XdpRegisterInterface(
                Native->Adapter->IfIndex,
                &Native->Adapter->Native->Capabilities,
                Native, &MpXdpDispatch, &Native->XdpRegistration);
    }

    ExReleasePushLockExclusive(&Native->Lock);

    return Status;
}

static
NTSTATUS
NativeIrpXdpDeregister(
    _In_ NATIVE_CONTEXT *Native,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    ExAcquirePushLockExclusive(&Native->Lock);

    if (Native->XdpRegistration == NULL) {
        Status = STATUS_INVALID_DEVICE_STATE;
    } else {
        XdpDeregisterInterface(Native->XdpRegistration);
        Native->XdpRegistration = NULL;
        Status = STATUS_SUCCESS;
    }

    ExReleasePushLockExclusive(&Native->Lock);

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NativeIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NATIVE_CONTEXT *Native = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_XDP_REGISTER:
        Status = NativeIrpXdpRegister(Native, Irp, IrpSp);
        break;
    case IOCTL_XDP_DEREGISTER:
        Status = NativeIrpXdpDeregister(Native, Irp, IrpSp);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
NativeCleanup(
    _In_ NATIVE_CONTEXT *Native
    )
{
    if (Native->XdpRegistration != NULL) {
        XdpDeregisterInterface(Native->XdpRegistration);
    }

    if (Native->Adapter != NULL) {
        MpDereferenceAdapter(Native->Adapter);
    }

    NativeRemoveContext(Native);

    ExFreePoolWithTag(Native, POOLTAG_NATIVE);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NativeIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    UNREFERENCED_PARAMETER(Irp);

    NativeCleanup(IrpSp->FileObject->FsContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH NativeFileDispatch = {
    .IoControl = NativeIrpDeviceIoControl,
    .Close = NativeIrpClose,
};

NTSTATUS
NativeIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    NATIVE_CONTEXT *Native = NULL;
    XDPFNMP_OPEN_NATIVE *OpenNative;
    UINT32 IfIndex;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenNative)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenNative = InputBuffer;
    IfIndex = OpenNative->IfIndex;

    Native = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Native), POOLTAG_NATIVE);
    if (Native == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Native->Header.ObjectType = XDPFNMP_FILE_TYPE_NATIVE;
    Native->Header.Dispatch = &NativeFileDispatch;

    Native->Adapter = MpFindAdapter(IfIndex);
    if (Native->Adapter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (!NativeAddExclusiveContext(Native, IfIndex)) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    IrpSp->FileObject->FsContext = Native;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Native != NULL) {
            NativeCleanup(Native);
        }
    }

    return Status;
}

NDIS_STATUS
MpNativeStart(
    VOID
    )
{
    ExInitializePushLock(&NativeContextListLock);
    InitializeListHead(&NativeContextList);

    return NDIS_STATUS_SUCCESS;
}

VOID
MpNativeCleanup(
    VOID
    )
{
    FRE_ASSERT(IsListEmpty(&NativeContextList));
}
