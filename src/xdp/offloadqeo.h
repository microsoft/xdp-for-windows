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
XdpOffloadQeoRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    );

NTSTATUS
XdpInterfaceOffloadQeoSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _In_ const XDP_QUIC_CONNECTION *ConnectionsIn,
    _In_ UINT32 InputBufferLength,
    _Inout_ UINT32 *OutputBufferLength
    );

NTSTATUS
XdpIrpInterfaceOffloadQeoSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
