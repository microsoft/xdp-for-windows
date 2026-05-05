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
    _In_ const XDP_OPEN_PACKET *OpenPacket,
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

//
// Device extension stored in each XDP device object.
//
typedef struct _XDP_DEVICE_EXTENSION {
    BOOLEAN IsPerTypeDevice;
    XDP_OBJECT_TYPE AllowedObjectType;
} XDP_DEVICE_EXTENSION;

//
// Table entry for per-object-type device objects.
//
typedef struct _XDP_DEVICE_TABLE_ENTRY {
    XDP_OBJECT_TYPE ObjectType;
    const GUID *DeviceClassGuid;
    DEVICE_OBJECT *DeviceObject;
} XDP_DEVICE_TABLE_ENTRY;

NTSTATUS
XdpReferenceObjectByHandle(
    _In_ HANDLE Handle,
    _In_ XDP_OBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ FILE_OBJECT **XdpFileObject
    );

BOOLEAN
XdpIsXdpDeviceObject(
    _In_ DEVICE_OBJECT *DeviceObject
    );

extern DRIVER_OBJECT *XdpDriverObject;
extern DEVICE_OBJECT *XdpDeviceObject;
extern XDP_DEVICE_TABLE_ENTRY XdpDeviceTable[XDP_OBJECT_TYPE_MAX];
extern const WCHAR *XDP_PARAMETERS_KEY;
extern XDP_REG_WATCHER *XdpRegWatcher;

BOOLEAN
XdpIsFeOrLater(
    VOID
    );

BOOLEAN
XdpFaultInject(
    VOID
    );
