//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdpfnlwfioctl.h>

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FILE_CREATE_ROUTINE(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FILE_IRP_ROUTINE(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

typedef struct FILE_DISPATCH {
    FILE_IRP_ROUTINE *IoControl;
    FILE_IRP_ROUTINE *Cleanup;
    FILE_IRP_ROUTINE *Close;
} FILE_DISPATCH;

typedef struct FILE_OBJECT_HEADER {
    XDPFNLWF_FILE_TYPE ObjectType;
    CONST FILE_DISPATCH *Dispatch;
} FILE_OBJECT_HEADER;
