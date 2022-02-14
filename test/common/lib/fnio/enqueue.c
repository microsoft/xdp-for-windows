//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

static
ENQUEUE_NBL_CONTEXT *
FnIoEnqueueGetNblContext(
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    return (ENQUEUE_NBL_CONTEXT *)NET_BUFFER_LIST_CONTEXT_DATA_START(NetBufferList);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoEnqueueFrameBegin(
    _In_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength,
    _In_ NDIS_HANDLE NblPool,
    _Out_ DATA_ENQUEUE_IN *EnqueueIn,
    _Out_ NET_BUFFER_LIST **NetBufferList
    )
{
    NET_BUFFER_LIST *Nbl = NULL;
    NET_BUFFER *Nb;
    ENQUEUE_NBL_CONTEXT *NblContext;
    NTSTATUS Status;

    *NetBufferList = NULL;

    Status = FnIoIoctlBounceEnqueue(InputBuffer, InputBufferLength, EnqueueIn);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Nbl = NdisAllocateNetBufferAndNetBufferList(NblPool, sizeof(*NblContext), 0, NULL, 0, 0);
    if (Nbl == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    NblContext = FnIoEnqueueGetNblContext(Nbl);
    RtlZeroMemory(NblContext, sizeof(*NblContext));

    for (UINT32 BufferOffset = EnqueueIn->Frame.BufferCount; BufferOffset > 0; BufferOffset--) {
        CONST UINT32 BufferIndex = BufferOffset - 1;
        CONST DATA_BUFFER *RxBuffer = &EnqueueIn->Frame.Buffers[BufferIndex];
        UCHAR *MdlBuffer;

        if (RxBuffer->DataOffset > 0 && BufferIndex > 0) {
            //
            // Only the first MDL can have a data offset.
            //
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (RxBuffer->DataOffset + RxBuffer->DataLength < RxBuffer->BufferLength &&
            BufferOffset < EnqueueIn->Frame.BufferCount) {
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
        if (BufferOffset == EnqueueIn->Frame.BufferCount) {
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

    *NetBufferList = Nbl;

Exit:

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoEnqueueFrameEnd(
    _In_ DATA_ENQUEUE_IN *EnqueueIn
    )
{
    FnIoIoctlCleanupEnqueue(EnqueueIn);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoEnqueueFrameReturn(
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    ENQUEUE_NBL_CONTEXT *NblContext = FnIoEnqueueGetNblContext(Nbl);

    NET_BUFFER_DATA_LENGTH(Nb) += NblContext->TrailingMdlBytes;
    NdisRetreatNetBufferDataStart(Nb, NET_BUFFER_DATA_OFFSET(Nb), 0, NULL);
    NdisAdvanceNetBufferDataStart(Nb, NET_BUFFER_DATA_LENGTH(Nb), TRUE, NULL);
    NdisFreeNetBufferList(Nbl);
}
