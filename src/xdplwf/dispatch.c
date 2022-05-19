//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

DRIVER_OBJECT *XdpLwfDriverObject;
CONST WCHAR *XDP_LWF_PARAMETERS_KEY;
XDP_REG_WATCHER *XdpLwfRegWatcher = NULL;
XDP_REG_WATCHER_CLIENT_ENTRY XdpLwfRegWatcherEntry = {0};

#if DBG
static BOOLEAN XdpLwfFaultInjectEnabled = FALSE;
#endif

static
VOID
XdpLwfRegistryUpdate(
    VOID
    )
{
#if DBG
    NTSTATUS Status;

    Status =
        XdpRegQueryBoolean(XDP_LWF_PARAMETERS_KEY, L"XdpFaultInject", &XdpLwfFaultInjectEnabled);
    if (!NT_SUCCESS(Status)) {
        XdpLwfFaultInjectEnabled = FALSE;
    }
#endif
}

NTSTATUS
XdpLwfStart(
    _In_ DRIVER_OBJECT *DriverObject,
    _In_z_ CONST WCHAR *RegistryPath
    )
{
    NTSTATUS Status;

    XdpLwfDriverObject = DriverObject;
    XDP_LWF_PARAMETERS_KEY = RegistryPath;

    //
    // Load initial configuration before doing anything else.
    //
    XdpLwfRegistryUpdate();

    XdpLwfRegWatcher = XdpRegWatcherCreate(XDP_LWF_PARAMETERS_KEY, XdpLwfDriverObject, NULL);
    if (XdpLwfRegWatcher == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpRegWatcherAddClient(XdpLwfRegWatcher, XdpLwfRegistryUpdate, &XdpLwfRegWatcherEntry);

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
    XdpRegWatcherRemoveClient(XdpLwfRegWatcher, &XdpLwfRegWatcherEntry);

    if (XdpLwfRegWatcher != NULL) {
        XdpRegWatcherDelete(XdpLwfRegWatcher);
        XdpLwfRegWatcher = NULL;
    }

    XdpLwfDriverObject = NULL;
}

BOOLEAN
XdpLwfFaultInject(
    VOID
    )
{
#if DBG
    return XdpLwfFaultInjectEnabled && RtlRandomNumberInRange(0, 100) == 0;
#else
    return FALSE;
#endif
}
