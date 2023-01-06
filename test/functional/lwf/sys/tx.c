//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _DEFAULT_TX {
    DEFAULT_CONTEXT *Default;
    NBL_COUNTED_QUEUE Nbls;
} DEFAULT_TX;

UINT32
TxCleanupNblChain(
    _In_ NET_BUFFER_LIST *NblChain
    )
{
    UINT32 Count = 0;

    while (NblChain != NULL) {
        NET_BUFFER_LIST *Nbl = NblChain;
        NblChain = Nbl->Next;
        FnIoEnqueueFrameReturn(Nbl);
        Count++;
    }

    return Count;
}

VOID
TxCleanup(
    _In_ DEFAULT_TX *Tx
    )
{
    TxCleanupNblChain(NdisGetNblChainFromNblCountedQueue(&Tx->Nbls));
    NdisInitializeNblCountedQueue(&Tx->Nbls);
    ExFreePoolWithTag(Tx, POOLTAG_DEFAULT_TX);
}

DEFAULT_TX *
TxCreate(
    _In_ DEFAULT_CONTEXT *Default
    )
{
    DEFAULT_TX *Tx;
    NTSTATUS Status;

    Tx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Tx), POOLTAG_DEFAULT_TX);
    if (Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Tx->Default = Default;
    NdisInitializeNblCountedQueue(&Tx->Nbls);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Tx != NULL) {
            TxCleanup(Tx);
            Tx = NULL;
        }
    }

    return Tx;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
TxIrpEnqueue(
    _In_ DEFAULT_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    LWF_FILTER *Filter = Tx->Default->Filter;
    NDIS_HANDLE NblPool = Filter->NblPool;
    DATA_ENQUEUE_IN EnqueueIn = {0};
    NET_BUFFER_LIST *Nbl = NULL;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status =
        FnIoEnqueueFrameBegin(
            Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.DeviceIoControl.InputBufferLength,
            NblPool, &EnqueueIn, &Nbl);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (EnqueueIn.Frame.Input.RssHashQueueId != 0) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    NET_BUFFER_LIST_SET_HASH_FUNCTION(Nbl, NdisHashFunctionToeplitz);
    NET_BUFFER_LIST_SET_HASH_VALUE(Nbl, 0);
    NET_BUFFER_LIST_SET_HASH_TYPE(Nbl, NDIS_HASH_IPV4);

    Nbl->SourceHandle = Filter->NdisFilterHandle;

    KeAcquireSpinLock(&Tx->Default->Lock, &OldIrql);
    if (Tx->Nbls.NblCount < MAXUINT32) {
        NdisAppendSingleNblToNblCountedQueue(&Tx->Nbls, Nbl);
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_INTEGER_OVERFLOW;
    }
    KeReleaseSpinLock(&Tx->Default->Lock, OldIrql);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Nbl != NULL) {
            FnIoEnqueueFrameReturn(Nbl);
        }
    }

    FnIoEnqueueFrameEnd(&EnqueueIn);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
TxIrpFlush(
    _In_ DEFAULT_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    KIRQL OldIrql;
    LWF_FILTER *Filter = Tx->Default->Filter;
    CONST DATA_FLUSH_IN *In = Irp->AssociatedIrp.SystemBuffer;
    NBL_COUNTED_QUEUE Nbls;
    UINT32 NdisFlags = 0;
    NTSTATUS Status;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Options.Flags.LowResources || In->Options.Flags.RssCpu) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    NdisInitializeNblCountedQueue(&Nbls);

    KeAcquireSpinLock(&Tx->Default->Lock, &OldIrql);
    NdisAppendNblCountedQueueToNblCountedQueueFast(&Nbls, &Tx->Nbls);
    KeReleaseSpinLock(&Tx->Default->Lock, OldIrql);

    if (NdisIsNblCountedQueueEmpty(&Nbls)) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (!ExAcquireRundownProtectionEx(&Filter->NblRundown, (UINT32)Nbls.NblCount)) {
        TxCleanupNblChain(NdisGetNblChainFromNblCountedQueue(&Nbls));
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    if (In->Options.Flags.DpcLevel) {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        NdisFlags |= NDIS_SEND_FLAGS_DISPATCH_LEVEL;
    }

    NdisFSendNetBufferLists(
        Filter->NdisFilterHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
        NDIS_DEFAULT_PORT_NUMBER, NdisFlags);

    if (In->Options.Flags.DpcLevel) {
        KeLowerIrql(OldIrql);
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_Use_decl_annotations_
VOID
FilterSendNetBufferLists(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG SendFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    NdisFSendNetBufferLists(
        Filter->NdisFilterHandle, NetBufferLists, PortNumber, SendFlags);
}

_Use_decl_annotations_
VOID
FilterSendNetBufferListsComplete(
    NDIS_HANDLE FilterModuleContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG SendCompleteFlags
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NBL_QUEUE NblQueue;
    NBL_QUEUE PassList;
    NET_BUFFER_LIST *Nbl;

    NdisInitializeNblQueue(&NblQueue);
    NdisInitializeNblQueue(&PassList);

    NdisAppendNblChainToNblQueue(&NblQueue, NetBufferLists);

    while (!NdisIsNblQueueEmpty(&NblQueue)) {
        Nbl = NdisPopFirstNblFromNblQueue(&NblQueue);
        if (Nbl->SourceHandle == Filter->NdisFilterHandle) {
            FnIoEnqueueFrameReturn(Nbl);
            ExReleaseRundownProtectionEx(&Filter->NblRundown, 1);
        } else {
            NdisAppendSingleNblToNblQueue(&PassList, Nbl);
        }
    }

    if (!NdisIsNblQueueEmpty(&PassList)) {
        NdisFSendNetBufferListsComplete(
            Filter->NdisFilterHandle, NdisGetNblChainFromNblQueue(&PassList),
            SendCompleteFlags);
    }
}
