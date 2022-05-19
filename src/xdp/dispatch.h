//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XDP_FILE_CREATE_ROUTINE(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XDP_FILE_IRP_ROUTINE(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    );

typedef struct _XDP_FILE_DISPATCH {
    XDP_FILE_IRP_ROUTINE *IoControl;
    XDP_FILE_IRP_ROUTINE *Cleanup;
    XDP_FILE_IRP_ROUTINE *Close;
} XDP_FILE_DISPATCH;

typedef struct _XDP_FILE_OBJECT_HEADER {
    XDP_OBJECT_TYPE ObjectType;
    XDP_FILE_DISPATCH *Dispatch;
} XDP_FILE_OBJECT_HEADER;

NTSTATUS
XdpReferenceObjectByHandle(
    _In_ HANDLE Handle,
    _In_ XDP_OBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ FILE_OBJECT **XdpFileObject
    );

extern DRIVER_OBJECT *XdpDriverObject;
extern DEVICE_OBJECT *XdpDeviceObject;
extern CONST WCHAR *XDP_PARAMETERS_KEY;
extern XDP_REG_WATCHER *XdpRegWatcher;

BOOLEAN
XdpIsFeOrLater(
    VOID
    );

BOOLEAN
XdpFaultInject(
    VOID
    );
