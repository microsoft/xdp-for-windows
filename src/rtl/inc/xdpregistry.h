//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

__declspec(code_seg("PAGE"))
NTSTATUS
XdpRegQueryDwordValue(
    _In_z_ CONST WCHAR *KeyName,
    _In_z_ CONST WCHAR *ValueName,
    _Out_ ULONG *ValueData
    );

__declspec(code_seg("PAGE"))
NTSTATUS
XdpRegQueryBoolean(
    _In_z_ CONST WCHAR *KeyName,
    _In_z_ CONST WCHAR *ValueName,
    _Out_ BOOLEAN *ValueData
    );

//
// The XDP registry watcher uses ZwNotifyChangeKey to register for value changes
// of a registry key and triggers notifications to a list of clients. The
// registration is not recursive, meaning value changes for sub keys will not
// trigger notifications.
//

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_REG_WATCHER_CLIENT_CALLBACK(
    VOID
    );

typedef struct _XDP_REG_WATCHER_CLIENT_ENTRY {
    LIST_ENTRY Link;
    XDP_REG_WATCHER_CLIENT_CALLBACK *Callback;
} XDP_REG_WATCHER_CLIENT_ENTRY;

typedef struct _XDP_REG_WATCHER XDP_REG_WATCHER;

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherAddClient(
    _In_ XDP_REG_WATCHER *Watcher,
    _In_ XDP_REG_WATCHER_CLIENT_CALLBACK *ClientCallback,
    _Inout_ XDP_REG_WATCHER_CLIENT_ENTRY *ClientEntry
    );

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherRemoveClient(
    _In_ XDP_REG_WATCHER *Watcher,
    _Inout_ XDP_REG_WATCHER_CLIENT_ENTRY *ClientEntry
    );

_IRQL_requires_(PASSIVE_LEVEL)
XDP_REG_WATCHER *
XdpRegWatcherCreate(
    _In_z_ CONST WCHAR *KeyName,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    );

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherDelete(
    _In_ XDP_REG_WATCHER *Watcher
    );
