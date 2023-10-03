//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _DATA_FILTER {
    DATA_FILTER_IN Params;
    UCHAR *ContiguousBuffer;
    NBL_COUNTED_QUEUE NblQueue;
    NBL_COUNTED_QUEUE NblReturn;
} DATA_FILTER;

#define NBL_PROCESSOR_NUMBER(Nbl) (*(PROCESSOR_NUMBER *)(&(Nbl)->Scratch))

_IRQL_requires_max_(DISPATCH_LEVEL)
DATA_FILTER *
FnIoCreateFilter(
    _In_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength
    )
{
    NTSTATUS Status;
    DATA_FILTER *Filter;

    Filter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Filter), POOLTAG_FILTER);
    if (Filter == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = FnIoIoctlBounceFilter(InputBuffer, InputBufferLength, &Filter->Params);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (Filter->Params.Length == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Filter->ContiguousBuffer =
        ExAllocatePoolZero(NonPagedPoolNx, Filter->Params.Length, POOLTAG_FILTER);
    if (Filter->ContiguousBuffer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    NdisInitializeNblCountedQueue(&Filter->NblQueue);
    NdisInitializeNblCountedQueue(&Filter->NblReturn);

Exit:

    if (!NT_SUCCESS(Status) && Filter != NULL) {
        FnIoDeleteFilter(Filter);
        Filter = NULL;
    }

    return Filter;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoDeleteFilter(
    _In_ DATA_FILTER *Filter
    )
{
    if (Filter->ContiguousBuffer != NULL) {
        ExFreePoolWithTag(Filter->ContiguousBuffer, POOLTAG_FILTER);
        Filter->ContiguousBuffer = NULL;
    }

    FnIoIoctlCleanupFilter(&Filter->Params);

    //
    // Clients are required to flush all frames prior to filter delete.
    //
    ASSERT(NdisIsNblCountedQueueEmpty(&Filter->NblQueue));
    ASSERT(NdisIsNblCountedQueueEmpty(&Filter->NblReturn));

    ExFreePoolWithTag(Filter, POOLTAG_FILTER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FnIoFilterNbl(
    _In_ DATA_FILTER *Filter,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    UINT32 DataLength = min(NET_BUFFER_DATA_LENGTH(NetBuffer), Filter->Params.Length);
    UCHAR *Buffer;
    PROCESSOR_NUMBER ProcessorNumber;

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

    KeGetCurrentProcessorNumberEx(&ProcessorNumber);
    NBL_PROCESSOR_NUMBER(Nbl) = ProcessorNumber;
    NdisAppendSingleNblToNblCountedQueue(&Filter->NblQueue, Nbl);

    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoGetFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    NET_BUFFER_LIST *Nbl;
    NET_BUFFER *NetBuffer;
    MDL *Mdl;
    UINT32 OutputSize;
    UINT32 DataBytes;
    UINT8 BufferCount;
    DATA_FRAME *Frame;
    DATA_BUFFER *Buffer;
    UCHAR *Data;
    UINT32 OutputBufferLength =
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    VOID *OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;

    *BytesReturned = 0;

    Nbl = Filter->NblQueue.Queue.First;
    for (UINT32 NblIndex = 0; NblIndex < Index; NblIndex++) {
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
    BufferCount = 0;
    OutputSize = sizeof(*Frame);

    for (Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer); Mdl != NULL; Mdl = Mdl->Next) {
        OutputSize += sizeof(*Buffer);
        OutputSize += Mdl->ByteCount;

        if (!NT_SUCCESS(RtlUInt8Add(BufferCount, 1, &BufferCount))) {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Exit;
        }
    }

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = OutputSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBufferLength < OutputSize) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Frame = OutputBuffer;
    Buffer = (DATA_BUFFER *)(Frame + 1);
    Data = (UCHAR *)(Buffer + BufferCount);

    Frame->Output.ProcessorNumber = NBL_PROCESSOR_NUMBER(Nbl);
    Frame->Output.RssHash = NET_BUFFER_LIST_GET_HASH_VALUE(Nbl);
    Frame->BufferCount = BufferCount;
    Frame->Buffers = RTL_PTR_SUBTRACT(Buffer, Frame);

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    DataBytes = NET_BUFFER_DATA_LENGTH(NetBuffer);

    for (UINT32 BufferIndex = 0; BufferIndex < BufferCount; BufferIndex++) {
        UCHAR *MdlBuffer;

        if (Mdl == NULL) {
            Status = STATUS_UNSUCCESSFUL;
            goto Exit;
        }

        MdlBuffer = MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority);
        if (MdlBuffer == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        if (BufferIndex == 0) {
            Buffer[BufferIndex].DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
        } else {
            Buffer[BufferIndex].DataOffset = 0;
        }

        Buffer[BufferIndex].DataLength =
            min(Mdl->ByteCount - Buffer[BufferIndex].DataOffset, DataBytes);
        Buffer[BufferIndex].BufferLength =
            Buffer[BufferIndex].DataOffset + Buffer[BufferIndex].DataLength;

        RtlCopyMemory(
            Data + Buffer[BufferIndex].DataOffset,
            MdlBuffer + Buffer[BufferIndex].DataOffset,
            Buffer[BufferIndex].DataLength);
        Buffer[BufferIndex].VirtualAddress = RTL_PTR_SUBTRACT(Data, Frame);

        Data += Buffer[BufferIndex].DataOffset;
        Data += Buffer[BufferIndex].DataLength;
        DataBytes -= Buffer[BufferIndex].DataLength;
        Mdl = Mdl->Next;
    }

    *BytesReturned = OutputSize;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoDequeueFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index
    )
{
    NTSTATUS Status;
    NBL_COUNTED_QUEUE NblChain;
    NET_BUFFER_LIST *Nbl;

    NdisInitializeNblCountedQueue(&NblChain);

    Status = STATUS_NOT_FOUND;

    for (UINT32 NblIndex = 0; !NdisIsNblCountedQueueEmpty(&Filter->NblQueue); NblIndex++) {
        Nbl = NdisPopFirstNblFromNblCountedQueue(&Filter->NblQueue);

        if (NblIndex == Index) {
            NdisAppendSingleNblToNblCountedQueue(&Filter->NblReturn, Nbl);
            Status = STATUS_SUCCESS;
        } else {
            NdisAppendSingleNblToNblCountedQueue(&NblChain, Nbl);
        }
    }

    NdisAppendNblCountedQueueToNblCountedQueueFast(&Filter->NblQueue, &NblChain);

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
SIZE_T
FnIoFlushDequeuedFrames(
    _In_ DATA_FILTER *Filter,
    _Out_ NET_BUFFER_LIST **NblChain
    )
{
    NBL_COUNTED_QUEUE FlushChain;

    NdisInitializeNblCountedQueue(&FlushChain);

    NdisAppendNblCountedQueueToNblCountedQueueFast(&FlushChain, &Filter->NblReturn);
    *NblChain = NdisGetNblChainFromNblCountedQueue(&FlushChain);

    return FlushChain.NblCount;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
SIZE_T
FnIoFlushAllFrames(
    _In_ DATA_FILTER *Filter,
    _Out_ NET_BUFFER_LIST **NblChain
    )
{
    NBL_COUNTED_QUEUE FlushChain;

    NdisInitializeNblCountedQueue(&FlushChain);

    NdisAppendNblCountedQueueToNblCountedQueueFast(&FlushChain, &Filter->NblQueue);
    NdisAppendNblCountedQueueToNblCountedQueueFast(&FlushChain, &Filter->NblReturn);
    *NblChain = NdisGetNblChainFromNblCountedQueue(&FlushChain);

    return FlushChain.NblCount;
}
