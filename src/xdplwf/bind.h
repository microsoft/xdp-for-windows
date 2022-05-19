//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

NTSTATUS
XdpLwfBindStart(
    _In_ DRIVER_OBJECT *DriverObject
    );

VOID
XdpLwfBindStop(
    VOID
    );
