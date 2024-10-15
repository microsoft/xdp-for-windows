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
#include "platform_kernel.h"
#include "xskbench_common.h"
#include "xskbench.h"
#include "trace.h"
#include "xskbenchdrvioctl.h"
#include "driver.tmh"

XDP_API_PROVIDER_DISPATCH *XdpApi;
XDP_API_PROVIDER_BINDING_CONTEXT *XdpApiProviderBindingContext;

static DEVICE_OBJECT *XskBenchDrvDeviceObject;
static BOOLEAN IsDeviceOpened;
static BOOLEAN IsSessionActive;
static HANDLE MainThread;
static int Argc;
static char **Argv;

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_STATUS
XdpFuncXskNotifyCallback(
    _In_ VOID* ClientContext,
    _In_ XSK_NOTIFY_RESULT_FLAGS Result
    )
{
    UNREFERENCED_PARAMETER(ClientContext);
    UNREFERENCED_PARAMETER(Result);
    return XDP_STATUS_SUCCESS;
}

static const XDP_API_CLIENT_DISPATCH XdpFuncXdpApiClientDispatch = {
    XdpFuncXskNotifyCallback
};

static XDP_API_CLIENT XdpApiContext = {0};
#define TEST_TIMEOUT_ASYNC_MS 1000
#define POLL_INTERVAL_MS 10

VOID
DetachCallback(
    _In_ VOID *ClientContext
    )
{
    UNREFERENCED_PARAMETER(ClientContext);
}

VOID
CxPlatXdpApiInitialize(
    VOID
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    TraceEnter(TRACE_CONTROL, "-");

    Status = XdpOpenApi(XDP_API_VERSION_1, NULL, NULL, &DetachCallback, &XdpFuncXdpApiClientDispatch, &XdpApiContext, &XdpApi, &XdpApiProviderBindingContext);

    if (!NT_SUCCESS(Status)) {
        goto Done;
    }

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

    XdpUnloadApi(&XdpApiContext);

    TraceExitStatus(TRACE_CONTROL);

    return;
}

XDP_STATUS
CxPlatXskCreate(
    _Out_ HANDLE *Socket
    )
{
    return XdpApi->XskCreate(
                XdpApiProviderBindingContext,
                NULL, NULL, NULL,
                Socket);
}

XDP_STATUS
CxPlatXdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    return XdpApi->XdpCreateProgram(
        XdpApiProviderBindingContext,
        InterfaceIndex,
        HookId,
        QueueId,
        Flags,
        Rules,
        RuleCount,
        Program);
}

VOID
CxPlatPrintStats(
    MY_QUEUE *Queue
    )
{
    KFLOATING_SAVE FloatingSave;
    if (KeSaveFloatingPointState(&FloatingSave) == STATUS_SUCCESS) {
        PrintFinalStats(Queue);
        KeRestoreFloatingPointState(&FloatingSave);
    }
}

VOID
CxPlatQueueCleanup(
    MY_QUEUE *Queue
    )
{
    if (Queue->rxProgram != NULL) {
        XdpApi->XdpDeleteProgram(Queue->rxProgram);
        Queue->rxProgram = NULL;
    }
    if (Queue->sock != NULL) {
        XdpApi->XskDelete(Queue->sock);
        Queue->sock = NULL;
    }

    if (Queue->umemReg.Address != NULL) {
        CXPLAT_VIRTUAL_FREE(Queue->umemReg.Address, 0, MEM_RELEASE, POOLTAG_UMEM);
        Queue->umemReg.Address = NULL;
    }
    if (Queue->freeRingLayout != NULL) {
        CXPLAT_FREE(Queue->freeRingLayout, POOLTAG_FREERING);
        Queue->freeRingLayout = NULL;
    }
}

BOOLEAN
CxPlatEnableLargePages(
    VOID
    )
{
    return TRUE;
}

VOID
CxPlatAlignMemory(
    _Inout_ XSK_UMEM_REG *UmemReg
    )
{
    UNREFERENCED_PARAMETER(UmemReg);
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
