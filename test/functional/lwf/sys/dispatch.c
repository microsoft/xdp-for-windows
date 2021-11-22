//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "dispatch.tmh"

DEVICE_OBJECT *XdpFnLwfDeviceObject;

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
        Status = STATUS_SUCCESS;
        break;

    case IRP_MJ_CLEANUP:
        Status = STATUS_SUCCESS;
        break;

    case IRP_MJ_CLOSE:
        Status = STATUS_SUCCESS;
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
