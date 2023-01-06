//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
DefaultIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    DEFAULT_CONTEXT *Default = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

    case IOCTL_RX_FILTER:
        Status = RxIrpFilter(Default->Rx, Irp, IrpSp);
        break;

    case IOCTL_RX_GET_FRAME:
        Status = RxIrpGetFrame(Default->Rx, Irp, IrpSp);
        break;

    case IOCTL_RX_DEQUEUE_FRAME:
        Status = RxIrpDequeueFrame(Default->Rx, Irp, IrpSp);
        break;

    case IOCTL_RX_FLUSH:
        Status = RxIrpFlush(Default->Rx, Irp, IrpSp);
        break;

    case IOCTL_TX_ENQUEUE:
        Status = TxIrpEnqueue(Default->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_FLUSH:
        Status = TxIrpFlush(Default->Tx, Irp, IrpSp);
        break;

    case IOCTL_OID_SUBMIT_REQUEST:
        Status = OidIrpSubmitRequest(Default->Filter, Irp, IrpSp);
        break;

    case IOCTL_STATUS_SET_FILTER:
        Status = StatusIrpFilter(Default->Status, Irp, IrpSp);
        break;

    case IOCTL_STATUS_GET_INDICATION:
        Status = StatusIrpGetIndication(Default->Status, Irp, IrpSp);
        break;

    case IOCTL_DATAPATH_GET_STATE:
        Status = FilterIrpGetDatapathState(Default->Filter, Irp, IrpSp);
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
DefaultCleanup(
    _In_ DEFAULT_CONTEXT *Default
    )
{
    if (Default->Status != NULL) {
        StatusCleanup(Default->Status);
    }

    if (Default->Tx != NULL) {
        TxCleanup(Default->Tx);
    }

    if (Default->Rx != NULL) {
        RxCleanup(Default->Rx);
    }

    if (Default->Filter != NULL) {
        FilterDereferenceFilter(Default->Filter);
    }

    ExFreePoolWithTag(Default, POOLTAG_DEFAULT);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
DefaultIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    UNREFERENCED_PARAMETER(Irp);

    DefaultCleanup(IrpSp->FileObject->FsContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH DefaultFileDispatch = {
    .IoControl = DefaultIrpDeviceIoControl,
    .Close = DefaultIrpClose,
};

NTSTATUS
DefaultIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    DEFAULT_CONTEXT *Default = NULL;
    XDPFNLWF_OPEN_DEFAULT *OpenDefault;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenDefault)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenDefault = InputBuffer;

    Default = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Default), POOLTAG_DEFAULT);
    if (Default == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Default->Header.ObjectType = XDPFNLWF_FILE_TYPE_DEFAULT;
    Default->Header.Dispatch = &DefaultFileDispatch;
    KeInitializeSpinLock(&Default->Lock);

    Default->Filter = FilterFindAndReferenceFilter(OpenDefault->IfIndex);
    if (Default->Filter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Default->Rx = RxCreate(Default);
    if (Default->Rx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Default->Tx = TxCreate(Default);
    if (Default->Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Default->Status = StatusCreate(Default);
    if (Default->Status == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    IrpSp->FileObject->FsContext = Default;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Default != NULL) {
            DefaultCleanup(Default);
        }
    }

    return Status;
}
