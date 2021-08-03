//
// Copyright (C) Microsoft Corporation. All rights reserved.
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
