//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XSK XSK;

VOID
XskReceive(
    _In_ XDP_REDIRECT_BATCH *Batch
    );

BOOLEAN
XskReceiveBatchedExclusive(
    _In_ VOID *Target
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskFillTxCompletion(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32
XskFillTx(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry,
    _In_ UINT32 FrameQuota
    );

NTSTATUS
XskReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskHandle
    );

NTSTATUS
XskValidateDatapathHandle(
    _In_ HANDLE XskHandle
    );

BOOLEAN
XskCanBypass(
    _In_ HANDLE XskHandle,
    _In_ XDP_RX_QUEUE *RxQueue
    );

VOID
XskDereferenceDatapathHandle(
    _In_ HANDLE XskHandle
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XskCreate(
    _Out_ XSK **Xsk,
    _In_opt_ XSK_NOTIFY_CALLBACK *NotifyCallback,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
    );

NTSTATUS
XskSetSockopt(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    );

NTSTATUS
XskGetSockopt(
    _In_ XSK *Xsk,
    _In_ UINT32 Option,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    );

NTSTATUS
XskBindSocket(
    _In_ XSK *Xsk,
    _In_ XSK_BIND_IN Bind
    );

NTSTATUS
XskActivateSocket(
    _In_ XSK *Xsk,
    _In_ XSK_ACTIVATE_IN Activate
    );

_Success_(return == STATUS_SUCCESS)
NTSTATUS
XskNotify(
    _In_ XSK *Xsk,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_ ULONG_PTR *Information,
    _Inout_opt_ IRP *Irp,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ BOOLEAN UseCallback
    );

XDP_FILE_CREATE_ROUTINE XskIrpCreateSocket;

BOOLEAN
XskFastIo(
    _In_ XDP_FILE_OBJECT_HEADER *FileObjectHeader,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ VOID *OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ IO_STATUS_BLOCK *IoStatus
    );

NTSTATUS
XskStart(
    VOID
    );

VOID
XskStop(
    VOID
    );
