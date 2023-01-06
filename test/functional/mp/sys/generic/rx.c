//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _GENERIC_RX {
    GENERIC_CONTEXT *Generic;
    NBL_COUNTED_QUEUE Nbls;
} GENERIC_RX;

UINT32
GenericRxCleanupNblChain(
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
GenericRxCleanup(
    _In_ GENERIC_RX *Rx
    )
{
    GenericRxCleanupNblChain(NdisGetNblChainFromNblCountedQueue(&Rx->Nbls));
    NdisInitializeNblCountedQueue(&Rx->Nbls);
    ExFreePoolWithTag(Rx, POOLTAG_GENERIC_RX);
}

GENERIC_RX *
GenericRxCreate(
    _In_ GENERIC_CONTEXT *Generic
    )
{
    GENERIC_RX *Rx;
    NTSTATUS Status;

    Rx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Rx), POOLTAG_GENERIC_RX);
    if (Rx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Rx->Generic = Generic;
    NdisInitializeNblCountedQueue(&Rx->Nbls);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Rx != NULL) {
            GenericRxCleanup(Rx);
            Rx = NULL;
        }
    }

    return Rx;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpRxEnqueue(
    _In_ GENERIC_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    ADAPTER_CONTEXT *Adapter = Rx->Generic->Adapter;
    NDIS_HANDLE NblPool = Adapter->Generic->NblPool;
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

    if (EnqueueIn.Frame.Input.RssHashQueueId != MAXUINT32) {
        if (EnqueueIn.Frame.Input.RssHashQueueId >= Adapter->NumRssQueues) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        NET_BUFFER_LIST_SET_HASH_FUNCTION(Nbl, NdisHashFunctionToeplitz);
        NET_BUFFER_LIST_SET_HASH_VALUE(
            Nbl, Adapter->RssQueues[EnqueueIn.Frame.Input.RssHashQueueId].RssHash);
        NET_BUFFER_LIST_SET_HASH_TYPE(Nbl, NDIS_HASH_IPV4);
    }

    KeAcquireSpinLock(&Rx->Generic->Lock, &OldIrql);
    if (Rx->Nbls.NblCount < MAXUINT32) {
        NdisAppendSingleNblToNblCountedQueue(&Rx->Nbls, Nbl);
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_INTEGER_OVERFLOW;
    }
    KeReleaseSpinLock(&Rx->Generic->Lock, OldIrql);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Nbl != NULL) {
            FnIoEnqueueFrameReturn(Nbl);
        }
    }

    FnIoEnqueueFrameEnd(&EnqueueIn);

    return Status;
}

VOID
MpReturnNetBufferLists(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NET_BUFFER_LIST *NetBufferLists,
   _In_ ULONG ReturnFlags
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    UINT32 Count;

    UNREFERENCED_PARAMETER(ReturnFlags);

    Count = GenericRxCleanupNblChain(NetBufferLists);

    ExReleaseRundownProtectionEx(&Adapter->Generic->NblRundown, Count);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpRxFlush(
    _In_ GENERIC_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    KIRQL OldIrql;
    ADAPTER_CONTEXT *Adapter = Rx->Generic->Adapter;
    CONST DATA_FLUSH_IN *In = Irp->AssociatedIrp.SystemBuffer;
    NBL_COUNTED_QUEUE Nbls;
    UINT32 NdisFlags = 0;
    BOOLEAN SetAffinity = FALSE;
    GROUP_AFFINITY OldAffinity;
    NTSTATUS Status;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Options.Flags.RssCpu) {
        GROUP_AFFINITY Affinity = {0};
        PROCESSOR_NUMBER ProcessorNumber;

        if (In->Options.RssCpuQueueId >= Adapter->NumRssQueues) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        NT_VERIFY(NT_SUCCESS(KeGetProcessorNumberFromIndex(
            Adapter->RssQueues[In->Options.RssCpuQueueId].ProcessorIndex, &ProcessorNumber)));

        Affinity.Group = ProcessorNumber.Group;
        Affinity.Mask = 1ui64 << ProcessorNumber.Number;
        KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);
        SetAffinity = TRUE;
    }

    NdisInitializeNblCountedQueue(&Nbls);

    KeAcquireSpinLock(&Rx->Generic->Lock, &OldIrql);
    NdisAppendNblCountedQueueToNblCountedQueueFast(&Nbls, &Rx->Nbls);
    KeReleaseSpinLock(&Rx->Generic->Lock, OldIrql);

    if (NdisIsNblCountedQueueEmpty(&Nbls)) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (!ExAcquireRundownProtectionEx(&Adapter->Generic->NblRundown, (UINT32)Nbls.NblCount)) {
        GenericRxCleanupNblChain(NdisGetNblChainFromNblCountedQueue(&Nbls));
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    if (In->Options.Flags.LowResources) {
        NET_BUFFER_LIST *Nbl = NdisGetNblChainFromNblCountedQueue(&Nbls);

        //
        // Create a shadow list of NBLs within the NBL chain. After indicating
        // the NBLs, verify the NBL chain matches the original.
        //
        while (Nbl != NULL) {
            NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = Nbl->Next;
            Nbl = Nbl->Next;
        }

        NdisFlags |= NDIS_RECEIVE_FLAGS_RESOURCES;
    }

    if (In->Options.Flags.DpcLevel) {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    }

    NdisMIndicateReceiveNetBufferLists(
        Adapter->MiniportHandle, NdisGetNblChainFromNblCountedQueue(&Nbls),
        NDIS_DEFAULT_PORT_NUMBER, (UINT32)Nbls.NblCount, NdisFlags);

    if (In->Options.Flags.DpcLevel) {
        KeLowerIrql(OldIrql);
    }

    if (In->Options.Flags.LowResources) {
        NET_BUFFER_LIST *Nbl = NdisGetNblChainFromNblCountedQueue(&Nbls);
        UINT32 Count = 0;

        ExReleaseRundownProtectionEx(&Adapter->Generic->NblRundown, (UINT32)Nbls.NblCount);

        //
        // Verify the returned NBL chain matches the original.
        //
        while (Nbl != NULL) {
            if (NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] != Nbl->Next) {
                Status = STATUS_IO_DEVICE_ERROR;
                goto Exit;
            }

            Nbl = Nbl->Next;
            Count++;
        }

        if (Count != (UINT32)Nbls.NblCount) {
            Status = STATUS_IO_DEVICE_ERROR;
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    if (SetAffinity) {
        KeRevertToUserGroupAffinityThread(&OldAffinity);
    }

    return Status;
}
