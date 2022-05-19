//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "status.tmh"

typedef struct _DEFAULT_STATUS_INDICATION DEFAULT_STATUS_INDICATION;

typedef struct _DEFAULT_STATUS_INDICATION {
    LIST_ENTRY Link;
    UCHAR *StatusBuffer;
    UINT32 StatusBufferSize;
} DEFAULT_STATUS_INDICATION;

typedef struct _DEFAULT_STATUS {
    DEFAULT_CONTEXT *Default;
    LIST_ENTRY StatusFilterLink;
    NDIS_STATUS StatusCodeFilter;
    BOOLEAN BlockIndications;
    BOOLEAN QueueIndications;
    LIST_ENTRY StatusIndicationList;
} DEFAULT_STATUS;

static
VOID
StatusIndicationCleanup(
    _In_ DEFAULT_STATUS_INDICATION *Indication
    )
{
    if (Indication->StatusBuffer != NULL) {
        ExFreePoolWithTag(Indication->StatusBuffer, POOLTAG_DEFAULT_STATUS);
    }

    ExFreePoolWithTag(Indication, POOLTAG_DEFAULT_STATUS);
}

static
_Requires_lock_held_(&StatusFile->Default->Filter->Lock)
VOID
StatusFlush(
    _In_ DEFAULT_STATUS *StatusFile
    )
{
    while (!IsListEmpty(&StatusFile->StatusIndicationList)) {
        LIST_ENTRY *Entry = RemoveHeadList(&StatusFile->StatusIndicationList);
        DEFAULT_STATUS_INDICATION *Indication =
            CONTAINING_RECORD(Entry, DEFAULT_STATUS_INDICATION, Link);

        StatusIndicationCleanup(Indication);
    }
}

VOID
StatusCleanup(
    _In_ DEFAULT_STATUS *StatusFile
    )
{
    LWF_FILTER *Filter = StatusFile->Default->Filter;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "StatusFile=%p", StatusFile);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (!IsListEmpty(&StatusFile->StatusFilterLink)) {
        RemoveEntryList(&StatusFile->StatusFilterLink);
        InitializeListHead(&StatusFile->StatusFilterLink);
    }

    StatusFlush(StatusFile);

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    ExFreePoolWithTag(StatusFile, POOLTAG_DEFAULT_STATUS);

    TraceExitSuccess(TRACE_CONTROL);
}

DEFAULT_STATUS *
StatusCreate(
    _In_ DEFAULT_CONTEXT *Default
    )
{
    DEFAULT_STATUS *StatusFile;
    NTSTATUS Status;

    TraceEnter(TRACE_CONTROL, "Default=%p", Default);

    StatusFile = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*StatusFile), POOLTAG_DEFAULT_STATUS);
    if (StatusFile == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    StatusFile->Default = Default;
    InitializeListHead(&StatusFile->StatusFilterLink);
    InitializeListHead(&StatusFile->StatusIndicationList);
    Status = STATUS_SUCCESS;
    TraceVerbose(TRACE_CONTROL, "Default=%p StatusFile=%p", Default, StatusFile);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (StatusFile != NULL) {
            StatusCleanup(StatusFile);
            StatusFile = NULL;
        }
    }

    TraceExitStatus(TRACE_CONTROL);

    return StatusFile;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
StatusIrpFilter(
    _In_ DEFAULT_STATUS *StatusFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    CONST STATUS_FILTER_IN *In = Irp->AssociatedIrp.SystemBuffer;
    LWF_FILTER *Filter = StatusFile->Default->Filter;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "StatusFile=%p", StatusFile);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    StatusFile->StatusCodeFilter = In->StatusCode;
    StatusFile->BlockIndications = !!In->BlockIndications;
    StatusFile->QueueIndications = !!In->QueueIndications;
    StatusFlush(StatusFile);

    if (IsListEmpty(&StatusFile->StatusFilterLink)) {
        InsertTailList(&Filter->StatusFilterList, &StatusFile->StatusFilterLink);
    }

    TraceVerbose(
        TRACE_CONTROL,
        "StatusFile=%p StatusCodeFilter=0x%x BlockIndications=%u QueueIndications=%u",
        StatusFile, In->StatusCode, In->BlockIndications, In->QueueIndications);

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
StatusIrpGetIndication(
    _In_ DEFAULT_STATUS *StatusFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    LWF_FILTER *Filter = StatusFile->Default->Filter;
    UINT32 OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;
    DEFAULT_STATUS_INDICATION *Indication;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "StatusFile=%p", StatusFile);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (IsListEmpty(&StatusFile->StatusIndicationList)) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Indication =
        CONTAINING_RECORD(StatusFile->StatusIndicationList.Flink, DEFAULT_STATUS_INDICATION, Link);

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0 &&
        Indication->StatusBufferSize > 0) {
        *BytesReturned = Indication->StatusBufferSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < Indication->StatusBufferSize) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    RtlCopyMemory(
        Irp->AssociatedIrp.SystemBuffer, Indication->StatusBuffer, Indication->StatusBufferSize);
    *BytesReturned = Indication->StatusBufferSize;
    RemoveEntryList(&Indication->Link);
    StatusIndicationCleanup(Indication);
    Status = STATUS_SUCCESS;

Exit:

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(FILTER_STATUS)
VOID
FilterStatus(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ NDIS_STATUS_INDICATION *StatusIndication
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    LIST_ENTRY *Entry;
    KIRQL OldIrql;
    DEFAULT_STATUS_INDICATION *CopyIndication = NULL;
    BOOLEAN Block = FALSE;

    TraceEnter(
        TRACE_CONTROL,
        "Filter=%p StatusCode=0x%x StatusBufferLength=%u",
        Filter, StatusIndication->StatusCode, StatusIndication->StatusBufferSize);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    Entry = Filter->StatusFilterList.Flink;

    while (Entry != &Filter->StatusFilterList) {
        DEFAULT_STATUS *StatusFile = CONTAINING_RECORD(Entry, DEFAULT_STATUS, StatusFilterLink);
        Entry = Entry->Flink;

        if (StatusFile->StatusCodeFilter == StatusIndication->StatusCode) {
            Block = StatusFile->BlockIndications;

            if (StatusFile->QueueIndications) {
                CopyIndication =
                    ExAllocatePoolZero(
                        NonPagedPoolNx, sizeof(*CopyIndication), POOLTAG_DEFAULT_STATUS);

                if (CopyIndication == NULL) {
                    break;
                }

                if (StatusIndication->StatusBufferSize > 0) {
                    CopyIndication->StatusBuffer =
                        ExAllocatePoolZero(
                            NonPagedPoolNx, StatusIndication->StatusBufferSize,
                            POOLTAG_DEFAULT_STATUS);

                    if (CopyIndication->StatusBuffer == NULL) {
                        break;
                    }

                    CopyIndication->StatusBufferSize = StatusIndication->StatusBufferSize;

                    RtlCopyMemory(
                        CopyIndication->StatusBuffer, StatusIndication->StatusBuffer,
                        CopyIndication->StatusBufferSize);
                }

                InsertTailList(&StatusFile->StatusIndicationList, &CopyIndication->Link);
                CopyIndication = NULL;
            }

            break;
        }
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (!Block) {
        NdisFIndicateStatus(Filter->NdisFilterHandle, StatusIndication);
    }

    if (CopyIndication != NULL) {
        StatusIndicationCleanup(CopyIndication);
    }

    TraceExitSuccess(TRACE_CONTROL);
}
