//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "dispatch.tmh"

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

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
DRIVER_DISPATCH_PAGED XdpIrpDispatch;
DRIVER_DISPATCH_PAGED XdpIrpDeviceIoControl;
FAST_IO_DEVICE_CONTROL XdpFastDeviceIoControl;

FAST_IO_DISPATCH XdpFastIoDispatch = {
    .FastIoDeviceControl = XdpFastDeviceIoControl,
};

static RTL_OSVERSIONINFOW XdpOsVersion;
static WCHAR XdpParametersKeyStorage[256] = { 0 };
CONST WCHAR *XDP_PARAMETERS_KEY = XdpParametersKeyStorage;

DRIVER_OBJECT *XdpDriverObject;
DEVICE_OBJECT *XdpDeviceObject;
XDP_REG_WATCHER *XdpRegWatcher;
XDP_REG_WATCHER_CLIENT_ENTRY XdpRegWatcherEntry = {0};

#if DBG
static BOOLEAN XdpFaultInjectEnabled = FALSE;
#endif

#pragma NDIS_INIT_FUNCTION(DriverEntry)

BOOLEAN
XdpIsFeOrLater(
    VOID
    )
{
    return
        (XdpOsVersion.dwMajorVersion > 10 || (XdpOsVersion.dwMajorVersion == 10 &&
        (XdpOsVersion.dwMinorVersion > 0 || (XdpOsVersion.dwMinorVersion == 0 &&
        (XdpOsVersion.dwBuildNumber >= 20100)))));
}

