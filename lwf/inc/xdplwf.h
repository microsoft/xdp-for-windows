//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

NTSTATUS
XdpLwfStart(
    _In_ DRIVER_OBJECT *DriverObject,
    _In_z_ CONST WCHAR *RegistryPath
    );

VOID
XdpLwfStop(
    VOID
    );
