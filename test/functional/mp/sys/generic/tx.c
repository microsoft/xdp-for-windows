//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

typedef struct _TX_FILTER {
    LIST_ENTRY Link;
    TX_FILTER_IN Params;
    UCHAR *ContiguousBuffer;
} TX_FILTER;

typedef struct _GENERIC_TX {
    GENERIC_CONTEXT *Generic;
    TX_FILTER Filter;
    //
    // This driver allows NBLs to be held indefinitely by user mode, which is a
    // bad practice. Unfortunately, it is necessary to hold NBLs for some
    // configurable interval in order to test XDP (wait completion, poke
    // optimization, etc.) so the only question is whether a watchdog is also
    // necessary. For now, don't bother.
    //
    NBL_COUNTED_QUEUE NblQueue;
    NBL_COUNTED_QUEUE NblReturn;
} GENERIC_TX;

VOID
_Requires_lock_held_(&Generic->Lock)
GenericTxCleanupFilter(
    _Inout_ TX_FILTER *Filter
    )
{
    if (!IsListEmpty(&Filter->Link)) {
        RemoveEntryList(&Filter->Link);
        InitializeListHead(&Filter->Link);
    }

    if (Filter->ContiguousBuffer != NULL) {
        ExFreePoolWithTag(Filter->ContiguousBuffer, POOLTAG_GENERIC_TX);
        Filter->ContiguousBuffer = NULL;
    }

    IoctlCleanupTxFilter(&Filter->Params);
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
    NBL_COUNTED_QUEUE ReturnQueue;
    KIRQL OldIrql;

    NdisInitializeNblCountedQueue(&ReturnQueue);

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    NdisAppendNblCountedQueueToNblCountedQueueFast(&ReturnQueue, &Tx->NblQueue);
    NdisAppendNblCountedQueueToNblCountedQueueFast(&ReturnQueue, &Tx->NblReturn);

    GenericTxCleanupFilter(&Tx->Filter);
    if (!IsListEmpty(&Tx->Filter.Link)) {
        RemoveEntryList(&Tx->Filter.Link);
        InitializeListHead(&Tx->Filter.Link);
    }

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    ExFreePoolWithTag(Tx, POOLTAG_GENERIC_TX);

    if (!NdisIsNblCountedQueueEmpty(&ReturnQueue)) {
        GenericTxCompleteNbls(
            AdapterGeneric, NdisGetNblChainFromNblCountedQueue(&ReturnQueue), ReturnQueue.NblCount);
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
    InitializeListHead(&Tx->Filter.Link);
    NdisInitializeNblCountedQueue(&Tx->NblQueue);
    NdisInitializeNblCountedQueue(&Tx->NblReturn);
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
GenericTxMatchFilter(
    _In_ TX_FILTER *Filter,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    UINT32 DataLength = min(NET_BUFFER_DATA_LENGTH(NetBuffer), Filter->Params.Length);
    UCHAR *Buffer;

    if (NetBuffer->Next != NULL) {
        //
        // Generic XDP does not send multiple NBs per NBL and will not for the
        // forseeable future, so skip over multi-NB NBLs.
        //
        return FALSE;
    }

    Buffer = NdisGetDataBuffer(NetBuffer, DataLength, Filter->ContiguousBuffer, 1, 0);
    ASSERT(Buffer != NULL);

    for (UINT32 Index = 0; Index < DataLength; Index++) {
        if ((Buffer[Index] & Filter->Params.Mask[Index]) != Filter->Params.Pattern[Index]) {
            return FALSE;
        }
    }

    return TRUE;
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
        GENERIC_TX *Tx = CONTAINING_RECORD(Entry, GENERIC_TX, Filter.Link);
        Entry = Entry->Flink;

        if (GenericTxMatchFilter(&Tx->Filter, Nbl)) {
            NdisAppendSingleNblToNblCountedQueue(&Tx->NblQueue, Nbl);
            return TRUE;
        }
    }

    return FALSE;
}

VOID
MpSendNetBufferLists(
   _Inout_ NDIS_HANDLE MiniportAdapterContext,
   _Inout_ NET_BUFFER_LIST *NetBufferLists,
   _In_ ULONG PortNumber,
   _In_ ULONG SendFlags
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NBL_COUNTED_QUEUE NblChain, ReturnChain;
    KIRQL OldIrql;

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
    TX_FILTER_IN FilterIn = {0};
    UCHAR *ContiguousBuffer = NULL;
    KIRQL OldIrql;

    Status =
        IoctlBounceTxFilter(
            Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.DeviceIoControl.InputBufferLength,
            &FilterIn);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (FilterIn.Length > 0) {
        ContiguousBuffer = ExAllocatePoolZero(NonPagedPoolNx, FilterIn.Length, POOLTAG_GENERIC_TX);
        if (ContiguousBuffer == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    }

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    GenericTxCleanupFilter(&Tx->Filter);

    Tx->Filter.Params = FilterIn;
    Tx->Filter.ContiguousBuffer = ContiguousBuffer;

    if (Tx->Filter.Params.Length > 0) {
        InsertTailList(&AdapterGeneric->TxFilterList, &Tx->Filter.Link);
    }

    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

Exit:

    if (!NT_SUCCESS(Status)) {
        IoctlCleanupTxFilter(&FilterIn);
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
    TX_GET_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    NET_BUFFER_LIST *Nbl;
    NET_BUFFER *NetBuffer;
    MDL *Mdl;
    UINT32 OutputSize;
    UINT32 DataBytes;
    UINT8 BufferCount;
    TX_FRAME *TxFrame;
    TX_BUFFER *TxBuffer;
    UCHAR *DataBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Nbl = Tx->NblQueue.Queue.First;
    for (UINT32 NblIndex = 0; NblIndex < In->Index; NblIndex++) {
        if (Nbl == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        Nbl = Nbl->Next;
    }

    if (Nbl == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    DataBytes = NET_BUFFER_DATA_LENGTH(NetBuffer);
    BufferCount = 0;

    OutputSize = sizeof(*TxFrame);
    OutputSize += NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    OutputSize += DataBytes;

    for (Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer); DataBytes > 0; Mdl = Mdl->Next) {
        OutputSize += sizeof(*TxBuffer);
        DataBytes -= min(Mdl->ByteCount, DataBytes);

        if (!NT_SUCCESS(RtlUInt8Add(BufferCount, 1, &BufferCount))) {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Exit;
        }
    }

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < OutputSize) {
        Irp->IoStatus.Information = OutputSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    TxFrame = Irp->AssociatedIrp.SystemBuffer;
    TxBuffer = (TX_BUFFER *)(TxFrame + 1);
    DataBuffer = (UCHAR *)(TxBuffer + BufferCount);

    TxFrame->BufferCount = BufferCount;
    TxFrame->Buffers = RTL_PTR_SUBTRACT(TxBuffer, TxFrame);

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    DataBytes = NET_BUFFER_DATA_LENGTH(NetBuffer);

    for (UINT32 BufferIndex = 0; BufferIndex < BufferCount; BufferIndex++) {
        UCHAR *MdlBuffer;

        MdlBuffer = MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority);
        if (MdlBuffer == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        TxBuffer[BufferIndex].DataOffset = 0;
        TxBuffer[BufferIndex].DataLength = min(Mdl->ByteCount, DataBytes);
        TxBuffer[BufferIndex].BufferLength = TxBuffer[BufferIndex].DataLength;

        if (BufferIndex == 0) {
            TxBuffer[BufferIndex].DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
            TxBuffer[BufferIndex].BufferLength += TxBuffer[BufferIndex].DataOffset;
            DataBuffer += TxBuffer[BufferIndex].DataOffset;
        }

        RtlCopyMemory(
            DataBuffer, MdlBuffer + TxBuffer[BufferIndex].DataOffset,
            TxBuffer[BufferIndex].DataLength);
        TxBuffer[BufferIndex].VirtualAddress = RTL_PTR_SUBTRACT(DataBuffer, TxFrame);

        DataBuffer += TxBuffer[BufferIndex].DataLength;
        DataBytes -= TxBuffer[BufferIndex].DataLength;
    }

    Irp->IoStatus.Information = OutputSize;
    Status = STATUS_SUCCESS;

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
    TX_DEQUEUE_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    NBL_COUNTED_QUEUE NblChain;
    NET_BUFFER_LIST *Nbl;
    KIRQL OldIrql;

    NdisInitializeNblCountedQueue(&NblChain);

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = STATUS_NOT_FOUND;

    for (UINT32 NblIndex = 0; !NdisIsNblCountedQueueEmpty(&Tx->NblQueue); NblIndex++) {
        Nbl = NdisPopFirstNblFromNblCountedQueue(&Tx->NblQueue);

        if (NblIndex == In->Index) {
            NdisAppendSingleNblToNblCountedQueue(&Tx->NblReturn, Nbl);
            Status = STATUS_SUCCESS;
        } else {
            NdisAppendSingleNblToNblCountedQueue(&NblChain, Nbl);
        }
    }

    NdisAppendNblCountedQueueToNblCountedQueueFast(&Tx->NblQueue, &NblChain);

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
    NBL_COUNTED_QUEUE FlushChain;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    NdisInitializeNblCountedQueue(&FlushChain);

    KeAcquireSpinLock(&AdapterGeneric->Lock, &OldIrql);
    NdisAppendNblCountedQueueToNblCountedQueueFast(&FlushChain, &Tx->NblReturn);
    KeReleaseSpinLock(&AdapterGeneric->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&FlushChain)) {
        Status = STATUS_SUCCESS;
        GenericTxCompleteNbls(
            AdapterGeneric, NdisGetNblChainFromNblCountedQueue(&FlushChain), FlushChain.NblCount);
    } else {
        Status = STATUS_NOT_FOUND;
    }

    return Status;
}