BOOLEAN
XdpFaultInject(
    VOID
    )
{
#if DBG
    return XdpFaultInjectEnabled && RtlRandomNumberInRange(0, 100) == 0;
#else
    return FALSE;
#endif
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIrpCreate(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    FILE_FULL_EA_INFORMATION *EaBuffer;
    XDP_OPEN_PACKET *OpenPacket = NULL;
    UCHAR Disposition = 0;
    STRING ExpectedEaName;
    STRING ActualEaName = {0};
    XDP_FILE_CREATE_ROUTINE *CreateRoutine = NULL;

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

    RtlInitString(&ExpectedEaName, XDP_OPEN_PACKET_NAME);
    if (!RtlEqualString(&ExpectedEaName, &ActualEaName, FALSE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (EaBuffer->EaValueLength < sizeof(XDP_OPEN_PACKET)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    OpenPacket = (XDP_OPEN_PACKET *)(EaBuffer->EaName + EaBuffer->EaNameLength + 1);

    switch (OpenPacket->ObjectType) {
    case XDP_OBJECT_TYPE_PROGRAM:
        CreateRoutine = XdpIrpCreateProgram;
        break;

    case XDP_OBJECT_TYPE_XSK:
        CreateRoutine = XskIrpCreateSocket;
        break;

    case XDP_OBJECT_TYPE_INTERFACE:
        CreateRoutine = XdpIrpCreateInterface;
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status =
        CreateRoutine(
            Irp, IrpSp, Disposition, OpenPacket + 1,
            EaBuffer->EaValueLength - sizeof(XDP_OPEN_PACKET));

    if (NT_SUCCESS(Status)) {
        ASSERT(IrpSp->FileObject->FsContext != NULL);
        ASSERT(((XDP_FILE_OBJECT_HEADER *)IrpSp->FileObject->FsContext)->Dispatch != NULL);
    }

Exit:

    ASSERT(Status != STATUS_PENDING);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIrpCleanup(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_FILE_OBJECT_HEADER *FileHeader = IrpSp->FileObject->FsContext;

    if (FileHeader != NULL && FileHeader->Dispatch->Cleanup != NULL) {
        return FileHeader->Dispatch->Cleanup(Irp, IrpSp);
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIrpClose(
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_FILE_OBJECT_HEADER *FileHeader = IrpSp->FileObject->FsContext;

    if (FileHeader != NULL && FileHeader->Dispatch->Close != NULL) {
        return FileHeader->Dispatch->Close(Irp, IrpSp);
    }

    return STATUS_SUCCESS;
}

__declspec(code_seg("PAGE"))
_Use_decl_annotations_
NTSTATUS
XdpIrpDispatch(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp
    )
{
    IO_STACK_LOCATION *IrpSp;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(DeviceObject == XdpDeviceObject);
    PAGED_CODE();

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    //
    // Ensure threads cannot be frozen by user mode within any driver routines.
    //
    KeEnterCriticalRegion();

    switch (IrpSp->MajorFunction) {
    case IRP_MJ_CREATE:
        Status = XdpIrpCreate(Irp, IrpSp);
        break;

    case IRP_MJ_CLEANUP:
        Status = XdpIrpCleanup(Irp, IrpSp);
        break;

    case IRP_MJ_CLOSE:
        Status = XdpIrpClose(Irp, IrpSp);
        break;

    default:
        break;
    }

    KeLeaveCriticalRegion();

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

__declspec(code_seg("PAGE"))
_Use_decl_annotations_
NTSTATUS
XdpIrpDeviceIoControl(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp
    )
{
    IO_STACK_LOCATION *IrpSp;
    XDP_FILE_OBJECT_HEADER *FileHeader;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(DeviceObject == XdpDeviceObject);
    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileHeader = IrpSp->FileObject->FsContext;

    //
    // Ensure threads cannot be frozen by user mode within any driver routines.
    //
    KeEnterCriticalRegion();

    if (FileHeader != NULL) {
        if (FileHeader->Dispatch->IoControl != NULL) {
            Status = FileHeader->Dispatch->IoControl(Irp, IrpSp);
        } else {
            Status = STATUS_NOT_SUPPORTED;
        }
    } else {
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
        default:
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

Exit:

    KeLeaveCriticalRegion();

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

#pragma warning(push)
#pragma warning(disable:6101) // We don't set OutputBuffer
__declspec(code_seg("PAGE"))
_Use_decl_annotations_
BOOLEAN
XdpFastDeviceIoControl(
    struct _FILE_OBJECT *FileObject,
    BOOLEAN Wait,
    VOID *InputBuffer,
    ULONG InputBufferLength,
    VOID *OutputBuffer,
    ULONG OutputBufferLength,
    ULONG IoControlCode,
    IO_STATUS_BLOCK *IoStatus,
    struct _DEVICE_OBJECT *DeviceObject
    )
{
    XDP_FILE_OBJECT_HEADER *FileObjHeader = FileObject->FsContext;

    PAGED_CODE();

    if (FileObjHeader == NULL) {
        return FALSE;
    }

    switch (FileObjHeader->ObjectType) {
    case XDP_OBJECT_TYPE_XSK:
        return
            XskFastIo(
                FileObjHeader, InputBuffer, InputBufferLength, OutputBuffer,
                OutputBufferLength, IoControlCode, IoStatus);
    default:
        return FALSE;
    }

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(DeviceObject);
}
#pragma warning(pop)

NTSTATUS
XdpReferenceObjectByHandle(
    _In_ HANDLE Handle,
    _In_ XDP_OBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ FILE_OBJECT **XdpFileObject
    )
{
    NTSTATUS Status;
    FILE_OBJECT *FileObject = NULL;
    XDP_FILE_OBJECT_HEADER *XdpFileObjectHeader;

    Status =
        ObReferenceObjectByHandle(
            Handle, DesiredAccess, *IoFileObjectType, RequestorMode,
            &FileObject, NULL);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (FileObject->DeviceObject != XdpDeviceObject || FileObject->FsContext == NULL) {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }

    XdpFileObjectHeader = (XDP_FILE_OBJECT_HEADER *)FileObject->FsContext;
    if (XdpFileObjectHeader->ObjectType != ObjectType) {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }

    *XdpFileObject = FileObject;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (FileObject != NULL) {
            ObDereferenceObject(FileObject);
        }
    }

    return Status;
}

static
VOID
XdpRegistryUpdate(
    VOID
    )
{
#if DBG
    NTSTATUS Status;

    Status =
        XdpRegQueryBoolean(XDP_PARAMETERS_KEY, L"XdpFaultInject", &XdpFaultInjectEnabled);
    if (!NT_SUCCESS(Status)) {
        XdpFaultInjectEnabled = FALSE;
    }
#endif
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
XdpStop(
    VOID
    )
{
    if (XdpDeviceObject != NULL) {
        IoDeleteDevice(XdpDeviceObject);
        XdpDeviceObject = NULL;
    }

    XdpProgramStop();
    XskStop();
    XdpIfStop();
    XdpTxStop();
    XdpRxStop();
    XdpPollStop();
    XdpRegWatcherRemoveClient(XdpRegWatcher, &XdpRegWatcherEntry);

    if (XdpRegWatcher != NULL) {
        XdpRegWatcherDelete(XdpRegWatcher);
        XdpRegWatcher = NULL;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpStart(
    VOID
    )
{
    NTSTATUS Status;
    DECLARE_CONST_UNICODE_STRING(DeviceName, XDP_DEVICE_NAME);

#pragma warning(push)
#pragma warning(disable:28175) // We can access the DRIVER_OBJECT
#pragma warning(disable:28168) // Default annotation seems to be incorrect
    XdpDriverObject->MajorFunction[IRP_MJ_CREATE]           = (PDRIVER_DISPATCH)XdpIrpDispatch; // BUGBUG: MajorFunction is an array of PDRIVER_DISPATCH, NOT PDRIVER_DISPATCH_PAGED!!!
    XdpDriverObject->MajorFunction[IRP_MJ_CLEANUP]          = (PDRIVER_DISPATCH)XdpIrpDispatch;
    XdpDriverObject->MajorFunction[IRP_MJ_CLOSE]            = (PDRIVER_DISPATCH)XdpIrpDispatch;
    XdpDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]   = (PDRIVER_DISPATCH)XdpIrpDeviceIoControl;
    XdpDriverObject->FastIoDispatch                         = &XdpFastIoDispatch;
    XdpDriverObject->DriverUnload                           = DriverUnload;
#pragma warning(pop)

    XdpOsVersion.dwOSVersionInfoSize = sizeof(XdpOsVersion);
    Status = RtlGetVersion(&XdpOsVersion);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    TraceInfo(
        TRACE_CORE, "XdpVersion=%s OsVersion=%u.%u.%u",
        XDP_VERSION_STR, XdpOsVersion.dwMajorVersion, XdpOsVersion.dwMinorVersion,
        XdpOsVersion.dwBuildNumber);

    //
    // Load initial configuration before doing anything else.
    //
    XdpRegistryUpdate();

    XdpRegWatcher = XdpRegWatcherCreate(XDP_PARAMETERS_KEY, XdpDriverObject, NULL);
    if (XdpRegWatcher == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpRegWatcherAddClient(XdpRegWatcher, XdpRegistryUpdate, &XdpRegWatcherEntry);

    Status = XdpPollStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpRxStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpTxStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpIfStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XskStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpProgramStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        IoCreateDeviceSecure(
            XdpDriverObject, 0, (PUNICODE_STRING)&DeviceName, FILE_DEVICE_NETWORK,
            FILE_DEVICE_SECURE_OPEN, FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL, &XDP_DEVICE_CLASS_GUID,
            &XdpDeviceObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpStop();
    }

    return Status;
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    DRIVER_OBJECT *DriverObject,
    UNICODE_STRING *RegistryPath
    )
{
    NTSTATUS Status;

    XdpDriverObject = DriverObject;

#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);

    WPP_INIT_TRACING(XdpDriverObject, RegistryPath);
    EventRegisterMicrosoft_XDP();

    TraceEnter(TRACE_CORE, "DriverObject=%p", DriverObject);

    if (wcscat_s(
            XdpParametersKeyStorage, RTL_NUMBER_OF(XdpParametersKeyStorage),
            RegistryPath->Buffer) != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    if (wcscat_s(
            XdpParametersKeyStorage, RTL_NUMBER_OF(XdpParametersKeyStorage),
            L"\\Parameters") != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = XdpRtlStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpLwfStart(XdpDriverObject, XDP_PARAMETERS_KEY);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);

    if (!NT_SUCCESS(Status)) {
        DriverUnload(DriverObject);
    }

    return Status;
}

_Use_decl_annotations_
VOID
DriverUnload(
    DRIVER_OBJECT *DriverObject
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(TRACE_CORE, "DriverObject=%p", DriverObject);

    XdpLwfStop();
    XdpStop();
    XdpRtlStop();

    TraceExitStatus(TRACE_CORE);

    EventUnregisterMicrosoft_XDP();
    WPP_CLEANUP(XdpDriverObject);
}
