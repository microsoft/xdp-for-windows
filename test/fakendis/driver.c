//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

PDEVICE_OBJECT FndisDeviceObject;

_Function_class_(DRIVER_DISPATCH)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
__declspec(code_seg("PAGE"))
NTSTATUS
IrpIoDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    NTSTATUS Status;
    PIO_STACK_LOCATION IrpSp;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

    case IOCTL_FNDIS_POLL_GET_BACKCHANNEL:
    {
        FNDIS_POLL_GET_BACKCHANNEL *Out;

        if (Irp->RequestorMode != KernelMode) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Out)) {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        Out = Irp->AssociatedIrp.SystemBuffer;

        Status = NdisPollGetBackchannel(Irp, (VOID *)&Out->Dispatch);
        break;
    }

    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

VOID
DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

    NdisPollCpuStop();
    NdisPollStop();
    if (FndisDeviceObject != NULL) {
        IoDeleteDevice(FndisDeviceObject);
        FndisDeviceObject = NULL;
    }
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
    NTSTATUS Status;
    UNICODE_STRING DeviceName;

    UNREFERENCED_PARAMETER(RegistryPath);
#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    RtlInitUnicodeString(&DeviceName, FNDIS_DEVICE_NAME);

    Status =
        IoCreateDevice(
            DriverObject,
            0,
            &DeviceName,
            FILE_DEVICE_NETWORK,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &FndisDeviceObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

#pragma warning(push)
#pragma warning(disable:28168) // The function 'IrpIoDeviceControl' does not have a _Dispatch_type_ annotation matching dispatch table position 'IRP_MJ_DEVICE_CONTROL' (0x0e).
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpIoDeviceControl;
#pragma warning(pop)
    DriverObject->DriverUnload = DriverUnload;

    Status = NdisPollStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = NdisPollCpuStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        DriverUnload(DriverObject);
    }

    return Status;
}
