//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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
