//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "bind.h"
#include "dispatch.h"

typedef struct _XDP_OFFLOAD_QEO_SETTINGS {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY Connections;
} XDP_OFFLOAD_QEO_SETTINGS;

typedef struct _XDP_OFFLOAD_IF_SETTINGS {
    XDP_OFFLOAD_QEO_SETTINGS Qeo;
} XDP_OFFLOAD_IF_SETTINGS;

typedef struct _XDP_INTERFACE_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_IFSET_HANDLE IfSetHandle;
    XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle;
} XDP_INTERFACE_OBJECT;

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateInterface(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    );

VOID
XdpOffloadInitializeIfSettings(
    _Out_ XDP_OFFLOAD_IF_SETTINGS *OffloadIfSettings
    );

VOID
XdpOffloadRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    );
