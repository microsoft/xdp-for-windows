//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_OFFLOAD_QEO_SETTINGS {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY Connections;
} XDP_OFFLOAD_QEO_SETTINGS;

typedef struct _XDP_INTERFACE_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_IFSET_HANDLE IfSetHandle;
    XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle;
    XDP_OFFLOAD_QEO_SETTINGS QeoSettings;
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
