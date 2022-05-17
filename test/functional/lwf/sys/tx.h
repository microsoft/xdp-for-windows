//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

FILTER_SEND_NET_BUFFER_LISTS FilterSendNetBufferLists;
FILTER_SEND_NET_BUFFER_LISTS_COMPLETE FilterSendNetBufferListsComplete;

typedef struct _DEFAULT_TX DEFAULT_TX;

VOID
TxCleanup(
    _In_ DEFAULT_TX *Tx
    );

DEFAULT_TX *
TxCreate(
    _In_ DEFAULT_CONTEXT *Default
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
TxIrpEnqueue(
    _In_ DEFAULT_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
TxIrpFlush(
    _In_ DEFAULT_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
