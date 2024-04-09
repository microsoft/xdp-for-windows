/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Kernel Mode Test Driver

--*/

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <netioddk.h>

// #include <ntddk.h>
// #include <ntintsafe.h>
// #include <ntstrsafe.h>
// #include <ndis.h>
#include <ws2def.h>
// #include <ws2ipdef.h>
// #include <netiodef.h>
// #include <netioapi.h>
// #include <mstcpip.h>

#include <xdpapi.h>

#include "tests.h"
#include "xdpfunctionaltestdrvioctl.h"
#include "fntrace.h"

#include "control.tmh"

#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif

// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE __declspec(code_seg(KRTL_INIT_SEGMENT))

// Use on pageable functions.
#define PAGEDX __declspec(code_seg(KRTL_PAGE_SEGMENT))

DECLARE_CONST_UNICODE_STRING(TestDrvCtlDeviceName, L"\\Device\\" FUNCTIONAL_TEST_DRIVER_NAME);
DECLARE_CONST_UNICODE_STRING(TestDrvCtlDeviceSymLink, L"\\DosDevices\\" FUNCTIONAL_TEST_DRIVER_NAME);

typedef struct TEST_CLIENT
{
    bool TestFailure;

} TEST_CLIENT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TEST_CLIENT, TestDrvCtlGetFileContext);

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL TestDrvCtlEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE TestDrvCtlEvtIoCanceled;

PAGEDX EVT_WDF_DEVICE_FILE_CREATE TestDrvCtlEvtFileCreate;
PAGEDX EVT_WDF_FILE_CLOSE TestDrvCtlEvtFileClose;
PAGEDX EVT_WDF_FILE_CLEANUP TestDrvCtlEvtFileCleanup;

WDFDEVICE TestDrvCtlDevice = nullptr;
TEST_CLIENT* TestDrvClient = nullptr;
CHAR TestDrvClientActive = FALSE;

_No_competing_thread_
INITCODE
NTSTATUS
TestDrvCtlInitialize(
    _In_ WDFDRIVER Driver
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PWDFDEVICE_INIT DeviceInit = nullptr;
    WDF_FILEOBJECT_CONFIG FileConfig;
    WDF_OBJECT_ATTRIBUTES Attribs;
    WDFDEVICE Device;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDFQUEUE Queue;

    DeviceInit =
        WdfControlDeviceInitAllocate(
            Driver,
            &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (DeviceInit == nullptr) {
        TraceError(
            "[ lib] ERROR, %s.",
            "WdfControlDeviceInitAllocate failed");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Error;
    }

    Status =
        WdfDeviceInitAssignName(
            DeviceInit,
            &TestDrvCtlDeviceName);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WdfDeviceInitAssignName failed");
        goto Error;
    }

    WDF_FILEOBJECT_CONFIG_INIT(
        &FileConfig,
        TestDrvCtlEvtFileCreate,
        TestDrvCtlEvtFileClose,
        TestDrvCtlEvtFileCleanup);
    FileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext2;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attribs, TEST_CLIENT);
    WdfDeviceInitSetFileObjectConfig(
        DeviceInit,
        &FileConfig,
        &Attribs);

    Status =
        WdfDeviceCreate(
            &DeviceInit,
            &Attribs,
            &Device);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WdfDeviceCreate failed");
        goto Error;
    }

    Status = WdfDeviceCreateSymbolicLink(Device, &TestDrvCtlDeviceSymLink);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WdfDeviceCreateSymbolicLink failed");
        goto Error;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = TestDrvCtlEvtIoDeviceControl;
    QueueConfig.EvtIoCanceledOnQueue = TestDrvCtlEvtIoCanceled;

    __analysis_assume(QueueConfig.EvtIoStop != 0);
    Status =
        WdfIoQueueCreate(
            Device,
            &QueueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &Queue);
    __analysis_assume(QueueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WdfIoQueueCreate failed");
        goto Error;
    }

    TestDrvCtlDevice = Device;

    WdfControlFinishInitializing(Device);

    TraceVerbose(
        "[test] Control interface initialized");

