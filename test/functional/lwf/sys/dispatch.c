//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "dispatch.tmh"

DEVICE_OBJECT *XdpFnLwfDeviceObject;

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSYSAPI
BOOLEAN
NTAPI
RtlEqualString(
    _In_ const STRING * String1,
    _In_ const STRING * String2,
    _In_ BOOLEAN CaseInSensitive
    );

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
LwfIrpCreate(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    FILE_FULL_EA_INFORMATION *EaBuffer;
    XDPFNLWF_OPEN_PACKET *OpenPacket = NULL;
    UCHAR Disposition = 0;
    STRING ExpectedEaName;
    STRING ActualEaName = {0};
    FILE_CREATE_ROUTINE *CreateRoutine = NULL;

#ifdef _WIN64
    if (IoIs32bitProcess(Irp)) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }
#endif

    EaBuffer = Irp->AssociatedIrp.SystemBuffer;

    if (EaBuffer == NULL) {
        return STATUS_SUCCESS;
    }

    if (EaBuffer->NextEntryOffset != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Disposition = (UCHAR)(IrpSp->Parameters.Create.Options >> 24);

    ActualEaName.MaximumLength = EaBuffer->EaNameLength;
    ActualEaName.Length = EaBuffer->EaNameLength;
    ActualEaName.Buffer = EaBuffer->EaName;

    RtlInitString(&ExpectedEaName, XDPFNLWF_OPEN_PACKET_NAME);
    if (!RtlEqualString(&ExpectedEaName, &ActualEaName, FALSE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (EaBuffer->EaValueLength < sizeof(XDPFNLWF_OPEN_PACKET)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    OpenPacket = (XDPFNLWF_OPEN_PACKET *)(EaBuffer->EaName + EaBuffer->EaNameLength + 1);

    switch (OpenPacket->ObjectType) {
    case XDPFNLWF_FILE_TYPE_DEFAULT:
        CreateRoutine = DefaultIrpCreate;
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status =
        CreateRoutine(
            Irp, IrpSp, Disposition, OpenPacket + 1,
            EaBuffer->EaValueLength - sizeof(XDPFNLWF_OPEN_PACKET));

    if (NT_SUCCESS(Status)) {
        ASSERT(IrpSp->FileObject->FsContext != NULL);
        ASSERT(((FILE_OBJECT_HEADER *)IrpSp->FileObject->FsContext)->Dispatch != NULL);
    }

Exit:

    ASSERT(Status != STATUS_PENDING);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
LwfIrpCleanup(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    FILE_OBJECT_HEADER *FileHeader = IrpSp->FileObject->FsContext;

    if (FileHeader != NULL && FileHeader->Dispatch->Cleanup != NULL) {
        return FileHeader->Dispatch->Cleanup(Irp, IrpSp);
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
LwfIrpClose(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    FILE_OBJECT_HEADER *FileHeader = IrpSp->FileObject->FsContext;

    if (FileHeader != NULL && FileHeader->Dispatch->Close != NULL) {
        return FileHeader->Dispatch->Close(Irp, IrpSp);
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
LwfIrpIoctl(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    FILE_OBJECT_HEADER *FileHeader;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    Irp->IoStatus.Information = 0;
    FileHeader = IrpSp->FileObject->FsContext;

    if (FileHeader != NULL && FileHeader->Dispatch->IoControl != NULL) {
        Status = FileHeader->Dispatch->IoControl(Irp, IrpSp);
    }

    return Status;
}

static
_Function_class_(DRIVER_DISPATCH)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
__declspec(code_seg("PAGE"))
NTSTATUS
IrpIoDispatch(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp
    )
{
    IO_STACK_LOCATION *IrpSp;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(DeviceObject == XdpFnLwfDeviceObject);
    PAGED_CODE();

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MajorFunction) {
        case IRP_MJ_CREATE:
        Status = LwfIrpCreate(Irp, IrpSp);
        break;

    case IRP_MJ_CLEANUP:
        Status = LwfIrpCleanup(Irp, IrpSp);
        break;

    case IRP_MJ_CLOSE:
        Status = LwfIrpClose(Irp, IrpSp);
        break;

    case IRP_MJ_DEVICE_CONTROL:
        Status = LwfIrpIoctl(Irp, IrpSp);
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

static
VOID
DriverUnload(
    DRIVER_OBJECT *DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    FilterStop();

    if (XdpFnLwfDeviceObject != NULL) {
        IoDeleteDevice(XdpFnLwfDeviceObject);
        XdpFnLwfDeviceObject = NULL;
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
    _In_ UNICODE_STRING *RegistryPath
    )
{
    NTSTATUS Status;
    UNICODE_STRING DeviceName;

#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    RtlInitUnicodeString(&DeviceName, XDPFNLWF_DEVICE_NAME);

    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    Status =
        IoCreateDevice(
            DriverObject,
            0,
            &DeviceName,
            FILE_DEVICE_NETWORK,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &XdpFnLwfDeviceObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

#pragma warning(push)
#pragma warning(disable:28168) // The function 'IrpIoDispatch' does not have any _Dispatch_type_ annotations.
    DriverObject->MajorFunction[IRP_MJ_CREATE] = IrpIoDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = IrpIoDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IrpIoDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpIoDispatch;
#pragma warning(pop)
    DriverObject->DriverUnload = DriverUnload;

    Status = FilterStart(DriverObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    if (!NT_SUCCESS(Status)) {
        DriverUnload(DriverObject);
    }

    return Status;
}
