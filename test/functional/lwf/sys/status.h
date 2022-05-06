//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

FILTER_STATUS FilterStatus;

typedef struct _DEFAULT_STATUS DEFAULT_STATUS;

VOID
StatusCleanup(
    _In_ DEFAULT_STATUS *StatusFile
    );

DEFAULT_STATUS *
StatusCreate(
    _In_ DEFAULT_CONTEXT *Default
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
StatusIrpFilter(
    _In_ DEFAULT_STATUS *StatusFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
StatusIrpGetIndication(
    _In_ DEFAULT_STATUS *StatusFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
