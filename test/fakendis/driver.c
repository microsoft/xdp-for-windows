//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "driver.tmh"

typedef struct _FNDIS_EXPORT_ENTRY {
    UNICODE_STRING Name;
    PVOID Function;
} FNDIS_EXPORT_ENTRY;

#pragma warning(push)
#pragma warning(disable:4152) // nonstandard extension, function/data pointer conversion

static const FNDIS_EXPORT_ENTRY FndisExports[] = {
    { RTL_CONSTANT_STRING(L"NdisRegisterPoll"), FNdisRegisterPoll },
    { RTL_CONSTANT_STRING(L"NdisDeregisterPoll"), FNdisDeregisterPoll },
    { RTL_CONSTANT_STRING(L"NdisSetPollAffinity"), FNdisSetPollAffinity },
    { RTL_CONSTANT_STRING(L"NdisRequestPoll"), FNdisRequestPoll },
    { RTL_CONSTANT_STRING(L"NdisGetRoutineAddress"), FNdisGetRoutineAddress },
};

#pragma warning(pop)

PDEVICE_OBJECT FndisDeviceObject;

_IRQL_requires_(PASSIVE_LEVEL)
PVOID
FNdisGetRoutineAddress(
    _In_ PUNICODE_STRING RoutineName
    )
{
    for (SIZE_T Index = 0; Index < RTL_NUMBER_OF(FndisExports); Index++) {
        if (RtlEqualUnicodeString(RoutineName, &FndisExports[Index].Name, FALSE)) {
            return FndisExports[Index].Function;
        }
    }

    return NULL;
}

static
__declspec(code_seg("PAGE"))
_Dispatch_type_(IRP_MJ_CREATE)
NTSTATUS
IrpIoCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static
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

    case IOCTL_FNDIS_POLL_GET_ROUTINE_ADDRESS:
    {
        CONST FNDIS_POLL_GET_ROUTINE_ADDRESS_IN *In;
        FNDIS_POLL_GET_ROUTINE_ADDRESS_OUT *Out;

        if (Irp->RequestorMode != KernelMode) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In) ||
            IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Out)) {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        In = Irp->AssociatedIrp.SystemBuffer;
        Out = Irp->AssociatedIrp.SystemBuffer;

        Out->Routine = FNdisGetRoutineAddress(In->RoutineName);

        Irp->IoStatus.Information = sizeof(*Out);
        Status = STATUS_SUCCESS;
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

static
VOID
DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    NdisPollCpuStop();
    NdisPollStop();
    if (FndisDeviceObject != NULL) {
        IoDeleteDevice(FndisDeviceObject);
        FndisDeviceObject = NULL;
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
    NTSTATUS Status;
    UNICODE_STRING DeviceName;

    UNREFERENCED_PARAMETER(RegistryPath);
#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    RtlInitUnicodeString(&DeviceName, FNDIS_DEVICE_NAME);

    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

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

    DriverObject->MajorFunction[IRP_MJ_CREATE] = IrpIoCreate;
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

    TraceExitStatus(TRACE_CONTROL);

    if (!NT_SUCCESS(Status)) {
        DriverUnload(DriverObject);
    }

    return Status;
}
