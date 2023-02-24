//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "rx.tmh"

typedef struct _DEFAULT_RX {
    DEFAULT_CONTEXT *Default;
    //
    // This driver allows NBLs to be held indefinitely by user mode, which is a
    // bad practice. Unfortunately, it is necessary to hold NBLs for some
    // configurable interval in order to test XDP (wait completion, poke
    // optimization, etc.) so the only question is whether a watchdog is also
    // necessary. For now, don't bother.
    //
    LIST_ENTRY DataFilterLink;
    DATA_FILTER *DataFilter;
} DEFAULT_RX;

static
VOID
_Requires_lock_held_(&Rx->Default->Filter->Lock)
RxClearFilter(
    _Inout_ DEFAULT_RX *Rx
    )
{
    if (!IsListEmpty(&Rx->DataFilterLink)) {
        RemoveEntryList(&Rx->DataFilterLink);
        InitializeListHead(&Rx->DataFilterLink);
    }

    if (Rx->DataFilter != NULL) {
        FnIoDeleteFilter(Rx->DataFilter);
        Rx->DataFilter = NULL;
    }
}

static
BOOLEAN
_Requires_lock_held_(&Filter->Lock)
RxFilterNbl(
    _In_ LWF_FILTER *Filter,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    LIST_ENTRY *Entry = Filter->RxFilterList.Flink;

    while (Entry != &Filter->RxFilterList) {
        DEFAULT_RX *Rx = CONTAINING_RECORD(Entry, DEFAULT_RX, DataFilterLink);
        Entry = Entry->Flink;
        if (FnIoFilterNbl(Rx->DataFilter, Nbl)) {
            return TRUE;
        }
    }

    return FALSE;
}

VOID
RxCleanup(
    _In_ DEFAULT_RX *Rx
    )
{
    LWF_FILTER *Filter = Rx->Default->Filter;
    NET_BUFFER_LIST *NblChain = NULL;
    SIZE_T NblCount = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (Rx->DataFilter != NULL) {
        NblCount = FnIoFlushAllFrames(Rx->DataFilter, &NblChain);
    }

    RxClearFilter(Rx);

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    ExFreePoolWithTag(Rx, POOLTAG_DEFAULT_RX);

    if (NblCount > 0) {
        ASSERT(NblCount <= MAXULONG);
        NdisFIndicateReceiveNetBufferLists(
            Filter->NdisFilterHandle, NblChain, NDIS_DEFAULT_PORT_NUMBER,
            (ULONG)NblCount, 0);
    }
}

DEFAULT_RX *
RxCreate(
    _In_ DEFAULT_CONTEXT *Default
    )
{
    DEFAULT_RX *Rx;
    NTSTATUS Status;

    Rx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Rx), POOLTAG_DEFAULT_RX);
    if (Rx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Rx->Default = Default;
    InitializeListHead(&Rx->DataFilterLink);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Rx != NULL) {
            RxCleanup(Rx);
            Rx = NULL;
        }
    }

    return Rx;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpFilter(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LWF_FILTER *Filter = Rx->Default->Filter;
    CONST DATA_FILTER_IN *In = Irp->AssociatedIrp.SystemBuffer;
    DATA_FILTER *DataFilter = NULL;
    KIRQL OldIrql;
    BOOLEAN ClearOnly = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Length == 0) {
        ClearOnly = TRUE;
    } else {
        DataFilter =
            FnIoCreateFilter(
                Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        if (DataFilter == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    RxClearFilter(Rx);

    if (!ClearOnly) {
        Rx->DataFilter = DataFilter;
        DataFilter = NULL;
        InsertTailList(&Filter->RxFilterList, &Rx->DataFilterLink);
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    Status = STATUS_SUCCESS;

Exit:

    if (DataFilter != NULL) {
        FnIoDeleteFilter(DataFilter);
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpGetFrame(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LWF_FILTER *Filter = Rx->Default->Filter;
    DATA_GET_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (Rx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoGetFilteredFrame(Rx->DataFilter, In->Index, Irp, IrpSp);

Exit:

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpDequeueFrame(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LWF_FILTER *Filter = Rx->Default->Filter;
    DATA_DEQUEUE_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (Rx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoDequeueFilteredFrame(Rx->DataFilter, In->Index);

Exit:

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RxIrpFlush(
    _In_ DEFAULT_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LWF_FILTER *Filter = Rx->Default->Filter;
    NET_BUFFER_LIST *NblChain;
    SIZE_T NblCount;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (Rx->DataFilter == NULL) {
        KeReleaseSpinLock(&Filter->Lock, OldIrql);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    NblCount = FnIoFlushDequeuedFrames(Rx->DataFilter, &NblChain);

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (NblCount > 0) {
        ASSERT(NblCount <= MAXULONG);
        NdisFIndicateReceiveNetBufferLists(
            Filter->NdisFilterHandle, NblChain, NDIS_DEFAULT_PORT_NUMBER,
            (ULONG)NblCount, 0);
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_NOT_FOUND;
    }

Exit:

    return Status;
}

_Use_decl_annotations_
VOID
FilterReturnNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG ReturnFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    TraceEnter(TRACE_DATAPATH, "Filter=%p", Filter);
    TraceNbls(NetBufferLists);

    NdisFReturnNetBufferLists(
        Filter->NdisFilterHandle, NetBufferLists, ReturnFlags);

    TraceExitSuccess(TRACE_DATAPATH);
}

_Use_decl_annotations_
VOID
FilterReceiveNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG NumberOfNetBufferLists,
    ULONG ReceiveFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NBL_COUNTED_QUEUE NblChain, IndicateChain;
    KIRQL OldIrql;

    TraceEnter(TRACE_DATAPATH, "Filter=%p", Filter);
    TraceNbls(NetBufferLists);

    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    NdisInitializeNblCountedQueue(&NblChain);
    NdisInitializeNblCountedQueue(&IndicateChain);
    NdisAppendNblChainToNblCountedQueue(&NblChain, NetBufferLists);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    while (!NdisIsNblCountedQueueEmpty(&NblChain)) {
        NET_BUFFER_LIST *Nbl = NdisPopFirstNblFromNblCountedQueue(&NblChain);

        if (!RxFilterNbl(Filter, Nbl)) {
            NdisAppendSingleNblToNblCountedQueue(&IndicateChain, Nbl);
        }
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&IndicateChain)) {
        ASSERT(IndicateChain.NblCount <= MAXULONG);
        NdisFIndicateReceiveNetBufferLists(
            Filter->NdisFilterHandle,
            NdisGetNblChainFromNblCountedQueue(&IndicateChain), PortNumber,
            (ULONG)IndicateChain.NblCount, ReceiveFlags);
    }

    TraceExitSuccess(TRACE_DATAPATH);
}
