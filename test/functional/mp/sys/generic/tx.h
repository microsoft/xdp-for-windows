//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _GENERIC_TX GENERIC_TX;

VOID
GenericTxCleanup(
    _In_ GENERIC_TX *Tx
    );

GENERIC_TX *
GenericTxCreate(
    _In_ GENERIC_CONTEXT *Generic
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxFilter(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxGetFrame(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxDequeueFrame(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxFlush(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
