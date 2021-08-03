//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

DRIVER_OBJECT *XdpLwfDriverObject;
CONST WCHAR *XDP_LWF_PARAMETERS_KEY;
XDP_REG_WATCHER *XdpLwfRegWatcher = NULL;

NTSTATUS
XdpLwfStart(
    _In_ DRIVER_OBJECT *DriverObject,
    _In_z_ CONST WCHAR *RegistryPath
    )
{
    NTSTATUS Status;

    XdpLwfDriverObject = DriverObject;
    XDP_LWF_PARAMETERS_KEY = RegistryPath;

    XdpLwfRegWatcher = XdpRegWatcherCreate(XDP_LWF_PARAMETERS_KEY, XdpLwfDriverObject, NULL);
    if (XdpLwfRegWatcher == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = XdpLifetimeStart(XdpLwfDriverObject, NULL);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpGenericStart();
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpLwfBindStart(XdpLwfDriverObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:
    if (!NT_SUCCESS(Status)) {
        XdpLwfStop();
    }

    return Status;
}

VOID
XdpLwfStop(
    VOID
    )
{
    XdpLwfBindStop();
    XdpGenericStop();
    XdpLifetimeStop();

    if (XdpLwfRegWatcher != NULL) {
        XdpRegWatcherDelete(XdpLwfRegWatcher);
        XdpLwfRegWatcher = NULL;
    }

    XdpLwfDriverObject = NULL;
}
