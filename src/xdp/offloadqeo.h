//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "offload.h"

VOID
XdpOffloadQeoInitializeSettings(
    _Inout_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings
    );

VOID
XdpOfloadQeoRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _Inout_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings
    );

NTSTATUS
XdpIrpInterfaceOffloadQeoSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
