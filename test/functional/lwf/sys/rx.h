//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

FILTER_RETURN_NET_BUFFER_LISTS FilterReturnNetBufferLists;
FILTER_RECEIVE_NET_BUFFER_LISTS FilterReceiveNetBufferLists;

typedef struct _DEFAULT_RX DEFAULT_RX;

VOID
RxCleanup(
    _In_ DEFAULT_RX *Rx
    );

DEFAULT_RX *
RxCreate(
    _In_ DEFAULT_CONTEXT *Default
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpFilter(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpGetFrame(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpDequeueFrame(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpFlush(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