Error:

    if (DeviceInit) {
        WdfDeviceInitFree(DeviceInit);
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
TestDrvCtlUninitialize(
    )
{
    TraceVerbose(
        "[test] Control interface uninitializing");

    if (TestDrvCtlDevice != nullptr) {
        WdfObjectDelete(TestDrvCtlDevice);
        TestDrvCtlDevice = nullptr;
    }

    TraceVerbose(
        "[test] Control interface uninitialized");
}

PAGEDX
_Use_decl_annotations_
VOID
TestDrvCtlEvtFileCreate(
    _In_ WDFDEVICE /* Device */,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    do
    {
        //
        // Single client support.
        //
        if (InterlockedExchange8(&TestDrvClientActive, TRUE) == TRUE) {
            TraceError(
                "[ lib] ERROR, %s.",
                "Already have max clients");
            Status = STATUS_TOO_MANY_SESSIONS;
            break;
        }

        TEST_CLIENT* Client = TestDrvCtlGetFileContext(FileObject);
        if (Client == nullptr) {
            TraceError(
                "[ lib] ERROR, %s.",
                "nullptr File context in FileCreate");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        RtlZeroMemory(Client, sizeof(TEST_CLIENT));

        TraceInfo(
            "[test] Client %p created",
            Client);

        TestDrvClient = Client;

        //
        // TestSetup requires TestDrvClient to be initialized.
        //
        if (!TestSetup()) {
            TraceError(
                "[ lib] ERROR, %s.",
                "TestSetup failed");
            TestDrvClient = nullptr;
            Status = STATUS_UNSUCCESSFUL;
            break;
        }
    }
    while (false);

    if (!NT_SUCCESS(Status) && Status != STATUS_TOO_MANY_SESSIONS) {
        InterlockedExchange8(&TestDrvClientActive, FALSE);
    }

    WdfRequestComplete(Request, Status);
}

PAGEDX
_Use_decl_annotations_
VOID
TestDrvCtlEvtFileClose(
    _In_ WDFFILEOBJECT /* FileObject */
    )
{
    PAGED_CODE();
}

PAGEDX
_Use_decl_annotations_
VOID
TestDrvCtlEvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
    )
{
    PAGED_CODE();

    TEST_CLIENT* Client = TestDrvCtlGetFileContext(FileObject);
    if (Client != nullptr) {

        TraceInfo(
            "[test] Client %p cleaning up",
            Client);

        //
        // TestSetup requires TestDrvClient to be initialized.
        //
        TestCleanup();

        TestDrvClient = nullptr;

        InterlockedExchange8(&TestDrvClientActive, FALSE);
    }
}

VOID
TestDrvCtlEvtIoCanceled(
    _In_ WDFQUEUE /* Queue */,
    _In_ WDFREQUEST Request
    )
{
    NTSTATUS Status;

    WDFFILEOBJECT FileObject = WdfRequestGetFileObject(Request);
    if (FileObject == nullptr) {
        Status = STATUS_DEVICE_NOT_READY;
        goto error;
    }

    TEST_CLIENT* Client = TestDrvCtlGetFileContext(FileObject);
    if (Client == nullptr) {
        Status = STATUS_DEVICE_NOT_READY;
        goto error;
    }

    TraceWarn(
        "[test] Client %p canceled request %p",
        Client,
        Request);

    Status = STATUS_CANCELLED;

error:

    WdfRequestComplete(Request, Status);
}

size_t IOCTL_BUFFER_SIZES[] =
{
    0,
    0,
};

static_assert(
    MAX_IOCTL_FUNC_CODE + 1 == (sizeof(IOCTL_BUFFER_SIZES)/sizeof(size_t)),
    "IOCTL_BUFFER_SIZES must be kept in sync with the IOCTLs");

typedef union {
} IOCTL_PARAMS;

#define TestDrvCtlRun(X) \
    Client->TestFailure = false; \
    X; \
    Status = Client->TestFailure ? STATUS_FAIL_FAST_EXCEPTION : STATUS_SUCCESS;

VOID
TestDrvCtlEvtIoDeviceControl(
    _In_ WDFQUEUE /* Queue */,
    _In_ WDFREQUEST Request,
    _In_ size_t /* OutputBufferLength */,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    WDFFILEOBJECT FileObject = nullptr;
    TEST_CLIENT* Client = nullptr;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        Status = STATUS_NOT_SUPPORTED;
        TraceError(
            "[ lib] ERROR, %s.",
            "IOCTL not supported greater than PASSIVE_LEVEL");
        goto Error;
    }

    FileObject = WdfRequestGetFileObject(Request);
    if (FileObject == nullptr) {
        Status = STATUS_DEVICE_NOT_READY;
        TraceError(
            "[ lib] ERROR, %s.",
            "WdfRequestGetFileObject failed");
        goto Error;
    }

    Client = TestDrvCtlGetFileContext(FileObject);
    if (Client == nullptr) {
        Status = STATUS_DEVICE_NOT_READY;
        TraceError(
            "[ lib] ERROR, %s.",
            "TestDrvCtlGetFileContext failed");
        goto Error;
    }

    ULONG FunctionCode = IoGetFunctionCodeFromCtlCode(IoControlCode);
    if (FunctionCode == 0 || FunctionCode > MAX_IOCTL_FUNC_CODE) {
        Status = STATUS_NOT_IMPLEMENTED;
        TraceError(
            "[ lib] ERROR, %u, %s.",
            FunctionCode,
            "Invalid FunctionCode");
        goto Error;
    }

    if (InputBufferLength < IOCTL_BUFFER_SIZES[FunctionCode]) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        TraceError(
            "[ lib] ERROR, %u, %s.",
            FunctionCode,
            "Invalid buffer size for FunctionCode");
        goto Error;
    }

    IOCTL_PARAMS* Params = nullptr;
    if (IOCTL_BUFFER_SIZES[FunctionCode] != 0) {
        Status =
            WdfRequestRetrieveInputBuffer(
                Request,
                IOCTL_BUFFER_SIZES[FunctionCode],
                (void**)&Params,
                nullptr);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Status,
                "WdfRequestRetrieveInputBuffer failed");
            goto Error;
        } else if (Params == nullptr) {
            TraceError(
                "[ lib] ERROR, %s.",
                "WdfRequestRetrieveInputBuffer failed to return parameter buffer");
            Status = STATUS_INVALID_PARAMETER;
            goto Error;
        }
    }

    TraceInfo(
        "[test] Client %p executing IOCTL %u",
        Client,
        FunctionCode);

    switch (IoControlCode) {
    case IOCTL_LOAD_API:
        TestDrvCtlRun(LoadApiTest());
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

Error:

    TraceInfo(
        "[test] Client %p completing request, 0x%x",
        Client,
        Status);

    WdfRequestComplete(Request, Status);
}

EXTERN_C
_IRQL_requires_max_(PASSIVE_LEVEL)
void
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
/*++

Routine Description:

    Records a test failure from the platform independent test code.

Arguments:

    File - The file where the failure occurred.

    Function - The function where the failure occurred.

    Line - The line (in File) where the failure occurred.

Return Value:

    None

--*/
{
    wchar_t Buffer[128];

    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (TestDrvClient != nullptr) {
        TestDrvClient->TestFailure = true;
    }

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, RTL_NUMBER_OF(Buffer), _TRUNCATE, Format, Args);
    va_end(Args);

    TraceError(
        "[test] File: %S, Function: %S, Line: %d",
        File,
        Function,
        Line);
    TraceError(
        "[test] FAIL: %S",
        Buffer);

#if BREAK_TEST
    NT_FRE_ASSERT(FALSE);
#endif
}
