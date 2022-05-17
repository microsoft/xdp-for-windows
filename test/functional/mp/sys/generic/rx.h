//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "generic.h"

typedef struct _GENERIC_RX GENERIC_RX;

VOID
GenericRxCleanup(
    _In_ GENERIC_RX *Rx
    );

GENERIC_RX *
GenericRxCreate(
    _In_ GENERIC_CONTEXT *Generic
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpRxEnqueue(
    _In_ GENERIC_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpRxFlush(
    _In_ GENERIC_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

extern CONST UINT16 GenericRxNblContextSize;
