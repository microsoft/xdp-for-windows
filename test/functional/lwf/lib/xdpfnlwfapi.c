//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    XDPFNLWF_OPEN_DEFAULT *OpenDefault;
    CHAR EaBuffer[XDPFNLWF_OPEN_EA_LENGTH + sizeof(*OpenDefault)];

    OpenDefault = FnLwfInitializeEa(XDPFNLWF_FILE_TYPE_DEFAULT, EaBuffer, sizeof(EaBuffer));
    OpenDefault->IfIndex = IfIndex;

    return FnLwfOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer), Handle);
}

HRESULT
FnLwfTxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    DATA_ENQUEUE_IN In = {0};

    //
    // Enqueues one frame to the TX backlog.
    //

    In.Frame = *Frame;

    return FnLwfIoctl(Handle, IOCTL_TX_ENQUEUE, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnLwfTxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    )
{
    DATA_FLUSH_IN In = {0};

    //
    // Indicates all frames from the TX backlog.
    //
    if (Options != NULL) {
        In.Options = *Options;
    }

    return FnLwfIoctl(Handle, IOCTL_TX_FLUSH, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnLwfRxFilter(
    _In_ HANDLE Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
    _In_ UINT32 Length
    )
{
    DATA_FILTER_IN In = {0};

    //
    // Sets a packet filter on the RX handle. If an NBL matches
    // the packet filter, it is captured by the RX context and added to the tail
    // of the packet queue.
    //
    // Captured NBLs are returned to NDIS either by closing the RX handle or by
    // dequeuing the packet and flushing the RX context.
    //
    // Zero-length filters disable packet captures.
    //

    In.Pattern = Pattern;
    In.Mask = Mask;
    In.Length = Length;

    return FnLwfIoctl(Handle, IOCTL_RX_FILTER, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnLwfRxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    DATA_GET_FRAME_IN In = {0};
    HRESULT Result;

    //
    // Returns the contents of a captured NBL.
    //

    In.Index = FrameIndex;

    Result =
        FnLwfIoctl(
            Handle, IOCTL_RX_GET_FRAME, &In, sizeof(In), Frame, *FrameBufferLength,
            FrameBufferLength, NULL);

    if (SUCCEEDED(Result) && Frame != NULL) {
        //
        // The IOCTL returns pointers relative to the output address; adjust
        // each pointer into this address space.
        //

        Frame->Buffers = RTL_PTR_ADD(Frame, Frame->Buffers);

        for (UINT32 BufferIndex = 0; BufferIndex < Frame->BufferCount; BufferIndex++) {
            Frame->Buffers[BufferIndex].VirtualAddress =
                RTL_PTR_ADD(Frame, Frame->Buffers[BufferIndex].VirtualAddress);
        }
    }

    return Result;
}

HRESULT
FnLwfRxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    DATA_DEQUEUE_FRAME_IN In = {0};

    //
    // Dequeues a packet from the RX queue and appends it to the completion
    // queue.
    //

    In.Index = FrameIndex;

    return FnLwfIoctl(Handle, IOCTL_RX_DEQUEUE_FRAME, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnLwfRxFlush(
    _In_ HANDLE Handle
    )
{
    //
    // Completes all packets on the completion queue.
    //

    return FnLwfIoctl(Handle, IOCTL_RX_FLUSH, NULL, 0, NULL, 0, NULL, NULL);
}

HRESULT
FnLwfOidSubmitRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    )
{
    OID_SUBMIT_REQUEST_IN In = {0};

    //
    // Issues an OID and waits for completion.
    //

    In.Key = Key;
    In.InformationBuffer = InformationBuffer;
    In.InformationBufferLength = *InformationBufferLength;

    return
        FnLwfIoctl(
            Handle, IOCTL_OID_SUBMIT_REQUEST, &In, sizeof(In), InformationBuffer,
            *InformationBufferLength, InformationBufferLength, NULL);
}

HRESULT
FnLwfStatusSetFilter(
    _In_ HANDLE Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    )
{
    STATUS_FILTER_IN In = {0};

    In.StatusCode = StatusCode;
    In.BlockIndications = BlockIndications;
    In.QueueIndications = QueueIndications;

    return FnLwfIoctl(Handle, IOCTL_STATUS_SET_FILTER, &In, sizeof(In), NULL, 0, 0, NULL);
}

HRESULT
FnLwfStatusGetIndication(
    _In_ HANDLE Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    )
{
    return
        FnLwfIoctl(
            Handle, IOCTL_STATUS_GET_INDICATION, NULL, 0, StatusBuffer, *StatusBufferLength,
            StatusBufferLength, NULL);
}

HRESULT
FnLwfDatapathGetState(
    _In_ HANDLE Handle,
    BOOLEAN *IsDatapathActive
    )
{
    return
        FnLwfIoctl(
            Handle, IOCTL_DATAPATH_GET_STATE, NULL, 0, IsDatapathActive, sizeof(*IsDatapathActive),
            NULL, NULL);
}
