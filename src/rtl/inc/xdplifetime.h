//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This is an early experiment with a minimal implementation of read-copy-update
// on Windows.
//

#pragma once

typedef struct _XDP_LIFETIME_ENTRY XDP_LIFETIME_ENTRY;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_LIFETIME_DELETE(
    _In_ XDP_LIFETIME_ENTRY *Entry
    );

typedef struct _XDP_LIFETIME_ENTRY {
    SINGLE_LIST_ENTRY Link;
    XDP_LIFETIME_DELETE *DeleteRoutine;
} XDP_LIFETIME_ENTRY;

VOID
XdpLifetimeDelete(
    _In_ XDP_LIFETIME_DELETE *DeleteRoutine,
    _In_ XDP_LIFETIME_ENTRY *Entry
    );

NTSTATUS
XdpLifetimeStart(
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    );

VOID
XdpLifetimeStop(
    VOID
    );
