//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "tx.tmh"

typedef struct _GENERIC_TX {
    GENERIC_CONTEXT *Generic;
    //
    // This driver allows NBLs to be held indefinitely by user mode, which is a
    // bad practice. Unfortunately, it is necessary to hold NBLs for some
    // configurable interval in order to test XDP (wait completion, poke
    // optimization, etc.) so the only question is whether a watchdog is also
    // necessary. For now, don't bother.
    //
    LIST_ENTRY DataFilterLink;
    DATA_FILTER *DataFilter;
} GENERIC_TX;

static
VOID
_Requires_lock_held_(&Tx->Generic->Adapter->Generic->Lock)
GenericTxClearFilter(
    _Inout_ GENERIC_TX *Tx
    )
{
    if (!IsListEmpty(&Tx->DataFilterLink)) {
        RemoveEntryList(&Tx->DataFilterLink);
        InitializeListHead(&Tx->DataFilterLink);
    }

    if (Tx->DataFilter != NULL) {
        FnIoDeleteFilter(Tx->DataFilter);
        Tx->DataFilter = NULL;
    }
}

VOID
GenericTxCompleteNbls(
    _In_ ADAPTER_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NblChain,
    _In_ SIZE_T Count
    )
{
    ASSERT(Count <= MAXULONG);
    NdisMSendNetBufferListsComplete(Generic->Adapter->MiniportHandle, NblChain, 0);
    ExReleaseRundownProtectionEx(&Generic->NblRundown, (ULONG)Count);
}

VOID
GenericTxCleanup(
    _In_ GENERIC_TX *Tx
    )
{
    ADAPTER_GENERIC *AdapterGeneric = Tx->Generic->Adapter->Generic;
    NET_BUFFER_LIST *NblChain = NULL;
    SIZE_T NblCount = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (Tx->DataFilter != NULL) {
        NblCount = FnIoFlushAllFrames(Tx->DataFilter, &NblChain);
    }

    GenericTxClearFilter(Tx);

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    ExFreePoolWithTag(Tx, POOLTAG_GENERIC_TX);

    if (NblCount > 0) {
        GenericTxCompleteNbls(AdapterGeneric, NblChain, NblCount);
    }
}

GENERIC_TX *
GenericTxCreate(
    _In_ GENERIC_CONTEXT *Generic
    )
{
    GENERIC_TX *Tx;
    NTSTATUS Status;

    Tx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Tx), POOLTAG_GENERIC_TX);
    if (Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Tx->Generic = Generic;
    InitializeListHead(&Tx->DataFilterLink);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Tx != NULL) {
            GenericTxCleanup(Tx);
            Tx = NULL;
        }
    }

    return Tx;
}

BOOLEAN
_Requires_lock_held_(&Generic->Lock)
GenericTxFilterNbl(
    _In_ ADAPTER_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    LIST_ENTRY *Entry = Generic->TxFilterList.Flink;

    while (Entry != &Generic->TxFilterList) {
        GENERIC_TX *Tx = CONTAINING_RECORD(Entry, GENERIC_TX, DataFilterLink);
        Entry = Entry->Flink;
        if (FnIoFilterNbl(Tx->DataFilter, Nbl)) {
            return TRUE;
        }
    }

    return FALSE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(MINIPORT_SEND_NET_BUFFER_LISTS)
VOID
MpSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ ULONG PortNumber,
    _In_ ULONG SendFlags
    )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NBL_COUNTED_QUEUE NblChain, ReturnChain;
    KIRQL OldIrql;

    TraceEnter(TRACE_DATAPATH, "Adapter=%p", Adapter);
    TraceNbls(NetBufferLists);

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    NdisInitializeNblCountedQueue(&NblChain);
    NdisInitializeNblCountedQueue(&ReturnChain);
    NdisAppendNblChainToNblCountedQueue(&NblChain, NetBufferLists);

    if (!ExAcquireRundownProtectionEx(&Adapter->Generic->NblRundown, (ULONG)NblChain.NblCount)) {
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, NetBufferLists, 0);
        return;
    }

    KeAcquireSpinLock(&Adapter->Generic->Lock, &OldIrql);

    while (!NdisIsNblCountedQueueEmpty(&NblChain)) {
        NET_BUFFER_LIST *Nbl = NdisPopFirstNblFromNblCountedQueue(&NblChain);

        if (!GenericTxFilterNbl(Adapter->Generic, Nbl)) {
            NdisAppendSingleNblToNblCountedQueue(&ReturnChain, Nbl);
        }
    }

    KeReleaseSpinLock(&Adapter->Generic->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&ReturnChain)) {
        GenericTxCompleteNbls(
            Adapter->Generic, NdisGetNblChainFromNblCountedQueue(&ReturnChain),
            ReturnChain.NblCount);
    }

    TraceExitSuccess(TRACE_DATAPATH);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxFilter(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_GENERIC *AdapterGeneric = Tx->Generic->Adapter->Generic;
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

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    GenericTxClearFilter(Tx);

    if (!ClearOnly) {
        Tx->DataFilter = DataFilter;
        DataFilter = NULL;
        InsertTailList(&AdapterGeneric->TxFilterList, &Tx->DataFilterLink);
    }

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    Status = STATUS_SUCCESS;

Exit:

    if (DataFilter != NULL) {
        FnIoDeleteFilter(DataFilter);
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxGetFrame(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_GENERIC *AdapterGeneric = Tx->Generic->Adapter->Generic;
    DATA_GET_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoGetFilteredFrame(Tx->DataFilter, In->Index, Irp, IrpSp);

Exit:

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxDequeueFrame(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_GENERIC *AdapterGeneric = Tx->Generic->Adapter->Generic;
    DATA_DEQUEUE_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoDequeueFilteredFrame(Tx->DataFilter, In->Index);

Exit:

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpTxFlush(
    _In_ GENERIC_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_GENERIC *AdapterGeneric = Tx->Generic->Adapter->Generic;
    NET_BUFFER_LIST *NblChain;
    SIZE_T NblCount;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    NblCount = FnIoFlushDequeuedFrames(Tx->DataFilter, &NblChain);

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    if (NblCount > 0) {
        Status = STATUS_SUCCESS;
        GenericTxCompleteNbls(AdapterGeneric, NblChain, NblCount);
    } else {
        Status = STATUS_NOT_FOUND;
    }

Exit:

    return Status;
}
