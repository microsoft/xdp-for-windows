//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

typedef struct _GENERIC_RX {
    GENERIC_CONTEXT *Generic;
    NBL_COUNTED_QUEUE Nbls;
} GENERIC_RX;

typedef struct DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) _GENERIC_RX_NBL_CONTEXT {
    UINT32 TrailingMdlBytes;
} GENERIC_RX_NBL_CONTEXT;

CONST UINT16 GenericRxNblContextSize = sizeof(GENERIC_RX_NBL_CONTEXT);

static
GENERIC_RX_NBL_CONTEXT *
GenericRxGetNblContext(
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    return (GENERIC_RX_NBL_CONTEXT *)NET_BUFFER_LIST_CONTEXT_DATA_START(NetBufferList);
}

VOID
GenericRxCleanupNbl(
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    GENERIC_RX_NBL_CONTEXT *NblContext = GenericRxGetNblContext(Nbl);

    NET_BUFFER_DATA_LENGTH(Nb) += NblContext->TrailingMdlBytes;
    NdisRetreatNetBufferDataStart(Nb, NET_BUFFER_DATA_OFFSET(Nb), 0, NULL);
    NdisAdvanceNetBufferDataStart(Nb, NET_BUFFER_DATA_LENGTH(Nb), TRUE, NULL);
    NdisFreeNetBufferList(Nbl);
}

UINT32
GenericRxCleanupNblChain(
    _In_ NET_BUFFER_LIST *NblChain
    )
{
    UINT32 Count = 0;

    while (NblChain != NULL) {
        NET_BUFFER_LIST *Nbl = NblChain;
        NblChain = Nbl->Next;
        GenericRxCleanupNbl(Nbl);
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
    RX_ENQUEUE_IN EnqueueIn = {0};
    NET_BUFFER_LIST *Nbl = NULL;
    NET_BUFFER *Nb;
    GENERIC_RX_NBL_CONTEXT *NblContext;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status =
        IoctlBounceRxEnqueue(
            Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.DeviceIoControl.InputBufferLength,
            &EnqueueIn);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Nbl = NdisAllocateNetBufferAndNetBufferList(NblPool, sizeof(*NblContext), 0, NULL, 0, 0);
    if (Nbl == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    NblContext = GenericRxGetNblContext(Nbl);
    RtlZeroMemory(NblContext, sizeof(*NblContext));

    if (EnqueueIn.Frame.RssHashQueueId != MAXUINT32) {
        if (EnqueueIn.Frame.RssHashQueueId >= Adapter->NumRssQueues) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        NET_BUFFER_LIST_SET_HASH_FUNCTION(Nbl, NdisHashFunctionToeplitz);
        NET_BUFFER_LIST_SET_HASH_VALUE(Nbl, Adapter->RssQueues[EnqueueIn.Frame.RssHashQueueId].RssHash);
        NET_BUFFER_LIST_SET_HASH_TYPE(Nbl, NDIS_HASH_IPV4);
    }

    for (UINT32 BufferOffset = EnqueueIn.Frame.BufferCount; BufferOffset > 0; BufferOffset--) {
        CONST UINT32 BufferIndex = BufferOffset - 1;
        CONST RX_BUFFER *RxBuffer = &EnqueueIn.Buffers[BufferIndex];
        UCHAR *MdlBuffer;

        if (RxBuffer->DataOffset > 0 && BufferIndex > 0) {
            //
            // Only the first MDL can have a data offset.
            //
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (RxBuffer->DataOffset + RxBuffer->DataLength > RxBuffer->BufferLength &&
            BufferOffset < EnqueueIn.Frame.BufferCount) {
            //
            // Only the last MDL can have a data trailer.
            //
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        //
        // Allocate an MDL and data to back the XDP buffer.
        //
        Status = NdisRetreatNetBufferDataStart(Nb, RxBuffer->BufferLength, 0, NULL);
        if (Status != NDIS_STATUS_SUCCESS) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        MdlBuffer = MmGetSystemAddressForMdlSafe(NET_BUFFER_CURRENT_MDL(Nb), LowPagePriority);
        if (MdlBuffer == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        //
        // Copy the whole RX buffer, including leading and trailing bytes.
        //
        RtlCopyMemory(MdlBuffer, RxBuffer->VirtualAddress, RxBuffer->BufferLength);

        //
        // Fix up the trailing bytes of the final MDL.
        //
        if (BufferOffset == EnqueueIn.Frame.BufferCount) {
            NblContext->TrailingMdlBytes =
                RxBuffer->BufferLength - RxBuffer->DataLength - RxBuffer->DataOffset;
            NET_BUFFER_DATA_LENGTH(Nb) -= NblContext->TrailingMdlBytes;
        }

        //
        // Fix up the data offset for the leading MDL.
        //
        if (BufferIndex == 0) {
            NdisAdvanceNetBufferDataStart(Nb, RxBuffer->DataOffset, FALSE, NULL);
        }
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
            GenericRxCleanupNbl(Nbl);
        }
    }

    IoctlCleanupRxEnqueue(&EnqueueIn);

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
    CONST RX_FLUSH_IN *In = Irp->AssociatedIrp.SystemBuffer;
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
        Status = STATUS_DELETE_PENDING;
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