//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <intsafe.h>
#include <ntstrsafe.h>
#include <netioddk.h>
#include <initguid.h>
#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include "cxplat.h"
#include "xskbench.h"
#include "trace.h"
#include "xskbenchdrvioctl.h"
#include "driver.tmh"

typedef struct _XBDRV_NMR_CLIENT_BINDING_CONTEXT {
    NPI Npi;
    HANDLE NmrBindingHandle;
} XBDRV_NMR_CLIENT_BINDING_CONTEXT;

XDP_API_PROVIDER_DISPATCH *XdpApi;
XDP_API_PROVIDER_BINDING_CONTEXT *XdpApiProviderBindingContext;

static DEVICE_OBJECT *XskBenchDrvDeviceObject;
static BOOLEAN IsDeviceOpened;
static BOOLEAN IsSessionActive;
static HANDLE MainThread;
static int Argc;
static char **Argv;
static HANDLE NmrRegistrationHandle;
static KEVENT BoundToProvider;

const NPI_MODULEID NPI_XBDRV_MODULEID = {
    sizeof(NPI_MODULEID),
    MIT_GUID,
    { 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_STATUS
XbDrvXskNotifyCallback(
    _In_ VOID* ClientContext,
    _In_ XSK_NOTIFY_RESULT_FLAGS Result
    )
{
    UNREFERENCED_PARAMETER(ClientContext);
    UNREFERENCED_PARAMETER(Result);
    return STATUS_SUCCESS;
}

static const XDP_API_CLIENT_DISPATCH NmrXdpApiClientDispatch = {
    XbDrvXskNotifyCallback
};

//
// Notify provider attach code.
//
NTSTATUS
XbDrvNmrAttachXdpApiProvider(
    HANDLE NmrBindingHandle,
    PVOID ClientContext,
    PNPI_REGISTRATION_INSTANCE ProviderRegistrationInstance
    )
{
    NTSTATUS Status;
    XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext = NULL;

    UNREFERENCED_PARAMETER(ClientContext);

    TraceEnter(TRACE_CONTROL, "-");

    //
    // Check if this provider interface is suitable.
    //
    if (ProviderRegistrationInstance->Number != XDP_API_VERSION_1) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    // Only support a single provider
    if (XdpApi != NULL) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    //
    // Allocate memory for this binding.
    //
    BindingContext =
        (XBDRV_NMR_CLIENT_BINDING_CONTEXT*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*BindingContext), 'vDbX');
    if (BindingContext == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    BindingContext->NmrBindingHandle = NmrBindingHandle;

    //
    // Attach to the provider.
    //
    Status =
        NmrClientAttachProvider(
            NmrBindingHandle,
            BindingContext,                 // ClientBindingContext
            &NmrXdpApiClientDispatch,       // ClientDispatch
            &BindingContext->Npi.Handle,    // ProviderBindingContext
            &BindingContext->Npi.Dispatch); // ProviderDispatch
    if (!NT_SUCCESS(Status)) {
        ExFreePool(BindingContext);
        goto Exit;
    }

    //
    // The client can now make calls into the provider.
    //
    XdpApi = (XDP_API_PROVIDER_DISPATCH *)BindingContext->Npi.Dispatch;
    XdpApiProviderBindingContext = (XDP_API_PROVIDER_BINDING_CONTEXT *)BindingContext->Npi.Handle;
    KeSetEvent(&BoundToProvider, 0, FALSE);

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

//
// Notify provider detach code.
//
NTSTATUS
XbDrvNmrDetachXdpApiProvider(
    PVOID ClientBindingContext
    )
{
    // XBDRV_NMR_CLIENT_BINDING_CONTEXT *BindingContext =
    //     (XBDRV_NMR_CLIENT_BINDING_CONTEXT *) ClientBindingContext;
    UNREFERENCED_PARAMETER(ClientBindingContext);

    TraceEnter(TRACE_CONTROL, "-");

    //
    // Initiate the closure of all XDPAPI handles.
    //

    // return STATUS_PENDING;

    XdpApiProviderBindingContext = NULL;
    XdpApi = NULL;
    KeResetEvent(&BoundToProvider);

    TraceExitSuccess(TRACE_CONTROL);

    return STATUS_SUCCESS;
}

// VOID
// XbDrvAllXdpApiHandlesAreClosed(
//     XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext
//     )
// {
//     //
//     // Indicate detach completion.
//     //
//     NmrClientDetachProviderComplete(BindingContext->NmrBindingHandle);
// }

VOID
XbDrvNmrCleanupXdpApiBindingContext(
    PVOID ClientBindingContext
    )
{
    XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext =
        (XBDRV_NMR_CLIENT_BINDING_CONTEXT*)ClientBindingContext;

    TraceEnter(TRACE_CONTROL, "-");

    //
    // Free memory for this binding.
    //
    ExFreePool(BindingContext);

    TraceExitSuccess(TRACE_CONTROL);
}

const NPI_CLIENT_CHARACTERISTICS XbDrvNmrXdpApiClientCharacteristics = {
    0, // Version
    sizeof(NPI_CLIENT_CHARACTERISTICS),
    (PNPI_CLIENT_ATTACH_PROVIDER_FN)XbDrvNmrAttachXdpApiProvider,
    (PNPI_CLIENT_DETACH_PROVIDER_FN)XbDrvNmrDetachXdpApiProvider,
    (PNPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN)XbDrvNmrCleanupXdpApiBindingContext,
    {
        0, // Version
        sizeof(NPI_REGISTRATION_INSTANCE),
        &NPI_XDPAPI_INTERFACE_ID,
        &NPI_XBDRV_MODULEID,
        XDP_API_VERSION_1, // Number
        NULL // NpiSpecificCharacteristics
    } // ClientRegistrationInstance
};

XDP_API_PROVIDER_BINDING_CONTEXT *
CxPlatXdpApiGetProviderBindingContext(
    VOID
    )
{
    return XdpApiProviderBindingContext;
}

VOID
CxPlatXdpApiInitialize(
    VOID
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_CONTROL, "-");

    Status =
        NmrRegisterClient(
            &XbDrvNmrXdpApiClientCharacteristics,
            NULL,
            &NmrRegistrationHandle);
    if (!NT_SUCCESS(Status)) {
        goto Done;
    }

    KeWaitForSingleObject(&BoundToProvider, Executive, KernelMode, FALSE, NULL);

Done:

    TraceExitStatus(TRACE_CONTROL);

    return;
}

VOID
CxPlatXdpApiUninitialize(
    VOID
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(TRACE_CONTROL, "-");

    if (NmrRegistrationHandle != NULL) {
        Status = NmrDeregisterClient(NmrRegistrationHandle);
        ASSERT(Status == STATUS_PENDING);

        if (Status == STATUS_PENDING) {
            Status = NmrWaitForClientDeregisterComplete(NmrRegistrationHandle);
            ASSERT(Status == STATUS_SUCCESS);
        }

        NmrRegistrationHandle = NULL;
    }

    TraceExitStatus(TRACE_CONTROL);

    return;
}

VOID
KmPlatPrint(
    char* Function,
    int Line,
    char level,
    const char* format,
    ...
    )
{
    char buffer[1024];
    va_list arglist;

    va_start(arglist, format);

    int err = vsprintf_s(buffer, sizeof(buffer), format, arglist);

    va_end(arglist);

    if (err < 0) {
        TraceError(TRACE_CONTROL, "vsprintf_s returned %d", err);
    } else {
        switch (level) {
        case 5:
            TraceVerbose(TRACE_CONTROL, "%s:%d - %s", Function, Line, buffer);
            break;
        case 4:
            TraceInfo(TRACE_CONTROL, "%s:%d - %s", Function, Line, buffer);
            break;
        case 3:
            TraceError(TRACE_CONTROL, "%s:%d - %s", Function, Line, buffer);
            break;
        default:
            ASSERT(FALSE);
        }
    }
}

VOID
CleanupArgs()
{
    if (Argv != NULL) {
        ExFreePool(Argv);
        Argv = NULL;
    }
    Argc = 0;
}

NTSTATUS
DeserializeArgs(
    char *InputBuffer,
    SIZE_T InputBufferLength
    )
{
    const SIZE_T MaxArgLength = 64;
    SIZE_T Index = 0;

    // calculate argc
    while (Index < InputBufferLength) {
        SIZE_T ArgLength = strnlen(InputBuffer + Index, MaxArgLength);
        if (ArgLength == MaxArgLength) {
            TraceError(TRACE_CONTROL, "Max arg length exceeded");
            return STATUS_INVALID_PARAMETER;
        }
        Index += ArgLength + 1;
        Argc++;
    }

    // allocate argv array
    Argv = ExAllocatePoolZero(NonPagedPoolNx, Argc * sizeof(char *), 'rDbX');
    if (Argv == NULL) {
        TraceError(TRACE_CONTROL, "memory");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // initialize argv array. Assumes that the IOCTL handler will not return until xskbench is complete.
    Argv[0] = InputBuffer;
    Index = 0;
    for (int i = 1; i < Argc; i++) {
        Index += strlen(InputBuffer + Index) + 1;
#pragma prefast (suppress: 6386, "Suppressing due to PREFast bug with _When_")
        Argv[i] = InputBuffer + Index;
    }

    return STATUS_SUCCESS;
}

VOID
MainThreadFunction(
    PVOID Context
    )
{
    UNREFERENCED_PARAMETER(Context);

    XskBenchStart(Argc, Argv);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_Dispatch_type_(IRP_MJ_CREATE)
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XbDrvDispatchCreate(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if (IsDeviceOpened) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    // IrpSp->FileObject->FsContext = UserContext;
    IsDeviceOpened = TRUE;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_Dispatch_type_(IRP_MJ_CLEANUP)
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XbDrvDispatchCleanup(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    return STATUS_SUCCESS;
}

_Dispatch_type_(IRP_MJ_CLOSE)
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XbDrvDispatchClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    IsDeviceOpened = FALSE;

    return STATUS_SUCCESS;
}

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XbDrvDispatchDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    NTSTATUS Status;
    IO_STACK_LOCATION *IrpSp;
    VOID *ThreadObject = NULL;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_START_SESSION:
        if (IsSessionActive) {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }

        Status =
            DeserializeArgs(
                Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        if (Status != STATUS_SUCCESS) {
            goto Exit;
        }

        Status =
            PsCreateSystemThread(
                &MainThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, MainThreadFunction, NULL);
        if (Status != STATUS_SUCCESS) {
            CleanupArgs();
            goto Exit;
        }

        IsSessionActive = TRUE;

        Status = ObReferenceObjectByHandle(MainThread, THREAD_ALL_ACCESS, NULL, KernelMode, &ThreadObject, NULL);
        if (Status != STATUS_SUCCESS) {
            TraceError(TRACE_CONTROL, "ObReferenceObjectByHandle failed");
            goto Exit;
        }

        KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);

        ObDereferenceObject(ThreadObject);

        CleanupArgs();
        IsSessionActive = FALSE;

        break;

    case IOCTL_INTERRUPT_SESSION:
        if (!IsSessionActive) {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }

        XskBenchCtrlHandler();
        Status = STATUS_SUCCESS;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

_Function_class_(DRIVER_UNLOAD)
VOID
XbDrvUnload(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    if (XskBenchDrvDeviceObject != NULL) {
        IoDeleteDevice(XskBenchDrvDeviceObject);
        XskBenchDrvDeviceObject = NULL;
    }

    CxPlatUninitialize();

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

#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    KeInitializeEvent(&BoundToProvider, NotificationEvent, FALSE);

    CxPlatInitialize();

    XskBenchInitialize();

    RtlInitUnicodeString(&DeviceName, XSKBENCHDRV_DEVICE_NAME);

    Status =
        IoCreateDevice(
            DriverObject,
            0,
            &DeviceName,
            FILE_DEVICE_NETWORK,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &XskBenchDrvDeviceObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    DriverObject->DriverUnload = XbDrvUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = XbDrvDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = XbDrvDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = XbDrvDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = XbDrvDispatchDeviceControl;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return STATUS_SUCCESS;
}
