//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "xsk.tmh"
#include <afxdp_helper.h>
#include <afxdp_experimental.h>

typedef enum _XSK_STATE {
    XskUnbound,
    XskBinding,
    XskBound,
    XskActivating,
    XskActive,
    XskDetached,
    XskClosing,
} XSK_STATE;

typedef struct _XSK_SHARED_RING {
    UINT32 ProducerIndex;
    UINT32 ConsumerIndex;
    UINT32 Flags;
    UINT32 Reserved;
    //
    // Followed by power-of-two array of ring elements, starting on 8-byte alignment.
    // XSK_FRAME_DESCRIPTOR[] for rx/tx, UINT64[] for fill/completion
    //
} XSK_SHARED_RING;

typedef struct _XSK_KERNEL_RING {
    XSK_SHARED_RING *Shared;
    MDL *Mdl;
    UINT32 Size;
    UINT32 Mask;
    UINT32 ElementStride;
    VOID *UserVa;
    VOID *OwningProcess;
    UINT32 IdealProcessor;
    XSK_ERROR Error;
} XSK_KERNEL_RING;

typedef struct _UMEM_MAPPING {
    MDL *Mdl;
    UCHAR *SystemAddress;
    DMA_LOGICAL_ADDRESS DmaAddress;
} UMEM_MAPPING;

typedef struct _UMEM {
    XSK_UMEM_REG Reg;
    UMEM_MAPPING Mapping;
    VOID *ReservedMapping;
    XDP_REFERENCE_COUNT ReferenceCount;
} UMEM;

typedef enum _ALLOCATION_SOURCE {
    NotAllocated,
    AllocatedByMm,
    AllocatedByDma,
} ALLOCATION_SOURCE;

typedef struct _UMEM_BOUNCE {
    UMEM_MAPPING Mapping;
    SIZE_T *Tracker;
    ALLOCATION_SOURCE AllocationSource;
} UMEM_BOUNCE;

typedef enum _XSK_IO_WAIT_FLAGS {
    XSK_IO_WAIT_FLAG_POLL_MODE_SOCKET = 0x1,
} XSK_IO_WAIT_FLAGS;

typedef struct _XSK_RX_XDP {
    //
    // XDP data path fields.
    //
    XDP_RING *FrameRing;
    XDP_RING *FragmentRing;
    XDP_EXTENSION VaExtension;
    XDP_EXTENSION FragmentExtension;
    XDP_EXTENSION RxActionExtension;
    NDIS_POLL_BACKCHANNEL *PollHandle;
    struct {
        UINT8 NotificationsRegistered : 1;
        UINT8 DatapathAttached : 1;
    } Flags;

    //
    // XDP control path fields.
    //
    XDP_BINDING_HANDLE IfHandle;
    XDP_HOOK_ID HookId;
    XDP_RX_QUEUE *Queue;
    XDP_RX_QUEUE_NOTIFICATION_ENTRY QueueNotificationEntry;
} XSK_RX_XDP;

typedef struct _XSK_RX {
    XSK_KERNEL_RING Ring;
    XSK_KERNEL_RING FillRing;
    XSK_RX_XDP Xdp;
} XSK_RX;

typedef struct _XSK_TX_XDP {
    //
    // XDP data path fields.
    //
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    XDP_EXTENSION VaExtension;
    XDP_EXTENSION LaExtension;
    XDP_EXTENSION MdlExtension;
    XDP_EXTENSION FrameTxCompletionExtension;
    XDP_EXTENSION TxCompletionExtension;
    UINT32 OutstandingFrames;
    UINT32 MaxBufferLength;
    UINT32 MaxFrameLength;
    struct {
        BOOLEAN VirtualAddressExt : 1;
        BOOLEAN LogicalAddressExt : 1;
        BOOLEAN MdlExt : 1;
        BOOLEAN CompletionContext : 1;
        BOOLEAN OutOfOrderCompletion : 1;
        BOOLEAN QueueInserted : 1;
        BOOLEAN QueueActive : 1;
    } Flags;
    NDIS_POLL_BACKCHANNEL *PollHandle;
    XDP_TX_QUEUE *Queue;

    //
    // Control path fields.
    //
    XDP_BINDING_HANDLE IfHandle;
    XDP_HOOK_ID HookId;
    XDP_TX_QUEUE_NOTIFICATION_ENTRY QueueNotificationEntry;
    XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY DatapathClientEntry;
    KEVENT OutstandingFlushComplete;
} XSK_TX_XDP;

typedef struct _XSK_TX {
    XSK_KERNEL_RING Ring;
    XSK_KERNEL_RING CompletionRing;
    UMEM_BOUNCE Bounce;
    XSK_TX_XDP Xdp;
    DMA_ADAPTER *DmaAdapter;
} XSK_TX;

typedef struct _XSK {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_REFERENCE_COUNT ReferenceCount;
    XSK_STATE State;
    UMEM *Umem;
    XSK_RX Rx;
    XSK_TX Tx;
    KSPIN_LOCK Lock;
    UINT32 IoWaitFlags;
    XSK_IO_WAIT_FLAGS IoWaitInternalFlags;
    KEVENT IoWaitEvent;
    IRP *IoWaitIrp;
    XSK_STATISTICS Statistics;
    EX_PUSH_LOCK PollLock;
    XSK_POLL_MODE PollMode;
    BOOLEAN PollBusy;
    ULONG PollWaiters;
    KEVENT PollRequested;
} XSK;

typedef struct _XSK_BINDING_WORKITEM {
    XDP_BINDING_WORKITEM IfWorkItem;
    XSK *Xsk;
    UINT32 QueueId;
    KEVENT CompletionEvent;
    NTSTATUS CompletionStatus;
} XSK_BINDING_WORKITEM;

typedef struct _XSK_GLOBALS {
    BOOLEAN DisableTxBounce;
    BOOLEAN RxZeroCopy;
} XSK_GLOBALS;

static
NTSTATUS
XskPoke(
    _In_ XSK *Xsk,
    _In_ UINT32 Flags,
    _In_ UINT32 TimeoutMs
    );

#define POOLTAG_BOUNCE 'BksX' // XskB
#define POOLTAG_RING   'RksX' // XskR
#define POOLTAG_UMEM   'UksX' // XskU
#define POOLTAG_XSK    'kksX' // Xskk
#define INFINITE 0xFFFFFFFF

static XSK_GLOBALS XskGlobals;
static XDP_REG_WATCHER_CLIENT_ENTRY XskRegWatcherEntry;
static XDP_FILE_IRP_ROUTINE XskIrpDeviceIoControl;
static XDP_FILE_IRP_ROUTINE XskIrpCleanup;
static XDP_FILE_IRP_ROUTINE XskIrpClose;
static XDP_FILE_DISPATCH XskFileDispatch = {
    .IoControl  = XskIrpDeviceIoControl,
    .Cleanup    = XskIrpCleanup,
    .Close      = XskIrpClose,
};

static
VOID
XskReference(
    _In_ XSK* Xsk
    )
{
    XdpIncrementReferenceCount(&Xsk->ReferenceCount);
}

static
VOID
XskDereference(
    _In_ XSK* Xsk
    )
{
    if (XdpDecrementReferenceCount(&Xsk->ReferenceCount)) {
        ExFreePoolWithTag(Xsk, POOLTAG_XSK);
    }
}

static
UINT32
XskWaitInFlagsToOutFlags(
    _In_ UINT32 NotifyFlags
    )
{
    UINT32 NotifyResult = 0;

    ASSERT((NotifyFlags & (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX)) == NotifyFlags);

    //
    // Sets all wait output flags given the input wait flags.
    //

    if (NotifyFlags & XSK_NOTIFY_FLAG_WAIT_RX) {
        NotifyResult |= XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE;
    }

    if (NotifyFlags & XSK_NOTIFY_FLAG_WAIT_TX) {
        NotifyResult |= XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE;
    }

    return NotifyResult;
}

static
VOID
XskSignalReadyIo(
    _In_ XSK *Xsk,
    _In_ UINT32 ReadyFlags
    )
{
    KIRQL OldIrql;
    IRP *Irp = NULL;

    ASSERT((ReadyFlags & (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX)) == ReadyFlags);

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if ((Xsk->IoWaitFlags & ReadyFlags) != 0) {
        if (Xsk->IoWaitIrp != NULL) {
            Irp = Xsk->IoWaitIrp;
            Irp->IoStatus.Information = XskWaitInFlagsToOutFlags(Xsk->IoWaitFlags & ReadyFlags);
            Xsk->IoWaitIrp = NULL;
            Xsk->IoWaitFlags = 0;

            //
            // Synchronize with IO cancellation. If the cancellation routine
            // is in flight, drop the IRP here and let the cancellation routine
            // complete it.
            //
            if (IoSetCancelRoutine(Irp, NULL) == NULL) {
                Irp = NULL;
            }
        } else {
            (VOID)KeSetEvent(&Xsk->IoWaitEvent, IO_NETWORK_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    if (Irp != NULL) {
        EventWriteXskNotifyAsyncComplete(&MICROSOFT_XDP_PROVIDER, Xsk, Irp, Irp->IoStatus.Status);
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }
}

static
UINT32
XskRingProdReserve(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Count
    )
{
    UINT32 Available =
        Ring->Size - (ReadUInt32NoFence(&Ring->Shared->ProducerIndex) -
            ReadUInt32NoFence(&Ring->Shared->ConsumerIndex));
    return min(Available, Count);
}

static
UINT32
XskRingConsPeek(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Count
    )
{
    UINT32 Available =
        ReadUInt32Acquire(&Ring->Shared->ProducerIndex) -
            ReadUInt32NoFence(&Ring->Shared->ConsumerIndex);
    return min(Available, Count);
}

static
VOID
XskRingProdSubmit(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Count
    )
{
    WriteUInt32Release(&Ring->Shared->ProducerIndex, Ring->Shared->ProducerIndex + Count);
}

static
VOID
XskRingConsRelease(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Count
    )
{
    Ring->Shared->ConsumerIndex += Count;
}

static
VOID *
XskKernelRingGetElement(
    _In_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Index
    )
{
    ASSERT(Index <= Ring->Mask);
    return (UCHAR *)&Ring->Shared[1] + (SIZE_T)Index * Ring->ElementStride;
}

static
VOID
XskKernelRingSetError(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ XSK_ERROR Error
    )
{
    if (InterlockedCompareExchange((LONG *)&Ring->Error, Error, XSK_NO_ERROR) == XSK_NO_ERROR) {
        if (Ring->Shared != NULL) {
            InterlockedOr((LONG *)&Ring->Shared->Flags, XSK_RING_FLAG_ERROR);
        }
    }
}

static
VOID
XskKernelRingUpdateIdealProcessor(
    _Inout_ XSK_KERNEL_RING *Ring
    )
{
    UINT32 CurrentProcessor = KeGetCurrentProcessorIndex();

    ASSERT(Ring->Size > 0);

    //
    // Set the ideal processor to the current processor. For queues that are not
    // strongly affinitized (e.g. RSS disabled, not supported, plain buggy) we
    // might need to add a heuristic to avoid indicating flapping CPUs up to
    // applications.
    //

    if (Ring->IdealProcessor != CurrentProcessor) {
        Ring->IdealProcessor = CurrentProcessor;
        InterlockedOr((LONG *)&Ring->Shared->Flags, XSK_RING_FLAG_AFFINITY_CHANGED);
    }
}

static
UMEM_MAPPING *
XskGetTxMapping(
    _In_ XSK *Xsk
    )
{
    return
        (Xsk->Tx.Bounce.Tracker != NULL)
            ? &Xsk->Tx.Bounce.Mapping : &Xsk->Umem->Mapping;
}

static
BOOLEAN
XskRequiresTxBounceBuffer(
    XSK *Xsk
    )
{
    //
    // Only the NDIS6 data path requires immutable buffers.
    // In the future, TX inspection programs may have similar requirements.
    //
    return
        !XskGlobals.DisableTxBounce &&
        (XdpIfGetCapabilities(Xsk->Tx.Xdp.IfHandle)->Mode == XDP_INTERFACE_MODE_GENERIC);
}

static
VOID
XskReleaseBounceBuffer(
    _In_ UMEM *Umem,
    _In_ UMEM_BOUNCE *Bounce,
    _In_ UINT64 RelativeAddress
    )
{
    SIZE_T ChunkIndex;

    if (Bounce->Tracker == NULL) {
        //
        // No debounce is required.
        //
        return;
    }

    ChunkIndex = RelativeAddress / Umem->Reg.ChunkSize;
    Bounce->Tracker[ChunkIndex]--;
}

static
_Success_(return != FALSE)
BOOLEAN
XskBounceBuffer(
    _In_ UMEM *Umem,
    _In_ UMEM_BOUNCE *Bounce,
    _In_ XDP_BUFFER *Buffer,
    _In_ UINT64 RelativeAddress,
    _Out_ UMEM_MAPPING **Mapping
    )
{
    SIZE_T ChunkIndex;

    if (Bounce->Tracker == NULL) {
        //
        // No bounce is required.
        //
        *Mapping = &Umem->Mapping;
        return TRUE;
    }

    ChunkIndex = RelativeAddress / Umem->Reg.ChunkSize;
    if (ChunkIndex != (RelativeAddress + Buffer->BufferLength - 1) / Umem->Reg.ChunkSize) {
        //
        // The entire buffer must fit within a chunk.
        //
        return FALSE;
    }

    if (Bounce->Tracker[ChunkIndex]++ == 0) {
        //
        // It is legal for an app to post the same buffer for multiple IOs, but
        // behavior is undefined if the buffer is modified while IO is
        // outstanding. Ignore any writes to the buffer once an IO is in flight.
        //
        RtlCopyMemory(
            Bounce->Mapping.SystemAddress + RelativeAddress + Buffer->DataOffset,
            Umem->Mapping.SystemAddress + RelativeAddress + Buffer->DataOffset,
            Buffer->DataLength);
    }

    *Mapping = &Bounce->Mapping;
    return TRUE;
}

static
NTSTATUS
XskAllocateTxBounceBuffer(
    _Inout_ XSK *Xsk
    )
{
    NTSTATUS Status;
    SIZE_T BounceTrackerSize;
    UMEM_BOUNCE *Bounce = &Xsk->Tx.Bounce;

    if (Bounce->AllocationSource == AllocatedByDma) {
        //
        // DMA setup resulted in a bounce buffer being allocated.
        //
        ASSERT(Bounce->Mapping.DmaAddress.QuadPart != 0);
        ASSERT(Bounce->Mapping.SystemAddress != 0);
        ASSERT(Bounce->Mapping.Mdl == 0);
    } else if (XskRequiresTxBounceBuffer(Xsk)) {
        //
        // Policy still requires we have a bounce buffer, so create one now.
        //
        ASSERT(Bounce->AllocationSource == NotAllocated);
        Bounce->Mapping.SystemAddress =
            ExAllocatePoolUninitialized(NonPagedPoolNx, Xsk->Umem->Reg.TotalSize, POOLTAG_BOUNCE);
        if (Bounce->Mapping.SystemAddress == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
        Bounce->AllocationSource = AllocatedByMm;
    } else {
        //
        // We don't have or need a bounce buffer.
        //
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    ASSERT(Xsk->Umem->Reg.TotalSize <= ULONG_MAX);
    Bounce->Mapping.Mdl =
        IoAllocateMdl(
            Bounce->Mapping.SystemAddress, (ULONG)Xsk->Umem->Reg.TotalSize, FALSE, FALSE, NULL);
    if (Bounce->Mapping.Mdl == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    MmBuildMdlForNonPagedPool(Bounce->Mapping.Mdl);

    Status =
        RtlSizeTMult(
            Xsk->Umem->Reg.TotalSize / Xsk->Umem->Reg.ChunkSize, sizeof(SIZE_T),
            &BounceTrackerSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Bounce->Tracker = ExAllocatePoolZero(NonPagedPoolNx, BounceTrackerSize, POOLTAG_BOUNCE);
    if (Bounce->Tracker == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
VOID
XskFreeBounceBuffer(
    _Inout_ UMEM_BOUNCE *Bounce
    )
{
    if (Bounce->Tracker != NULL) {
        ExFreePoolWithTag(Bounce->Tracker, POOLTAG_BOUNCE);
        Bounce->Tracker = NULL;
    }

    if (Bounce->Mapping.Mdl != NULL) {
        IoFreeMdl(Bounce->Mapping.Mdl);
        Bounce->Mapping.Mdl = NULL;
    }

    if (Bounce->Mapping.SystemAddress != NULL && Bounce->AllocationSource == AllocatedByMm) {
        ExFreePoolWithTag(Bounce->Mapping.SystemAddress, POOLTAG_BOUNCE);
        Bounce->Mapping.SystemAddress = NULL;
        Bounce->AllocationSource = NotAllocated;
    }
}

static
FORCEINLINE
UINT32
XskGetAvailableTxCompletion(
    _In_ XSK *Xsk
    )
{
    UINT32 XskCompletionAvailable = XskRingProdReserve(&Xsk->Tx.CompletionRing, MAXUINT32);

    if (!NT_VERIFY(XskCompletionAvailable >= Xsk->Tx.Xdp.OutstandingFrames)) {
        //
        // If the above condition does not hold, the XSK TX completion ring is
        // no longer valid. This implies an application programming error.
        //
        XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INVALID_RING);
        return 0;
    }

    return XskCompletionAvailable - Xsk->Tx.Xdp.OutstandingFrames;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32
XskFillTx(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry,
    _In_ UINT32 XdpTxAvailable
    )
{
    XSK *Xsk = CONTAINING_RECORD(DatapathClientEntry, XSK, Tx.Xdp.DatapathClientEntry);
    NTSTATUS Status;
    ULONGLONG Result;
    XSK_FRAME_DESCRIPTOR *XskFrame;
    XSK_BUFFER_DESCRIPTOR *XskBuffer;
    UINT32 Count;
    UINT32 FrameCount = 0;
    UINT32 TxIndex;
    UINT32 XskCompletionAvailable;
    UINT32 XskTxAvailable;
    XDP_RING *FrameRing = Xsk->Tx.Xdp.FrameRing;

    if (Xsk->State != XskActive) {
        return 0;
    }

    //
    // The need poke flag is cleared when a poke request is submitted. If no
    // input is available and no packets are outstanding, or if the TX queue is
    // blocked by a full TX completion queue, set the need poke flag and then
    // re-check for available TX/completion.
    //
    if (Xsk->Tx.Xdp.PollHandle == NULL &&
        ((XskRingConsPeek(&Xsk->Tx.Ring, 1) == 0 && Xsk->Tx.Xdp.OutstandingFrames == 0) ||
         (XskGetAvailableTxCompletion(Xsk) == 0))) {
        InterlockedOr((LONG *)&Xsk->Tx.Ring.Shared->Flags, XSK_RING_FLAG_NEED_POKE);
    }

    //
    // Number of buffers we can move from the XSK TX ring to the XDP TX ring is
    // the minimum of these values:
    //
    // 1) XDP TX frames available for production
    // 2) XSK TX descriptors available for consumption
    // 3) XSK TX completion descriptors available for production
    //    - XSK TX operations outstanding
    //

    XskTxAvailable = XskRingConsPeek(&Xsk->Tx.Ring, MAXUINT32);

    XskCompletionAvailable = XskGetAvailableTxCompletion(Xsk);

    Count = min(min(XdpTxAvailable, XskTxAvailable), XskCompletionAvailable);

    for (ULONG i = 0; i < Count; i++) {
        XDP_FRAME *Frame;
        XDP_BUFFER *Buffer;
        XSK_BUFFER_ADDRESS AddressDescriptor;
        UMEM_MAPPING *Mapping;
        XDP_TX_FRAME_COMPLETION_CONTEXT *CompletionContext;

        TxIndex =
            (ReadUInt32NoFence(&Xsk->Tx.Ring.Shared->ConsumerIndex) + i) & (Xsk->Tx.Ring.Mask);
        XskFrame = XskKernelRingGetElement(&Xsk->Tx.Ring, TxIndex);
        XskBuffer = &XskFrame->Buffer;

        Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex & FrameRing->Mask);
        Buffer = &Frame->Buffer;

        AddressDescriptor.AddressAndOffset =
            ReadUInt64NoFence(&XskBuffer->Address.AddressAndOffset);
        Buffer->DataOffset = (UINT32)AddressDescriptor.Offset;
        Buffer->DataLength = ReadUInt32NoFence(&XskBuffer->Length);
        Buffer->BufferLength = Buffer->DataLength + Buffer->DataOffset;

        Status = RtlUInt64Add(AddressDescriptor.BaseAddress, Buffer->DataLength, &Result);
        Status |= RtlUInt64Add(Buffer->DataOffset, Result, &Result);
        if (Result > Xsk->Umem->Reg.TotalSize ||
            Buffer->DataLength == 0 ||
            Status != STATUS_SUCCESS) {
            Xsk->Statistics.TxInvalidDescriptors++;
            STAT_INC(XdpTxQueueGetStats(Xsk->Tx.Xdp.Queue), XskInvalidDescriptors);
            continue;
        }

        if (Buffer->DataLength > min(Xsk->Tx.Xdp.MaxBufferLength, Xsk->Tx.Xdp.MaxFrameLength)) {
            Xsk->Statistics.TxInvalidDescriptors++;
            STAT_INC(XdpTxQueueGetStats(Xsk->Tx.Xdp.Queue), XskInvalidDescriptors);
            continue;
        }

        if (!XskBounceBuffer(
                Xsk->Umem, &Xsk->Tx.Bounce, Buffer, AddressDescriptor.BaseAddress, &Mapping)) {
            Xsk->Statistics.TxInvalidDescriptors++;
            STAT_INC(XdpTxQueueGetStats(Xsk->Tx.Xdp.Queue), XskInvalidDescriptors);
            continue;
        }

        if (Xsk->Tx.Xdp.Flags.VirtualAddressExt) {
            XDP_BUFFER_VIRTUAL_ADDRESS *Va;
            Va = XdpGetVirtualAddressExtension(Buffer, &Xsk->Tx.Xdp.VaExtension);
            Va->VirtualAddress = &Mapping->SystemAddress[AddressDescriptor.BaseAddress];
        }
        if (Xsk->Tx.Xdp.Flags.LogicalAddressExt) {
            XDP_BUFFER_LOGICAL_ADDRESS *La;
            La = XdpGetLogicalAddressExtension(Buffer, &Xsk->Tx.Xdp.LaExtension);
            La->LogicalAddress = Mapping->DmaAddress.QuadPart + AddressDescriptor.BaseAddress;
        }
        if (Xsk->Tx.Xdp.Flags.MdlExt) {
            XDP_BUFFER_MDL *Mdl;
            Mdl = XdpGetMdlExtension(Buffer, &Xsk->Tx.Xdp.MdlExtension);
            Mdl->Mdl = Mapping->Mdl;
            Mdl->MdlOffset = AddressDescriptor.BaseAddress;
        }
        if (Xsk->Tx.Xdp.Flags.CompletionContext) {
            CompletionContext =
                XdpGetFrameTxCompletionContextExtension(
                    Frame, &Xsk->Tx.Xdp.FrameTxCompletionExtension);
            CompletionContext->Context = &Xsk->Tx.Xdp.DatapathClientEntry;
        }

        EventWriteXskTxEnqueue(
            &MICROSOFT_XDP_PROVIDER, Xsk, Xsk->Tx.Ring.Shared->ConsumerIndex + i,
            FrameRing->ProducerIndex);

        FrameRing->ProducerIndex++;
        FrameCount++;
    }

    if (Count > 0) {
        XskRingConsRelease(&Xsk->Tx.Ring, Count);
        XskKernelRingUpdateIdealProcessor(&Xsk->Tx.Ring);
    }

    Xsk->Tx.Xdp.OutstandingFrames += FrameCount;

    //
    // If input was processed, clear the need poke flag.
    //
    if (Xsk->Tx.Xdp.PollHandle == NULL && Xsk->Tx.Xdp.OutstandingFrames > 0 &&
        (Xsk->Tx.Ring.Shared->Flags & XSK_RING_FLAG_NEED_POKE)) {
        InterlockedAnd((LONG *)&Xsk->Tx.Ring.Shared->Flags, ~XSK_RING_FLAG_NEED_POKE);
    }

    return FrameCount;
}

static
VOID
XskWriteUmemTxCompletion(
    _In_ XSK *Xsk,
    _In_ UINT32 Index,
    _In_ UINT64 RelativeAddress
    )
{
    UINT64 *XskCompletion;

    XskReleaseBounceBuffer(Xsk->Umem, &Xsk->Tx.Bounce, RelativeAddress);
    XskCompletion =
        XskKernelRingGetElement(
            &Xsk->Tx.CompletionRing, Index & (Xsk->Tx.CompletionRing.Mask));
    *XskCompletion = RelativeAddress;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskTxCompleteRundown(
    _In_ XSK *Xsk
    )
{
    if (Xsk->State > XskActive) {
        if (Xsk->Tx.Xdp.OutstandingFrames == 0) {
            KeSetEvent(&Xsk->Tx.Xdp.OutstandingFlushComplete, 0, FALSE);
        }
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskFillTxCompletion(
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *DatapathClientEntry
    )
{
    XSK *Xsk = CONTAINING_RECORD(DatapathClientEntry, XSK, Tx.Xdp.DatapathClientEntry);
    XSK_SHARED_RING *Ring = Xsk->Tx.CompletionRing.Shared;
    UINT32 ProducerIndex = ReadUInt32NoFence(&Ring->ProducerIndex);
    UINT32 OriginalProducerIndex = ProducerIndex;
    UINT32 Count;
    UINT64 RelativeAddress;
    UMEM_MAPPING *Mapping = XskGetTxMapping(Xsk);
    XDP_TX_FRAME_COMPLETION_CONTEXT *CompletionContext;

    if (Xsk->Tx.Xdp.Flags.OutOfOrderCompletion) {
        XDP_RING *XdpRing = Xsk->Tx.Xdp.CompletionRing;
        XDP_TX_FRAME_COMPLETION *Completion;

        //
        // Move all entries from the XDP TX completion ring to the XSK TX completion
        // ring. We have ensured sufficient XSK TX completion ring space when we
        // consumed from the XSK TX ring.
        //
        ASSERT(XdpRingCount(XdpRing) > 0);
        do {
            Completion = XdpRingGetElement(XdpRing, XdpRing->ConsumerIndex & XdpRing->Mask);

            if (Xsk->Tx.Xdp.Flags.CompletionContext) {
                CompletionContext =
                    XdpGetTxCompletionContextExtension(
                        Completion, &Xsk->Tx.Xdp.TxCompletionExtension);
                if (CompletionContext->Context != &Xsk->Tx.Xdp.DatapathClientEntry) {
                    //
                    // We must have completed at least the first frame.
                    //
                    ASSERT((ProducerIndex - OriginalProducerIndex) > 0);
                    break;
                }
            }

            if (Xsk->Tx.Xdp.Flags.VirtualAddressExt) {
                RelativeAddress = Completion->BufferAddress - (UINT64)Mapping->SystemAddress;
            } else if (Xsk->Tx.Xdp.Flags.LogicalAddressExt) {
                RelativeAddress = Completion->BufferAddress - Mapping->DmaAddress.QuadPart;
            } else if (Xsk->Tx.Xdp.Flags.MdlExt) {
                RelativeAddress = Completion->BufferAddress;
            } else {
                //
                // One of the above extensions must have be enabled.
                //
                ASSERT(FALSE);
                RelativeAddress = 0;
            }

            XskWriteUmemTxCompletion(Xsk, ProducerIndex++, RelativeAddress);
            XdpRing->ConsumerIndex++;
        } while (XdpRingCount(XdpRing) > 0);
    } else {
        XDP_RING *XdpRing = Xsk->Tx.Xdp.FrameRing;
        XDP_FRAME *Frame;

        //
        // Move all completed entries from the XDP TX ring to the XSK TX completion
        // ring. We have ensured sufficient XSK TX completion ring space when we
        // consumed from the XSK TX ring.
        //

        ASSERT((XdpRing->ConsumerIndex - XdpRing->Reserved) > 0);
        do {
            Frame = XdpRingGetElement(XdpRing, XdpRing->Reserved & XdpRing->Mask);

            if (Xsk->Tx.Xdp.Flags.CompletionContext) {
                CompletionContext =
                    XdpGetFrameTxCompletionContextExtension(
                        Frame, &Xsk->Tx.Xdp.FrameTxCompletionExtension);
                if (CompletionContext->Context != &Xsk->Tx.Xdp.DatapathClientEntry) {
                    //
                    // We must have completed at least the first frame.
                    //
                    ASSERT((ProducerIndex - OriginalProducerIndex) > 0);
                    break;
                }
            }

            if (Xsk->Tx.Xdp.Flags.VirtualAddressExt) {
                XDP_BUFFER_VIRTUAL_ADDRESS *Va;
                Va = XdpGetVirtualAddressExtension(&Frame->Buffer, &Xsk->Tx.Xdp.VaExtension);
                RelativeAddress = Va->VirtualAddress - Mapping->SystemAddress;
            } else if (Xsk->Tx.Xdp.Flags.LogicalAddressExt) {
                XDP_BUFFER_LOGICAL_ADDRESS *La;
                La = XdpGetLogicalAddressExtension(&Frame->Buffer, &Xsk->Tx.Xdp.LaExtension);
                RelativeAddress = La->LogicalAddress - Mapping->DmaAddress.QuadPart;
            } else if (Xsk->Tx.Xdp.Flags.MdlExt) {
                XDP_BUFFER_MDL *Mdl;
                Mdl = XdpGetMdlExtension(&Frame->Buffer, &Xsk->Tx.Xdp.MdlExtension);
                RelativeAddress = Mdl->MdlOffset;
            } else {
                //
                // One of the above extensions must have be enabled.
                //
                ASSERT(FALSE);
                RelativeAddress = 0;
            }

            XskWriteUmemTxCompletion(Xsk, ProducerIndex++, RelativeAddress);
        } while ((XdpRing->ConsumerIndex - ++XdpRing->Reserved) > 0);
    }

    Count = ProducerIndex - OriginalProducerIndex;

    if (Count > 0) {
        Xsk->Tx.Xdp.OutstandingFrames -= Count;

        //
        // If the below condition does not hold, the XSK TX completion ring is
        // no longer valid. This implies an application programming error.
        //
        if (NT_VERIFY(XskRingProdReserve(&Xsk->Tx.CompletionRing, Count) == Count)) {
            XskRingProdSubmit(&Xsk->Tx.CompletionRing, Count);
            EventWriteXskTxCompleteBatch(
                &MICROSOFT_XDP_PROVIDER, Xsk, OriginalProducerIndex, Count);
        } else {
            XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INVALID_RING);
        }

        //
        // N.B. See comment in XskNotify.
        //
        KeMemoryBarrier();

        if ((Xsk->IoWaitFlags & XSK_NOTIFY_FLAG_WAIT_TX) &&
            (KeReadStateEvent(&Xsk->IoWaitEvent) == 0 || Xsk->IoWaitIrp != NULL)) {
            XskSignalReadyIo(Xsk, XSK_NOTIFY_FLAG_WAIT_TX);
        }

        XskTxCompleteRundown(Xsk);
    }
}

NTSTATUS
XskReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ CONST VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskHandle
    )
{
    NTSTATUS Status;
    FILE_OBJECT *FileObject = NULL;
    HANDLE TargetHandle;
    XSK *Xsk = NULL;

    if (RequestorMode != KernelMode && !HandleBounced) {
        __try {
            ProbeForRead((VOID *)HandleBuffer, sizeof(HANDLE), PROBE_ALIGNMENT(HANDLE));
            TargetHandle = ReadHandleNoFence((volatile const HANDLE *)HandleBuffer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Status = GetExceptionCode();
            goto Exit;
        }
    } else {
        TargetHandle = *(HANDLE *)HandleBuffer;
    }

    Status =
        XdpReferenceObjectByHandle(
            TargetHandle, XDP_OBJECT_TYPE_XSK, RequestorMode, FILE_GENERIC_WRITE, &FileObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Xsk = FileObject->FsContext;
    XskReference(Xsk);
    *XskHandle = (HANDLE)Xsk;
    Status = STATUS_SUCCESS;

Exit:

    TraceInfo(TRACE_XSK, "Xsk=%p Status=%!STATUS!", Xsk, Status);

    if (FileObject != NULL) {
        ObDereferenceObject(FileObject);
    }

    return Status;
}

VOID
XskDereferenceDatapathHandle(
    _In_ HANDLE XskHandle
    )
{
    XSK *Xsk = XskHandle;

    TraceInfo(TRACE_XSK, "Xsk=%p", Xsk);

    XskDereference(Xsk);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XskIrpCreateSocket(
    _Inout_ IRP* Irp,
    _Inout_ IO_STACK_LOCATION* IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID* InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK *Xsk = NULL;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    Xsk = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Xsk), POOLTAG_XSK);
    if (Xsk == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Xsk->Header.ObjectType = XDP_OBJECT_TYPE_XSK;
    Xsk->Header.Dispatch = &XskFileDispatch;
    XdpInitializeReferenceCount(&Xsk->ReferenceCount);
    Xsk->State = XskUnbound;
    Xsk->Rx.Xdp.HookId.Layer = XDP_HOOK_L2;
    Xsk->Rx.Xdp.HookId.Direction = XDP_HOOK_RX;
    Xsk->Rx.Xdp.HookId.SubLayer = XDP_HOOK_INSPECT;
    Xsk->Tx.Xdp.HookId.Layer = XDP_HOOK_L2;
    Xsk->Tx.Xdp.HookId.Direction = XDP_HOOK_TX;
    Xsk->Tx.Xdp.HookId.SubLayer = XDP_HOOK_INJECT;
    KeInitializeSpinLock(&Xsk->Lock);
    KeInitializeEvent(&Xsk->IoWaitEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Xsk->PollRequested, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Xsk->Tx.Xdp.OutstandingFlushComplete, NotificationEvent, FALSE);

    IrpSp->FileObject->FsContext = Xsk;

    EventWriteXskCreateSocket(
        &MICROSOFT_XDP_PROVIDER, Xsk, PsGetCurrentProcessId());

Exit:

    TraceInfo(TRACE_XSK, "Xsk=%p Status=%!STATUS!", Xsk, Status);
    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskPollSocketNotify(
    _In_ VOID *NotifyContext
    )
{
    XSK *Xsk = NotifyContext;

    //
    // This routine is invoked by the NDIS polling backchannel when an interface
    // requests a poll.
    //

    KeSetEvent(&Xsk->PollRequested, 0, FALSE);
}

static
VOID
XskAcquirePollLock(
    _In_ XSK *Xsk
    )
{
    InterlockedIncrement((LONG *)&Xsk->PollWaiters);
    XskPollSocketNotify(Xsk);
    RtlAcquirePushLockExclusive(&Xsk->PollLock);
    InterlockedDecrement((LONG *)&Xsk->PollWaiters);
}

static
VOID
XskReleasePollLock(
    _In_ XSK *Xsk
    )
{
    RtlReleasePushLockExclusive(&Xsk->PollLock);
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskReleasePollModeSocket(
    _In_ XSK *Xsk
    )
{
    KIRQL OldIrql;

    //
    // Release control of any NDIS polling backchannels for a socket.
    //

    if (Xsk->Tx.Xdp.PollHandle != NULL) {
        if (Xsk->Tx.Xdp.PollHandle != Xsk->Rx.Xdp.PollHandle) {
            //
            // If the interface uses the same polling handle for RX and TX, only
            // one reference was acquired.
            //
            XdpPollReleaseExclusive(Xsk->Tx.Xdp.PollHandle);
            XdpPollDeleteBackchannel(Xsk->Tx.Xdp.PollHandle);
        }
        Xsk->Tx.Xdp.PollHandle = NULL;
        ASSERT(Xsk->Tx.Ring.Shared->Flags & XSK_RING_FLAG_NEED_POKE);
    }

    if (Xsk->Rx.Xdp.PollHandle != NULL) {
        Xsk->Rx.FillRing.Shared->Flags &= ~XSK_RING_FLAG_NEED_POKE;
        XdpPollReleaseExclusive(Xsk->Rx.Xdp.PollHandle);
        XdpPollDeleteBackchannel(Xsk->Rx.Xdp.PollHandle);
        Xsk->Rx.Xdp.PollHandle = NULL;
    }

    //
    // Reset the IO wait flag for socket polling mode.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    Xsk->IoWaitInternalFlags &= ~XSK_IO_WAIT_FLAG_POLL_MODE_SOCKET;
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskExitPollModeSocket(
    _In_ XSK *Xsk
    )
{
    Xsk->PollMode = XSK_POLL_MODE_DEFAULT;
    XskReleasePollModeSocket(Xsk);
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskAcquirePollModeSocketRx(
    _In_ XSK *Xsk
    )
{
    NDIS_HANDLE InterfaceRxPollHandle;
    NDIS_POLL_BACKCHANNEL *Backchannel = NULL;
    NTSTATUS Status;

    ASSERT(Xsk->Rx.Xdp.PollHandle == NULL);

    if (Xsk->Rx.Xdp.Queue == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    InterfaceRxPollHandle = XdpRxQueueGetInterfacePollHandle(Xsk->Rx.Xdp.Queue);
    if (InterfaceRxPollHandle == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = XdpPollCreateBackchannel(InterfaceRxPollHandle, &Backchannel);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPollAcquireExclusive(Backchannel, XskPollSocketNotify, Xsk);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Review: handling of multiple sockets sharing a queue.
    //
    Xsk->Rx.FillRing.Shared->Flags |= XSK_RING_FLAG_NEED_POKE;

    Xsk->Rx.Xdp.PollHandle = Backchannel;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Backchannel != NULL) {
            XdpPollDeleteBackchannel(Backchannel);
        }
    }

    return Status;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskAcquirePollModeSocketTx(
    _In_ XSK *Xsk
    )
{
    NTSTATUS Status;
    NDIS_HANDLE InterfaceTxPollHandle;
    NDIS_POLL_BACKCHANNEL *Backchannel = NULL;

    if (Xsk->Tx.Xdp.Queue == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    InterfaceTxPollHandle = XdpTxQueueGetInterfacePollHandle(Xsk->Tx.Xdp.Queue);
    if (InterfaceTxPollHandle == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (Xsk->Rx.Ring.Size > 0 &&
        InterfaceTxPollHandle == XdpRxQueueGetInterfacePollHandle(Xsk->Rx.Xdp.Queue)) {
        //
        // The interface uses the same polling context for RX and TX, and this
        // socket has already captured the RX handle. Use that for TX, too.
        //
        ASSERT(Xsk->Rx.Xdp.PollHandle != NULL);
        Xsk->Tx.Xdp.PollHandle = Xsk->Rx.Xdp.PollHandle;
        Status = STATUS_SUCCESS;
    } else {
        Status = XdpPollCreateBackchannel(InterfaceTxPollHandle, &Backchannel);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status = XdpPollAcquireExclusive(Backchannel, XskPollSocketNotify, Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        Xsk->Tx.Xdp.PollHandle = Backchannel;
        Status = STATUS_SUCCESS;
    }

    Xsk->Tx.Ring.Shared->Flags |= XSK_RING_FLAG_NEED_POKE;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Backchannel != NULL) {
            XdpPollDeleteBackchannel(Backchannel);
        }
    }

    return Status;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskAcquirePollModeSocket(
    _In_ XSK *Xsk
    )
{
    NTSTATUS Status;
    KIRQL OldIrql;
    UINT32 NotifyFlags = 0;

    if (Xsk->Rx.Ring.Size > 0 && Xsk->Rx.Xdp.PollHandle == NULL) {
        Status = XskAcquirePollModeSocketRx(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
    }

    if (Xsk->Tx.Ring.Size > 0 && Xsk->Tx.Xdp.PollHandle == NULL) {
        Status = XskAcquirePollModeSocketTx(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
    }

    //
    // Invalidate the internal notification state and cancel any outstanding
    // wait on the socket. We must ensure the data path thread makes forward
    // progress if poll modes are modified on another thread.
    //

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    Xsk->IoWaitInternalFlags |= XSK_IO_WAIT_FLAG_POLL_MODE_SOCKET;
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    XskSignalReadyIo(Xsk, NotifyFlags);

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XskReleasePollModeSocket(Xsk);
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskEnterPollModeSocket(
    _In_ XSK *Xsk
    )
{
    Xsk->PollMode = XSK_POLL_MODE_SOCKET;

    //
    // Polling mode is merely a hint to AF_XDP, so it's fine to silently fail if
    // socket polling is not available. The XSK might be attached to an RX queue
    // via XskNotifyAttachRxQueue whenever a new RX program is attached, so try
    // again then.
    //
    XskAcquirePollModeSocket(Xsk);

    return STATUS_SUCCESS;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskReleasePollModeBusyTx(
    _In_ XSK *Xsk
    )
{
    if (Xsk->Tx.Xdp.PollHandle != NULL) {
        XdpPollReleaseBusyReference(Xsk->Tx.Xdp.PollHandle);
        XdpPollDeleteBackchannel(Xsk->Tx.Xdp.PollHandle);
        Xsk->Tx.Xdp.PollHandle = NULL;

        //
        // Ensure the XSK ring need poke flag is set after releasing the busy
        // reference.
        //
        if (Xsk->Tx.Xdp.Flags.QueueActive) {
            XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_TX;

            XdpTxQueueInvokeInterfaceNotify(Xsk->Tx.Xdp.Queue, NotifyFlags);
        }
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskReleasePollModeBusyRx(
    _In_ XSK *Xsk
    )
{
    if (Xsk->Rx.Xdp.PollHandle != NULL) {
        XdpPollReleaseBusyReference(Xsk->Rx.Xdp.PollHandle);
        XdpPollDeleteBackchannel(Xsk->Rx.Xdp.PollHandle);
        Xsk->Rx.Xdp.PollHandle = NULL;

        ASSERT((Xsk->Rx.FillRing.Shared->Flags & XSK_RING_FLAG_NEED_POKE) == 0);
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskExitPollModeBusy(
    _In_ XSK *Xsk
    )
{
    Xsk->PollMode = XSK_POLL_MODE_DEFAULT;
    XskReleasePollModeBusyTx(Xsk);
    XskReleasePollModeBusyRx(Xsk);
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskAcquirePollModeBusyRx(
    _In_ XSK *Xsk
    )
{
    NDIS_HANDLE InterfaceRxPollHandle;
    NDIS_POLL_BACKCHANNEL *Backchannel = NULL;
    NTSTATUS Status;

    ASSERT(Xsk->Rx.Xdp.PollHandle == NULL);

    if (Xsk->Rx.Xdp.Queue == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    InterfaceRxPollHandle = XdpRxQueueGetInterfacePollHandle(Xsk->Rx.Xdp.Queue);
    if (InterfaceRxPollHandle == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = XdpPollCreateBackchannel(InterfaceRxPollHandle, &Backchannel);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPollAddBusyReference(Backchannel);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Review: handling of multiple sockets sharing a queue.
    //
    Xsk->Rx.FillRing.Shared->Flags &= ~XSK_RING_FLAG_NEED_POKE;
    Xsk->Rx.Xdp.PollHandle = Backchannel;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Backchannel != NULL) {
            XdpPollDeleteBackchannel(Backchannel);
        }
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskAcquirePollModeBusyTx(
    _In_ XSK *Xsk
    )
{
    NTSTATUS Status;
    NDIS_HANDLE InterfaceTxPollHandle;
    NDIS_POLL_BACKCHANNEL *Backchannel = NULL;

    if (Xsk->Tx.Xdp.Queue == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    InterfaceTxPollHandle = XdpTxQueueGetInterfacePollHandle(Xsk->Tx.Xdp.Queue);
    if (InterfaceTxPollHandle == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = XdpPollCreateBackchannel(InterfaceTxPollHandle, &Backchannel);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPollAddBusyReference(Backchannel);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Xsk->Tx.Ring.Shared->Flags &= ~XSK_RING_FLAG_NEED_POKE;
    Xsk->Tx.Xdp.PollHandle = Backchannel;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Backchannel != NULL) {
            XdpPollDeleteBackchannel(Backchannel);
        }
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskAcquirePollModeBusy(
    _In_ XSK *Xsk
    )
{
    if (Xsk->Rx.Ring.Size > 0 && Xsk->Rx.Xdp.PollHandle == NULL) {
        XskAcquirePollModeBusyRx(Xsk);

    }

    if (Xsk->Tx.Ring.Size > 0 && Xsk->Tx.Xdp.PollHandle == NULL) {
        XskAcquirePollModeBusyTx(Xsk);
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskEnterPollModeBusy(
    _In_ XSK *Xsk
    )
{
    Xsk->PollMode = XSK_POLL_MODE_BUSY;

    //
    // Polling mode is merely a hint to AF_XDP, so it's fine to silently fail if
    // socket polling is not available. The XSK might be attached to an RX queue
    // via XskNotifyAttachRxQueue whenever a new RX program is attached, so try
    // again then.
    //
    XskAcquirePollModeBusy(Xsk);

    return STATUS_SUCCESS;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskSetPollMode(
    _In_ XSK *Xsk,
    _In_ XSK_POLL_MODE PollMode
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Xsk->State != XskActive && PollMode != XSK_POLL_MODE_DEFAULT) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    //
    // Exit the old polling mode and return to the default state.
    //
    switch (Xsk->PollMode) {

    case XSK_POLL_MODE_DEFAULT:
        // Nothing to do.
        break;

    case XSK_POLL_MODE_BUSY:
        XskExitPollModeBusy(Xsk);
        break;

    case XSK_POLL_MODE_SOCKET:
        XskExitPollModeSocket(Xsk);
        break;

    default:
        ASSERT(FALSE);
    }

    ASSERT(Xsk->PollMode == XSK_POLL_MODE_DEFAULT);

    //
    // Enter the new polling mode.
    //
    switch (PollMode) {

    case XSK_POLL_MODE_DEFAULT:
        // Nothing to do.
        break;

    case XSK_POLL_MODE_BUSY:
        Status = XskEnterPollModeBusy(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        goto Exit;

    case XSK_POLL_MODE_SOCKET:
        Status = XskEnterPollModeSocket(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
XskNotifyDetachRxQueue(
    _In_ XSK *Xsk
    )
{
    XskAcquirePollLock(Xsk);

    //
    // This socket is being unbound from an RX queue, so release all polling
    // backchannels and deactivate the current polling mode.
    //
    switch (Xsk->PollMode) {

    case XSK_POLL_MODE_BUSY:
        XskReleasePollModeBusyRx(Xsk);
        break;

    case XSK_POLL_MODE_SOCKET:
        XskReleasePollModeSocket(Xsk);
        break;

    }

    XskReleasePollLock(Xsk);
}

static
VOID
XskNotifyDetachRxQueueComplete(
    _In_ XSK *Xsk
    )
{
    Xsk->Rx.Xdp.FrameRing = NULL;
    Xsk->Rx.Xdp.FragmentRing = NULL;
    RtlZeroMemory(&Xsk->Rx.Xdp.VaExtension, sizeof(Xsk->Rx.Xdp.VaExtension));
    RtlZeroMemory(&Xsk->Rx.Xdp.FragmentExtension, sizeof(Xsk->Rx.Xdp.FragmentExtension));
    RtlZeroMemory(&Xsk->Rx.Xdp.RxActionExtension, sizeof(Xsk->Rx.Xdp.RxActionExtension));
}

static
VOID
XskNotifyAttachRxQueue(
    _In_ XSK *Xsk
    )
{
    XDP_RX_QUEUE_CONFIG_ACTIVATE Config = XdpRxQueueGetConfig(Xsk->Rx.Xdp.Queue);
    XDP_EXTENSION_INFO ExtensionInfo;

    Xsk->Rx.Xdp.FrameRing = XdpRxQueueGetFrameRing(Config);

    ASSERT(XdpRxQueueIsVirtualAddressEnabled(Config));
    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Rx.Xdp.VaExtension);

    XdpInitializeExtensionInfo(
        &ExtensionInfo, XDP_FRAME_EXTENSION_RX_ACTION_NAME,
        XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
    XdpRxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Rx.Xdp.RxActionExtension);

    if (XdpRxQueueGetMaximumFragments(Config) > 1) {
        Xsk->Rx.Xdp.FragmentRing = XdpRxQueueGetFragmentRing(Config);

        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
            XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpRxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Rx.Xdp.FragmentExtension);
    }

    XskAcquirePollLock(Xsk);

    if (Xsk->State == XskActive) {
        //
        // Now that this socket is bound to an RX queue, attempt to acquire the
        // polling backchannel(s) and activate the appropriate polling mode.
        //
        switch (Xsk->PollMode) {

        case XSK_POLL_MODE_BUSY:
            XskAcquirePollModeBusyRx(Xsk);
            break;

        case XSK_POLL_MODE_SOCKET:
            XskAcquirePollModeSocket(Xsk);
            break;

        }
    }

    XskReleasePollLock(Xsk);

    Xsk->Rx.Xdp.Flags.DatapathAttached = TRUE;
}

VOID
XskNotifyRxQueue(
    _In_ XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    _In_ XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XSK *Xsk = CONTAINING_RECORD(NotificationEntry, XSK, Rx.Xdp.QueueNotificationEntry);

    TraceInfo(TRACE_XSK, "Xsk=%p NotificationType=%u", Xsk, NotificationType);

    switch (NotificationType) {

    case XDP_RX_QUEUE_NOTIFICATION_ATTACH:
        XskNotifyAttachRxQueue(Xsk);
        break;

    case XDP_RX_QUEUE_NOTIFICATION_DETACH:
        XskNotifyDetachRxQueue(Xsk);
        break;

    case XDP_RX_QUEUE_NOTIFICATION_DETACH_COMPLETE:
        XskNotifyDetachRxQueueComplete(Xsk);
        break;

    }
}

NTSTATUS
XskValidateDatapathHandle(
    _In_ HANDLE XskHandle
    )
{
    XSK *Xsk = (XSK *)XskHandle;

    if (Xsk->State < XskBound) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Xsk->Rx.Xdp.Queue == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
XskCanBypass(
    _In_ HANDLE XskHandle,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    XSK *Xsk = (XSK *)XskHandle;

    //
    // Allow XSKs that are terminally disconnected to bypass on any queue since
    // the receive path becomes a NOP.
    //
    if (Xsk->State > XskActive && !Xsk->Rx.Xdp.Flags.DatapathAttached) {
        return TRUE;
    }

    if (Xsk->Rx.Xdp.Queue != RxQueue) {
        return FALSE;
    }

    return TRUE;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XskIrpCleanup(
    _Inout_ IRP* Irp,
    _Inout_ IO_STACK_LOCATION* IrpSp
    )
{
    XSK *Xsk;
    KIRQL OldIrql;
    UINT32 IoWaitFlags;

    UNREFERENCED_PARAMETER(Irp);

    Xsk = IrpSp->FileObject->FsContext;
    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    //
    // Synchronize the polling execution context with socket cleanup: the socket
    // is protected from cleanup as long as the polling lock is held.
    //
    XskAcquirePollLock(Xsk);

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    Xsk->State = XskClosing;
    IoWaitFlags = Xsk->IoWaitFlags;
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    //
    // Revert any polling mode state set by this socket.
    //
    NT_VERIFY(XskSetPollMode(Xsk, XSK_POLL_MODE_DEFAULT) == STATUS_SUCCESS);

    XskReleasePollLock(Xsk);

    if (IoWaitFlags != 0) {
        XskSignalReadyIo(Xsk, IoWaitFlags);
    }

    TraceInfo(TRACE_XSK, "Xsk=%p Status=%!STATUS!", Xsk, STATUS_SUCCESS);

    TraceExitSuccess(TRACE_XSK);

    return STATUS_SUCCESS;
}

static
VOID
XskFreeRing(
    XSK_KERNEL_RING *Ring
    )
{
    ASSERT(
        (Ring->Size != 0 && Ring->Mdl != NULL && Ring->Shared != NULL) ||
        (Ring->Size == 0 && Ring->Mdl == NULL && Ring->Shared == NULL));

    if (Ring->UserVa != NULL) {
        VOID *CurrentProcess = PsGetCurrentProcess();
        KAPC_STATE ApcState;

        ASSERT(Ring->OwningProcess != NULL);

        if (CurrentProcess != Ring->OwningProcess) {
            KeStackAttachProcess(Ring->OwningProcess, &ApcState);
        }

        ASSERT(Ring->Mdl);
        MmUnmapLockedPages(Ring->UserVa, Ring->Mdl);

        if (CurrentProcess != Ring->OwningProcess) {
#pragma prefast(suppress:6001, "ApcState is correctly initialized in KeStackAttachProcess above.")
            KeUnstackDetachProcess(&ApcState);
        }

        ObDereferenceObject(Ring->OwningProcess);
        Ring->OwningProcess = NULL;
    }
    if (Ring->Mdl != NULL) {
        IoFreeMdl(Ring->Mdl);
    }
    if (Ring->Shared != NULL) {
        ExFreePoolWithTag(Ring->Shared, POOLTAG_RING);
    }
}

static
NTSTATUS
XskSetupDma(
    _In_ XSK *Xsk
    )
{
    NTSTATUS Status;
    DEVICE_DESCRIPTION DeviceDescription = {0};
    ULONG NumberOfMapRegisters = 0;
    UMEM_MAPPING *Mapping;
    CONST XDP_DMA_CAPABILITIES *DmaCapabilities =
        XdpTxQueueGetDmaCapabilities(Xsk->Tx.Xdp.Queue);
    CONST DMA_OPERATIONS *DmaOperations;

    ASSERT(DmaCapabilities->PhysicalDeviceObject != NULL);

    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION3;
    DeviceDescription.Master = TRUE;
    DeviceDescription.ScatterGather = TRUE;
    DeviceDescription.InterfaceType = InterfaceTypeUndefined;
    DeviceDescription.MaximumLength = ((ULONG)(1 << 17)); // 128 KB
    DeviceDescription.DmaAddressWidth = 64;

    Xsk->Tx.DmaAdapter =
        IoGetDmaAdapter(
            DmaCapabilities->PhysicalDeviceObject, &DeviceDescription, &NumberOfMapRegisters);
    if (Xsk->Tx.DmaAdapter == NULL) {
        TraceError(TRACE_XSK, "Xsk=%p Failed to get DMA adapter", Xsk);
        return STATUS_NO_MEMORY;
    }

    DmaOperations = (DMA_OPERATIONS*)Xsk->Tx.DmaAdapter->DmaOperations;

    //
    // Try to map the UMEM directly to hardware if policy allows and the DMA
    // adapter supports it.
    //
    if (!XskRequiresTxBounceBuffer(Xsk) &&
        RTL_CONTAINS_FIELD(DmaOperations, DmaOperations->Size, CreateCommonBufferFromMdl)) {
        Mapping = &Xsk->Umem->Mapping;
        Status =
            DmaOperations->CreateCommonBufferFromMdl(
                Xsk->Tx.DmaAdapter, Mapping->Mdl, NULL, 0, &Mapping->DmaAddress);
        if (Status == STATUS_SUCCESS) {
            TraceInfo(TRACE_XSK, "Xsk=%p Successfully created common buffer", Xsk);
            return STATUS_SUCCESS;
        }
    }

    //
    // Fall-back to allocating a bounce buffer that can be mapped to hardware.
    //
    Mapping = &Xsk->Tx.Bounce.Mapping;
    Mapping->SystemAddress =
        DmaOperations->AllocateCommonBuffer(
            Xsk->Tx.DmaAdapter, (ULONG)Xsk->Umem->Reg.TotalSize, &Mapping->DmaAddress, TRUE);
    if (Mapping->SystemAddress == NULL) {
        TraceWarn(TRACE_XSK, "Xsk=%p Failed to allocate common buffer", Xsk);
        return STATUS_NO_MEMORY;
    }
    Xsk->Tx.Bounce.AllocationSource = AllocatedByDma;

    TraceInfo(TRACE_XSK, "Xsk=%p Successfully allocated common buffer", Xsk);

    return STATUS_SUCCESS;
}

static
VOID
XskCleanupDma(
    XSK *Xsk
    )
{
    if (Xsk->Umem != NULL) {
        UMEM_MAPPING *Mapping = XskGetTxMapping(Xsk);

        if (Mapping->DmaAddress.QuadPart != 0) {
            Xsk->Tx.DmaAdapter->DmaOperations->FreeCommonBuffer(
                Xsk->Tx.DmaAdapter,
                Mapping->Mdl->ByteCount,
                Mapping->DmaAddress,
                Mapping->SystemAddress,
                TRUE);
            Mapping->DmaAddress.QuadPart = 0;

            if (Xsk->Tx.Bounce.AllocationSource == AllocatedByDma) {
                ASSERT(&Xsk->Tx.Bounce.Mapping == Mapping);
                Mapping->SystemAddress = NULL;
                Xsk->Tx.Bounce.AllocationSource = NotAllocated;
            }
        }
    }

    if (Xsk->Tx.DmaAdapter != NULL) {
        Xsk->Tx.DmaAdapter->DmaOperations->PutDmaAdapter(Xsk->Tx.DmaAdapter);
        Xsk->Tx.DmaAdapter = NULL;
    }
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskRxSyncDetach(
    _In_opt_ VOID *Context
    )
{
    XSK *Xsk = Context;
    ASSERT(Xsk);
    Xsk->Rx.Xdp.Flags.DatapathAttached = FALSE;
}

static
VOID
XskDetachRxIf(
    _In_ XSK *Xsk
    )
{
    KIRQL OldIrql;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (Xsk->Rx.Xdp.Queue != NULL) {
        if (Xsk->Rx.Xdp.Flags.NotificationsRegistered) {
            XdpRxQueueSync(Xsk->Rx.Xdp.Queue, XskRxSyncDetach, Xsk);
            XdpRxQueueDeregisterNotifications(Xsk->Rx.Xdp.Queue, &Xsk->Rx.Xdp.QueueNotificationEntry);
            Xsk->Rx.Xdp.Flags.NotificationsRegistered = FALSE;
        }

        XskNotifyDetachRxQueue(Xsk);
        XskNotifyDetachRxQueueComplete(Xsk);
        XdpRxQueueDereference(Xsk->Rx.Xdp.Queue);
        Xsk->Rx.Xdp.Queue = NULL;
    }

    if (Xsk->Rx.Xdp.IfHandle != NULL) {
        XdpIfDereferenceBinding(Xsk->Rx.Xdp.IfHandle);

        //
        // Synchronize with non-binding-workers while clearing the interface
        // handle.
        //
        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
        Xsk->Rx.Xdp.IfHandle = NULL;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    if (Xsk->State >= XskActive) {
        XskKernelRingSetError(&Xsk->Rx.Ring, XSK_ERROR_INTERFACE_DETACH);
        XskKernelRingSetError(&Xsk->Rx.FillRing, XSK_ERROR_INTERFACE_DETACH);
    }

    TraceExitSuccess(TRACE_XSK);
}

static
VOID
XskDeactivateTxIf(
    _In_ XSK *Xsk
    )
{
    if (Xsk->Tx.Xdp.Flags.QueueActive) {
        //
        // Wait for all outstanding TX frames to complete. We need to
        // synchronize with the data path: if there are frames outstanding,
        // then the OutstandingFlushComplete event will be set as part of
        // the regular data path. If no frames are outstanding, to ensure
        // the OutstandingFrames count is compared to zero within the data
        // path's execution context, an extra callback is required.
        //
        ASSERT(Xsk->State > XskActive);
        XdpTxQueueSync(Xsk->Tx.Xdp.Queue, XskTxCompleteRundown, Xsk);
        KeWaitForSingleObject(
            &Xsk->Tx.Xdp.OutstandingFlushComplete, Executive, KernelMode, FALSE, NULL);
        ASSERT(Xsk->Tx.Xdp.OutstandingFrames == 0);

        RtlAcquirePushLockExclusive(&Xsk->PollLock);
        Xsk->Tx.Xdp.Flags.QueueActive = FALSE;
        RtlReleasePushLockExclusive(&Xsk->PollLock);
    }

    if (Xsk->Tx.Xdp.Flags.QueueInserted) {
        XdpTxQueueRemoveDatapathClient(Xsk->Tx.Xdp.Queue, &Xsk->Tx.Xdp.DatapathClientEntry);
        Xsk->Tx.Xdp.Flags.QueueInserted = FALSE;
    }

    XskCleanupDma(Xsk);
    XskFreeBounceBuffer(&Xsk->Tx.Bounce);
}

static
VOID
XskDetachTxIf(
    _In_ XSK *Xsk
    )
{
    KIRQL OldIrql;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (Xsk->Tx.Xdp.Queue != NULL) {
        XskDeactivateTxIf(Xsk);
        XdpTxQueueDeregisterNotifications(Xsk->Tx.Xdp.Queue, &Xsk->Tx.Xdp.QueueNotificationEntry);
        XdpTxQueueDereference(Xsk->Tx.Xdp.Queue);
        Xsk->Tx.Xdp.Queue = NULL;
    }

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        XdpIfDereferenceBinding(Xsk->Tx.Xdp.IfHandle);

        //
        // Synchronize with non-binding-workers while clearing the interface
        // handle.
        //
        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
        Xsk->Tx.Xdp.IfHandle = NULL;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    if (Xsk->State >= XskActive) {
        XskKernelRingSetError(&Xsk->Tx.Ring, XSK_ERROR_INTERFACE_DETACH);
        XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INTERFACE_DETACH);
    }

    TraceExitSuccess(TRACE_XSK);
}

VOID
XskNotifyTxQueue(
    _In_ XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    _In_ XDP_TX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XSK *Xsk = CONTAINING_RECORD(NotificationEntry, XSK, Tx.Xdp.QueueNotificationEntry);
    KIRQL OldIrql;

    if (NotificationType != XDP_TX_QUEUE_NOTIFICATION_DETACH) {
        return;
    }

    //
    // Set the state to detached, except when socket closure has raced this
    // detach event.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if (Xsk->State != XskClosing) {
        ASSERT(Xsk->State >= XskBinding && Xsk->State <= XskActive);
        Xsk->State = XskDetached;
    }
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    //
    // This detach event is executing in the context of XDP binding work queue
    // processing on the TX queue, so it is synchronized with other detach
    // instances that also utilize the XDP binding work queue (detach during
    // bind failure and detach during socket closure).
    //
    XskDetachTxIf(Xsk);
}

static
VOID
XskDetachRxIfWorker(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;

    XskDetachRxIf(Xsk);

    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskDetachTxIfWorker(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;

    XskDetachTxIf(Xsk);

    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskBindRxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    ASSERT(Xsk->Rx.Xdp.IfHandle == NULL);
    Xsk->Rx.Xdp.IfHandle = WorkItem->IfWorkItem.BindingHandle;

    Status =
        XdpRxQueueFindOrCreate(
            Xsk->Rx.Xdp.IfHandle, &Xsk->Rx.Xdp.HookId, WorkItem->QueueId, &Xsk->Rx.Xdp.Queue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XskDetachRxIf(Xsk);
    }

    TraceExitStatus(TRACE_XSK);

    WorkItem->CompletionStatus = Status;
    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskActivateCommitRxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (Xsk->Rx.Xdp.IfHandle == NULL) {
        ASSERT(Xsk->State > XskActive);
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    XdpRxQueueRegisterNotifications(
        Xsk->Rx.Xdp.Queue, &Xsk->Rx.Xdp.QueueNotificationEntry, XskNotifyRxQueue);
    Xsk->Rx.Xdp.Flags.NotificationsRegistered = TRUE;

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_XSK);

    WorkItem->CompletionStatus = Status;
    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskBindTxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    XDP_TX_QUEUE_CONFIG_ACTIVATE Config;
    CONST XDP_TX_CAPABILITIES *InterfaceCapabilities;
    XDP_EXTENSION_INFO ExtensionInfo;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);
    Xsk->Tx.Xdp.IfHandle = WorkItem->IfWorkItem.BindingHandle;

    Status =
        XdpTxQueueFindOrCreate(
            Xsk->Tx.Xdp.IfHandle, &Xsk->Tx.Xdp.HookId, WorkItem->QueueId, &Xsk->Tx.Xdp.Queue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XdpTxQueueRegisterNotifications(
        Xsk->Tx.Xdp.Queue, &Xsk->Tx.Xdp.QueueNotificationEntry, XskNotifyTxQueue);

    InterfaceCapabilities = XdpTxQueueGetCapabilities(Xsk->Tx.Xdp.Queue);
    Xsk->Tx.Xdp.MaxBufferLength = InterfaceCapabilities->MaximumBufferSize;
    Xsk->Tx.Xdp.MaxFrameLength = InterfaceCapabilities->MaximumFrameSize;

    Config = XdpTxQueueGetConfig(Xsk->Tx.Xdp.Queue);

    Xsk->Tx.Xdp.Flags.OutOfOrderCompletion = XdpTxQueueIsOutOfOrderCompletionEnabled(Config);
    Xsk->Tx.Xdp.Flags.CompletionContext = XdpTxQueueIsTxCompletionContextEnabled(Config);

    Xsk->Tx.Xdp.FrameRing = XdpTxQueueGetFrameRing(Config);

    if (Xsk->Tx.Xdp.Flags.CompletionContext) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
            XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
            XDP_EXTENSION_TYPE_FRAME);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.FrameTxCompletionExtension);
    }

    if (Xsk->Tx.Xdp.Flags.OutOfOrderCompletion) {
        Xsk->Tx.Xdp.CompletionRing = XdpTxQueueGetCompletionRing(Config);

        if (Xsk->Tx.Xdp.Flags.CompletionContext) {
            XdpInitializeExtensionInfo(
                &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
                XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
                XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION);
            XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.TxCompletionExtension);
        }
    }

    Xsk->Tx.Xdp.Flags.VirtualAddressExt = XdpTxQueueIsVirtualAddressEnabled(Config);
    if (Xsk->Tx.Xdp.Flags.VirtualAddressExt) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
            XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.VaExtension);
    }

    Xsk->Tx.Xdp.Flags.LogicalAddressExt = XdpTxQueueIsLogicalAddressEnabled(Config);
    if (Xsk->Tx.Xdp.Flags.LogicalAddressExt) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME,
            XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.LaExtension);
    }

    Xsk->Tx.Xdp.Flags.MdlExt = XdpTxQueueIsMdlEnabled(Config);
    if (Xsk->Tx.Xdp.Flags.MdlExt) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
            XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.MdlExtension);
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XskDetachTxIf(Xsk);
    }

    TraceExitStatus(TRACE_XSK);

    WorkItem->CompletionStatus = Status;
    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskActivateTxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    ASSERT(Xsk->Tx.Ring.Size > 0);

    if (Xsk->Tx.Xdp.Queue == NULL) {
        ASSERT(Xsk->State > XskActive);
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    if (Xsk->Tx.Xdp.Flags.LogicalAddressExt) {
        Status = XskSetupDma(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status = XskAllocateTxBounceBuffer(Xsk);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpTxQueueAddDatapathClient(
            Xsk->Tx.Xdp.Queue, &Xsk->Tx.Xdp.DatapathClientEntry,
            XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE_XSK);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Xsk->Tx.Xdp.Flags.QueueInserted = TRUE;

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        XskDeactivateTxIf(Xsk);
    }

    TraceExitStatus(TRACE_XSK);

    WorkItem->CompletionStatus = Status;
    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskActivateCommitTxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    ASSERT(Xsk->Tx.Ring.Size > 0);

    if (Xsk->Tx.Xdp.Queue == NULL) {
        ASSERT(Xsk->State > XskActive);
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&Xsk->PollLock);
    Xsk->Tx.Xdp.Flags.QueueActive = TRUE;
    RtlReleasePushLockExclusive(&Xsk->PollLock);

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_XSK);

    WorkItem->CompletionStatus = Status;
    KeSetEvent(&WorkItem->CompletionEvent, 0, FALSE);
}

static
VOID
XskReferenceUmem(
    UMEM *Umem
    )
{
    XdpIncrementReferenceCount(&Umem->ReferenceCount);
}

static
VOID
XskDereferenceUmem(
    UMEM *Umem
    )
{
    if (XdpDecrementReferenceCount(&Umem->ReferenceCount)) {
        TraceInfo(TRACE_XSK, "Destroying Umem=%p", Umem);

        if (Umem->Mapping.Mdl != NULL) {
            if (Umem->ReservedMapping != NULL) {
                if (Umem->Mapping.SystemAddress != NULL) {
                    MmUnmapReservedMapping(Umem->ReservedMapping, POOLTAG_UMEM, Umem->Mapping.Mdl);
                }
                MmFreeMappingAddress(Umem->ReservedMapping, POOLTAG_UMEM);
            }
            if (Umem->Mapping.Mdl->MdlFlags & MDL_PAGES_LOCKED) {
                MmUnlockPages(Umem->Mapping.Mdl);
            }
            IoFreeMdl(Umem->Mapping.Mdl);
        }
        ExFreePoolWithTag(Umem, POOLTAG_UMEM);
    }
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XskIrpClose(
    _Inout_ IRP* Irp,
    _Inout_ IO_STACK_LOCATION* IrpSp
    )
{
    XSK *Xsk = IrpSp->FileObject->FsContext;
    KIRQL OldIrql;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);
    EventWriteXskCloseSocketStart(&MICROSOFT_XDP_PROVIDER, Xsk);

    UNREFERENCED_PARAMETER(Irp);

    ASSERT(Xsk->State == XskClosing);

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        XSK_BINDING_WORKITEM WorkItem = {0};

        KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
        WorkItem.Xsk = Xsk;
        WorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;
        WorkItem.IfWorkItem.WorkRoutine = XskDetachTxIfWorker;
        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

        KeReleaseSpinLock(&Xsk->Lock, OldIrql);

        KeWaitForSingleObject(
            &WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);

        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    }

    if (Xsk->Rx.Xdp.IfHandle != NULL) {
        XSK_BINDING_WORKITEM WorkItem = {0};

        KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
        WorkItem.Xsk = Xsk;
        WorkItem.IfWorkItem.BindingHandle = Xsk->Rx.Xdp.IfHandle;
        WorkItem.IfWorkItem.WorkRoutine = XskDetachRxIfWorker;
        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

        KeReleaseSpinLock(&Xsk->Lock, OldIrql);

        KeWaitForSingleObject(
            &WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);

        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    }

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    if (Xsk->Umem != NULL) {
        XskDereferenceUmem(Xsk->Umem);
    }

    XskFreeRing(&Xsk->Rx.Ring);
    XskFreeRing(&Xsk->Rx.FillRing);
    XskFreeRing(&Xsk->Tx.Ring);
    XskFreeRing(&Xsk->Tx.CompletionRing);

    XskDereference(Xsk);

    EventWriteXskCloseSocketStop(&MICROSOFT_XDP_PROVIDER, Xsk);
    TraceInfo(TRACE_XSK, "Xsk=%p Status=%!STATUS!", Xsk, STATUS_SUCCESS);
    TraceExitSuccess(TRACE_XSK);

    return STATUS_SUCCESS;
}

static
NTSTATUS
XskIrpBindSocket(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK *Xsk = IrpSp->FileObject->FsContext;
    XSK_BIND_IN Bind = {0};
    XSK_BINDING_WORKITEM WorkItem = {0};
    KIRQL OldIrql;
    XDP_INTERFACE_MODE *ModeFilter = NULL;
    XDP_INTERFACE_MODE RequiredMode;
    BOOLEAN BindIfInitiated = FALSE;
    CONST UINT32 ValidFlags = XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX | XSK_BIND_FLAG_GENERIC | XSK_BIND_FLAG_NATIVE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(XSK_BIND_IN)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Bind = *(XSK_BIND_IN*)Irp->AssociatedIrp.SystemBuffer;

    TraceEnter(
        TRACE_XSK, "Xsk=%p IfIndex=%u QueueId=%u Flags=%x",
        Xsk, Bind.IfIndex, Bind.QueueId, Bind.Flags);

    if ((Bind.Flags & ~ValidFlags) ||
        !RTL_IS_CLEAR_OR_SINGLE_FLAG(Bind.Flags, XSK_BIND_FLAG_GENERIC | XSK_BIND_FLAG_NATIVE) ||
        (Bind.Flags & (XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX)) == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Bind.Flags & XSK_BIND_FLAG_GENERIC) {
        RequiredMode = XDP_INTERFACE_MODE_GENERIC;
        ModeFilter = &RequiredMode;
    }

    if (Bind.Flags & XSK_BIND_FLAG_NATIVE) {
        RequiredMode = XDP_INTERFACE_MODE_NATIVE;
        ModeFilter = &RequiredMode;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (Xsk->State != XskUnbound) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }

    Xsk->State = XskBinding;
    BindIfInitiated = TRUE;

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    KeInitializeEvent(&WorkItem.CompletionEvent, SynchronizationEvent, FALSE);

    if (Bind.Flags & XSK_BIND_FLAG_RX) {
        WorkItem.Xsk = Xsk;
        WorkItem.QueueId = Bind.QueueId;
        WorkItem.IfWorkItem.WorkRoutine = XskBindRxIf;
        WorkItem.IfWorkItem.BindingHandle =
            XdpIfFindAndReferenceBinding(Bind.IfIndex, &Xsk->Rx.Xdp.HookId, 1, ModeFilter);
        if (WorkItem.IfWorkItem.BindingHandle == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

        KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = WorkItem.CompletionStatus;
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    if (Bind.Flags & XSK_BIND_FLAG_TX) {
        WorkItem.Xsk = Xsk;
        WorkItem.QueueId = Bind.QueueId;
        WorkItem.IfWorkItem.WorkRoutine = XskBindTxIf;
        WorkItem.IfWorkItem.BindingHandle =
            XdpIfFindAndReferenceBinding(Bind.IfIndex, &Xsk->Tx.Xdp.HookId, 1, ModeFilter);
        if (WorkItem.IfWorkItem.BindingHandle == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

        KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = WorkItem.CompletionStatus;
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

Exit:

    if (!NT_SUCCESS(Status) && BindIfInitiated) {
        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

        if (Xsk->Tx.Xdp.IfHandle != NULL) {
            WorkItem.Xsk = Xsk;
            WorkItem.QueueId = Bind.QueueId;
            WorkItem.IfWorkItem.WorkRoutine = XskDetachTxIfWorker;
            WorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;
            XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

            KeReleaseSpinLock(&Xsk->Lock, OldIrql);

            KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
            ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);

            KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
        }

        if (Xsk->Rx.Xdp.IfHandle != NULL) {
            WorkItem.Xsk = Xsk;
            WorkItem.QueueId = Bind.QueueId;
            WorkItem.IfWorkItem.WorkRoutine = XskDetachRxIfWorker;
            WorkItem.IfWorkItem.BindingHandle = Xsk->Rx.Xdp.IfHandle;
            XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

            KeReleaseSpinLock(&Xsk->Lock, OldIrql);

            KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
            ASSERT(Xsk->Rx.Xdp.IfHandle == NULL);

            KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
        }

        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (BindIfInitiated) {
        //
        // N.B. Current synchronization allows the socket to be closed and state
        // set to XskClosing while the bind is in progress. This is OK as socket
        // cleanup will happen after all file object references are released and
        // we currently are holding a reference to service this IOCTL. Do not
        // overwrite the state in this case.
        //
        if (Xsk->State == XskBinding) {
            if (NT_SUCCESS(Status)) {
                Xsk->State = XskBound;
            } else {
                Xsk->State = XskUnbound;
            }
        }
    }

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    TraceInfo(
        TRACE_XSK, "Xsk=%p IfIndex=%u QueueId=%u Flags=%x Status=%!STATUS!",
        Xsk, Bind.IfIndex, Bind.QueueId, Bind.Flags, Status);

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskIrpActivateSocket(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK *Xsk = IrpSp->FileObject->FsContext;
    XSK_ACTIVATE_IN Activate = {0};
    XSK_BINDING_WORKITEM RxWorkItem = {0}, TxWorkItem = {0};
    KIRQL OldIrql;
    BOOLEAN ActivateIfInitiated = FALSE;
    BOOLEAN ActivateIfCommitted = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(Activate)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Activate = *(XSK_ACTIVATE_IN *)Irp->AssociatedIrp.SystemBuffer;

    TraceEnter(TRACE_XSK, "Xsk=%p Flags=%x", Xsk, Activate.Flags);

    if (Activate.Flags != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (Xsk->Umem == NULL) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->State != XskBound) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->Rx.Xdp.Queue != NULL &&
        (Xsk->Rx.Ring.Size == 0 || Xsk->Rx.FillRing.Size == 0)) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->Tx.Xdp.Queue != NULL &&
        (Xsk->Tx.Ring.Size == 0 || Xsk->Tx.CompletionRing.Size == 0)) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }

    Xsk->State = XskActivating;
    ActivateIfInitiated = TRUE;

    KeInitializeEvent(&RxWorkItem.CompletionEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&TxWorkItem.CompletionEvent, SynchronizationEvent, FALSE);

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        TxWorkItem.Xsk = Xsk;
        TxWorkItem.IfWorkItem.WorkRoutine = XskActivateTxIf;
        TxWorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;

        XdpIfQueueWorkItem(&TxWorkItem.IfWorkItem);

        KeReleaseSpinLock(&Xsk->Lock, OldIrql);

        KeWaitForSingleObject(&TxWorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = TxWorkItem.CompletionStatus;
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    }

    //
    // The RX and TX queues have been configured as far as possible without
    // observably activating the data path. We are now committed to activation
    // or terminal failure; the XSK can no longer be reverted to XskBound state.
    //

    ActivateIfCommitted = TRUE;

    if (Xsk->State != XskActivating) {
        Status = STATUS_DELETE_PENDING;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        TxWorkItem.Xsk = Xsk;
        TxWorkItem.IfWorkItem.WorkRoutine = XskActivateCommitTxIf;
        TxWorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;

        XdpIfQueueWorkItem(&TxWorkItem.IfWorkItem);
    } else {
        TxWorkItem.CompletionStatus = STATUS_SUCCESS;
        KeSetEvent(&TxWorkItem.CompletionEvent, 0, FALSE);
    }

    if (Xsk->Rx.Xdp.IfHandle != NULL) {
        RxWorkItem.Xsk = Xsk;
        RxWorkItem.IfWorkItem.WorkRoutine = XskActivateCommitRxIf;
        RxWorkItem.IfWorkItem.BindingHandle = Xsk->Rx.Xdp.IfHandle;

        XdpIfQueueWorkItem(&RxWorkItem.IfWorkItem);
    } else {
        RxWorkItem.CompletionStatus = STATUS_SUCCESS;
        KeSetEvent(&RxWorkItem.CompletionEvent, 0, FALSE);
    }

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    KeWaitForSingleObject(&TxWorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(TxWorkItem.CompletionStatus)) {
        Status = TxWorkItem.CompletionStatus;
    }

    KeWaitForSingleObject(&RxWorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(RxWorkItem.CompletionStatus)) {
        Status = RxWorkItem.CompletionStatus;
    }

Exit:

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    //
    // Once we committed to XSK activation, any failure to activate implies the
    // socket is already in a terminal state.
    //
    if (ActivateIfCommitted) {
        ASSERT(NT_SUCCESS(Status) || Xsk->State > XskActive);
    }

    if (ActivateIfInitiated) {
        //
        // N.B. XSK synchronization allows the socket to be closed and state set
        // to XskClosing while activation is in progress. This is OK as socket
        // cleanup will happen after all file object references are released and
        // we currently are holding a reference to service this IOCTL. Do not
        // overwrite the state in this case.
        //

        if (Xsk->State == XskActivating) {
            if (NT_SUCCESS(Status)) {
                Xsk->State = XskActive;
            } else {
                Xsk->State = XskBound;
            }
        }
    }

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    TraceInfo(TRACE_XSK, "Xsk=%p Flags=%x Status=%!STATUS!", Xsk, Activate.Flags, Status);

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptGetStatistics(
    _In_ XSK *Xsk,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XSK_STATISTICS *Statistics;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(XSK_STATISTICS)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Statistics = (XSK_STATISTICS*)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Statistics, sizeof(*Statistics));

    *Statistics = Xsk->Statistics;

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*Statistics);

Exit:

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
VOID
XskFillRingInfo(
    _In_ XSK_KERNEL_RING *Ring,
    _Out_ XSK_RING_INFO *Info
    )
{
    ASSERT(Ring->Mdl != NULL);
    ASSERT(Ring->Shared != NULL);
    ASSERT(Ring->Size != 0);
    ASSERT(Ring->UserVa != NULL);

    Info->Ring = Ring->UserVa;
    Info->DescriptorsOffset = sizeof(XSK_SHARED_RING);
    Info->ProducerIndexOffset = FIELD_OFFSET(XSK_SHARED_RING, ProducerIndex);
    Info->ConsumerIndexOffset = FIELD_OFFSET(XSK_SHARED_RING, ConsumerIndex);
    Info->FlagsOffset = FIELD_OFFSET(XSK_SHARED_RING, Flags);
    Info->Size = Ring->Size;
    Info->ElementStride = Ring->ElementStride;
}

static
NTSTATUS
XskSockoptGetRingInfo(
    _In_ XSK *Xsk,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;
    XSK_RING_INFO_SET *InfoSet = Irp->AssociatedIrp.SystemBuffer;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(XSK_RING_INFO_SET)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    RtlZeroMemory(InfoSet, sizeof(*InfoSet));

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    IsLockHeld = TRUE;

    if (Xsk->State == XskClosing) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    if (Xsk->Rx.Ring.Size != 0) {
        XskFillRingInfo(&Xsk->Rx.Ring, &InfoSet->Rx);
    }
    if (Xsk->Rx.FillRing.Size != 0) {
        XskFillRingInfo(&Xsk->Rx.FillRing, &InfoSet->Fill);
    }
    if (Xsk->Tx.Ring.Size != 0) {
        XskFillRingInfo(&Xsk->Tx.Ring, &InfoSet->Tx);
    }
    if (Xsk->Tx.CompletionRing.Size != 0) {
        XskFillRingInfo(&Xsk->Tx.CompletionRing, &InfoSet->Completion);
    }

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*InfoSet);

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptSetUmem(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status;
    CONST VOID *SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    UMEM *Umem = NULL;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptInputBuffer = Sockopt->InputBuffer;
    SockoptInputBufferLength = Sockopt->InputBufferLength;

    if (SockoptInputBufferLength < sizeof(XSK_UMEM_REG)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Umem = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Umem), POOLTAG_UMEM);
    if (Umem == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    XdpInitializeReferenceCount(&Umem->ReferenceCount);

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead(
                (VOID*)SockoptInputBuffer, SockoptInputBufferLength,
                PROBE_ALIGNMENT(XSK_UMEM_REG));
        }
        RtlCopyVolatileMemory(&Umem->Reg, SockoptInputBuffer, sizeof(XSK_UMEM_REG));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    if (Umem->Reg.TotalSize == 0 || Umem->Reg.TotalSize > MAXULONG) {
        // TODO: support up to MAXUINT64?
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    if (Umem->Reg.Headroom > MAXUINT16 ||
        Umem->Reg.Headroom > Umem->Reg.ChunkSize ||
        Umem->Reg.ChunkSize > Umem->Reg.TotalSize ||
        Umem->Reg.ChunkSize == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // If support is needed for kernel mode AF_XDP sockets, UMEM MDL setup
    // needs more thought.
    //
    ASSERT(RequestorMode == UserMode);

    Umem->Mapping.Mdl =
        IoAllocateMdl(
            Umem->Reg.Address,
            (ULONG)Umem->Reg.TotalSize,
            FALSE, // SecondaryBuffer
            FALSE, // ChargeQuota
            NULL); // Irp
    if (Umem->Mapping.Mdl == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    __try {
        MmProbeAndLockPages(Umem->Mapping.Mdl, RequestorMode, IoWriteAccess);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    //
    // MmGetSystemAddressForMdlSafe and MmMapLockedPagesSpecifyCache do not
    // preserve large pages in system address mappings. Use the reserved MDL
    // mapping routines to ensure large pages are propagated into the kernel.
    // Note that the reserved mapping allocates the mapping size based on best-
    // case page-aligned buffers, so account for the MDL offset, too.
    //
    Umem->ReservedMapping =
        MmAllocateMappingAddress(
            BYTE_OFFSET(Umem->Reg.Address) + Umem->Reg.TotalSize, POOLTAG_UMEM);
    if (Umem->ReservedMapping == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Umem->Mapping.SystemAddress =
        MmMapLockedPagesWithReservedMapping(
            Umem->ReservedMapping, POOLTAG_UMEM, Umem->Mapping.Mdl, MmCached);
    if (Umem->Mapping.SystemAddress == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (Umem->Reg.TotalSize % Umem->Reg.ChunkSize != 0) {
        //
        // The final chunk is truncated, which might be required for alignment
        // reasons. Ignore the final chunk.
        //
        ASSERT(Umem->Reg.TotalSize > Umem->Reg.ChunkSize);
        Umem->Reg.TotalSize -= (Umem->Reg.TotalSize % Umem->Reg.ChunkSize);
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    IsLockHeld = TRUE;

    if (Xsk->State != XskUnbound) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    if (Xsk->Umem != NULL) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    TraceInfo(
        TRACE_XSK, "Xsk=%p Set Umem=%p TotalSize=%llu ChunkSize=%llu Headroom=%u",
        Xsk, Umem, Umem->Reg.TotalSize, Umem->Reg.ChunkSize, Umem->Reg.Headroom);

    Status = STATUS_SUCCESS;
    Xsk->Umem = Umem;
    Umem = NULL;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }
    if (Umem != NULL) {
        XskDereferenceUmem(Umem);
    }

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptSetRingSize(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status;
    CONST VOID *SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    XSK_SHARED_RING *Shared = NULL;
    MDL *Mdl = NULL;
    VOID *UserVa = NULL;
    XSK_KERNEL_RING *Ring = NULL;
    UINT32 NumDescriptors;
    ULONG DescriptorSize;
    ULONG AllocationSize;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptInputBuffer = Sockopt->InputBuffer;
    SockoptInputBufferLength = Sockopt->InputBufferLength;

    if (SockoptInputBufferLength < sizeof(UINT32)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)SockoptInputBuffer, SockoptInputBufferLength, PROBE_ALIGNMENT(UINT32));
        }
        NumDescriptors = ReadUInt32NoFence(SockoptInputBuffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    if (NumDescriptors > MAXUINT32 || !RTL_IS_POWER_OF_TWO(NumDescriptors)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    switch (Sockopt->Option) {
    case XSK_SOCKOPT_RX_RING_SIZE:
    case XSK_SOCKOPT_TX_RING_SIZE:
        DescriptorSize = sizeof(XSK_FRAME_DESCRIPTOR);
        break;
    case XSK_SOCKOPT_RX_FILL_RING_SIZE:
    case XSK_SOCKOPT_TX_COMPLETION_RING_SIZE:
        DescriptorSize = sizeof(UINT64);
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = RtlULongMult(NumDescriptors, DescriptorSize, &AllocationSize);
    if (Status != STATUS_SUCCESS) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Status = RtlULongAdd(AllocationSize, sizeof(*Shared), &AllocationSize);
    if (Status != STATUS_SUCCESS) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Pool allocations mapped into user space must consume entire pages.
    // TODO: Group all XSK rings into a single allocation?
    //
    AllocationSize = RTL_NUM_ALIGN_UP(AllocationSize, PAGE_SIZE);
    if (AllocationSize < PAGE_SIZE) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Shared = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, POOLTAG_RING);
    if (Shared == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Mdl =
        IoAllocateMdl(
            Shared,
            AllocationSize,
            FALSE, // SecondaryBuffer
            FALSE, // ChargeQuota
            NULL); // Irp
    if (Mdl == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    __try {
        UserVa =
            MmMapLockedPagesSpecifyCache(
                Mdl,
                RequestorMode,
                MmCached,
                NULL, // RequestedAddress
                FALSE,// BugCheckOnFailure
                NormalPagePriority | MdlMappingNoExecute);
        if (UserVa == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    IsLockHeld = TRUE;

    if (Xsk->State >= XskActivating) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    switch (Sockopt->Option) {
    case XSK_SOCKOPT_RX_RING_SIZE:
        Ring = &Xsk->Rx.Ring;
        break;
    case XSK_SOCKOPT_RX_FILL_RING_SIZE:
        Ring = &Xsk->Rx.FillRing;
        break;
    case XSK_SOCKOPT_TX_RING_SIZE:
        Ring = &Xsk->Tx.Ring;
        Shared->Flags = XSK_RING_FLAG_NEED_POKE;
        break;
    case XSK_SOCKOPT_TX_COMPLETION_RING_SIZE:
        Ring = &Xsk->Tx.CompletionRing;
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ASSERT(
        (Ring->Size != 0 && Ring->Mdl != NULL && Ring->Shared != NULL) ||
        (Ring->Size == 0 && Ring->Mdl == NULL && Ring->Shared == NULL));

    if (Ring->Size != 0) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    TraceInfo(
        TRACE_XSK, "Xsk=%p Set ring Type=%u Size=%u",
        Xsk, Sockopt->Option, NumDescriptors);

    Status = STATUS_SUCCESS;

    Ring->Shared = Shared;
    Ring->Mdl = Mdl;
    Ring->UserVa = UserVa;
    Ring->Size = NumDescriptors;
    Ring->Mask = NumDescriptors - 1;
    Ring->ElementStride = DescriptorSize;
    Ring->OwningProcess = PsGetCurrentProcess();
    Ring->IdealProcessor = INVALID_PROCESSOR_INDEX;
    ObReferenceObject(Ring->OwningProcess);

    Shared = NULL;
    Mdl = NULL;
    UserVa = NULL;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }
    if (UserVa != NULL) {
        MmUnmapLockedPages(UserVa, Mdl);
    }
    if (Mdl != NULL) {
        IoFreeMdl(Mdl);
    }
    if (Shared != NULL) {
        ExFreePoolWithTag(Shared, POOLTAG_RING);
    }

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptGetHookId(
    _In_ XSK *Xsk,
    _In_ UINT32 Option,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_HOOK_ID *HookId = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*HookId)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    IsLockHeld = TRUE;

    switch (Option) {
    case XSK_SOCKOPT_RX_HOOK_ID:
        *HookId = Xsk->Rx.Xdp.HookId;
        break;

    case XSK_SOCKOPT_TX_HOOK_ID:
        *HookId = Xsk->Tx.Xdp.HookId;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*HookId);

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptSetHookId(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status;
    CONST VOID *SockoptIn;
    UINT32 SockoptInSize;
    XDP_HOOK_ID HookId;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptIn = Sockopt->InputBuffer;
    SockoptInSize = Sockopt->InputBufferLength;

    if (SockoptInSize < sizeof(HookId)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)SockoptIn, SockoptInSize, PROBE_ALIGNMENT(XDP_HOOK_ID));
        }
        RtlCopyVolatileMemory(&HookId, SockoptIn, sizeof(XDP_HOOK_ID));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    IsLockHeld = TRUE;

    if (Xsk->State != XskUnbound) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    switch (Sockopt->Option) {
    case XSK_SOCKOPT_RX_HOOK_ID:
        TraceInfo(
            TRACE_XSK,
            "Xsk=%p Set XSK_SOCKOPT_RX_HOOK_ID Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
            Xsk, HookId.Layer, HookId.Direction, HookId.SubLayer);
        Xsk->Rx.Xdp.HookId = HookId;
        break;

    case XSK_SOCKOPT_TX_HOOK_ID:
        TraceInfo(
            TRACE_XSK,
            "Xsk=%p Set XSK_SOCKOPT_TX_HOOK_ID Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
            Xsk, HookId.Layer, HookId.Direction, HookId.SubLayer);
        Xsk->Tx.Xdp.HookId = HookId;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptGetError(
    _In_ XSK *Xsk,
    _In_ UINT32 Option,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XSK_ERROR *XskError = Irp->AssociatedIrp.SystemBuffer;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*XskError)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    switch (Option) {
    case XSK_SOCKOPT_RX_ERROR:
        *XskError = Xsk->Rx.Ring.Error;
        break;

    case XSK_SOCKOPT_RX_FILL_ERROR:
        *XskError = Xsk->Rx.FillRing.Error;
        break;

    case XSK_SOCKOPT_TX_ERROR:
        *XskError = Xsk->Tx.Ring.Error;
        break;

    case XSK_SOCKOPT_TX_COMPLETION_ERROR:
        *XskError = Xsk->Tx.CompletionRing.Error;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*XskError);

Exit:

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
UINT32
XskQueryReadyIo(
    _In_ XSK* Xsk,
    _In_ UINT32 InFlags
    )
{
    UINT32 SatisfiedFlags = 0;

    if (InFlags & XSK_NOTIFY_FLAG_WAIT_TX && XskRingConsPeek(&Xsk->Tx.CompletionRing, 1) > 0) {
        SatisfiedFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
    }
    if (InFlags & XSK_NOTIFY_FLAG_WAIT_RX && XskRingConsPeek(&Xsk->Rx.Ring, 1) > 0) {
        SatisfiedFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
    }

    return SatisfiedFlags;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
BOOLEAN
XskPollInvoke(
    _In_ XSK *Xsk,
    _In_ UINT32 RxQuota,
    _In_ UINT32 TxQuota
    )
{
    BOOLEAN MoreData = FALSE;

    ASSERT(Xsk->Rx.Xdp.PollHandle != NULL || Xsk->Tx.Xdp.PollHandle != NULL);

    if (Xsk->Rx.Xdp.PollHandle == Xsk->Tx.Xdp.PollHandle) {
        //
        // Typically the RX and TX poll contexts are the same.
        //
        MoreData = XdpPollInvoke(Xsk->Rx.Xdp.PollHandle, RxQuota, TxQuota);
    } else {
        if (Xsk->Rx.Xdp.PollHandle != NULL) {
            MoreData |= XdpPollInvoke(Xsk->Rx.Xdp.PollHandle, RxQuota, TxQuota);
        }
        if (Xsk->Tx.Xdp.PollHandle != NULL) {
            MoreData |= XdpPollInvoke(Xsk->Tx.Xdp.PollHandle, RxQuota, TxQuota);
        }
    }

    return MoreData;
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
VOID
XskPollSetNotifications(
    _In_ XSK *Xsk,
    _In_ BOOLEAN EnableNotifications
    )
{
    ASSERT(Xsk->Rx.Xdp.PollHandle != NULL || Xsk->Tx.Xdp.PollHandle != NULL);

    if (Xsk->Rx.Xdp.PollHandle == Xsk->Tx.Xdp.PollHandle) {
        //
        // Typically the RX and TX poll contexts are the same.
        //
        XdpPollSetNotifications(Xsk->Rx.Xdp.PollHandle, EnableNotifications);
    } else {
        if (Xsk->Rx.Xdp.PollHandle != NULL) {
            XdpPollSetNotifications(Xsk->Rx.Xdp.PollHandle, EnableNotifications);
        }
        if (Xsk->Tx.Xdp.PollHandle != NULL) {
            XdpPollSetNotifications(Xsk->Tx.Xdp.PollHandle, EnableNotifications);
        }
    }
}

static
_Requires_exclusive_lock_held_(&Xsk->PollLock)
NTSTATUS
XskPollSocket(
    _In_ XSK *Xsk,
    _In_ UINT32 Flags,
    _In_ UINT32 TimeoutMs
    )
{
    NTSTATUS Status;
    BOOLEAN MoreData;
    UINT32 WaitFlags;
    BOOLEAN NotificationsArmed = FALSE;
    UINT64 DueTime = 0;
    UINT64 CurrentTime;
    LARGE_INTEGER WaitTime;
    LARGE_INTEGER *WaitTimePtr = NULL;

    WaitFlags = Flags & (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX);

    if (TimeoutMs != INFINITE) {
        WaitTimePtr = &WaitTime;
        DueTime = KeQueryInterruptTime();
        DueTime += RTL_MILLISEC_TO_100NANOSEC(TimeoutMs);
    }

    while (TRUE) {
        UINT32 RxQuota = 256;
        UINT32 TxQuota = 256;

        if (ReadULongNoFence(&Xsk->PollWaiters) > 0) {
            //
            // Give the control path a chance to acquire the poll lock.
            //
            RtlReleasePushLockExclusive(&Xsk->PollLock);
            RtlAcquirePushLockExclusive(&Xsk->PollLock);

            if (Xsk->PollMode != XSK_POLL_MODE_SOCKET ||
                (Xsk->Rx.Xdp.PollHandle == NULL && Xsk->Tx.Xdp.PollHandle == NULL)) {
                return STATUS_SUCCESS;
            }
        }

        //
        // TODO: Optimize RX and TX quotas.
        // TODO: Optimize common case where RX and TX share a poll handle.
        //
        if (Xsk->Rx.Xdp.PollHandle != NULL) {
            RxQuota = XskRingConsPeek(&Xsk->Rx.FillRing, RxQuota);
            RxQuota = XskRingProdReserve(&Xsk->Rx.Ring, RxQuota);
        }
        if (Xsk->Tx.Xdp.PollHandle != NULL) {
            TxQuota = XskRingConsPeek(&Xsk->Tx.Ring, TxQuota);
            TxQuota = XskRingProdReserve(&Xsk->Tx.CompletionRing, TxQuota);
        }

        MoreData = XskPollInvoke(Xsk, RxQuota, TxQuota);

        //
        // TODO: Optimize return conditions: should we try to completely fill
        // all buffers, or return as soon as a single buffer is available, or
        // something in between?
        //
        if (!WaitFlags || XskQueryReadyIo(Xsk, WaitFlags)) {
            return STATUS_SUCCESS;
        }

        //
        // Check if the wait interval has timed out.
        // Review: should we use a higher precision time source?
        //
        if (TimeoutMs != INFINITE) {
            CurrentTime = KeQueryInterruptTime();
            if (CurrentTime >= DueTime) {
                return STATUS_TIMEOUT;
            }

            WaitTime.QuadPart = -(LONG64)(DueTime - CurrentTime);
        }

        //
        // If no data is available, attempt to arm notifications and enter
        // the waiting state.
        //
        if (!MoreData && !Xsk->PollBusy) {
            if (!NotificationsArmed) {
                XskPollSetNotifications(Xsk, TRUE);
                NotificationsArmed = TRUE;
            } else {
                Status =
                    KeWaitForSingleObject(
                        &Xsk->PollRequested, UserRequest, UserMode, FALSE, WaitTimePtr);
                if (Status != STATUS_SUCCESS) {
                    return Status;
                }
                MoreData = TRUE;
            }
        }

        if (MoreData) {
            NotificationsArmed = FALSE;
        }
    }
}

static
NTSTATUS
XskSockoptSetPollMode(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status;
    CONST VOID *SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    XSK_POLL_MODE PollMode;

    TraceEnter(TRACE_XSK, "Xsk=%p", Xsk);

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptInputBuffer = Sockopt->InputBuffer;
    SockoptInputBufferLength = Sockopt->InputBufferLength;

    if (SockoptInputBufferLength < sizeof(XSK_POLL_MODE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead(
                (VOID*)SockoptInputBuffer, SockoptInputBufferLength, PROBE_ALIGNMENT(XSK_POLL_MODE));
        }
        RtlCopyVolatileMemory(&PollMode, SockoptInputBuffer, sizeof(XSK_POLL_MODE));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    XskAcquirePollLock(Xsk);
    Status = XskSetPollMode(Xsk, PollMode);
    XskReleasePollLock(Xsk);

    if (NT_SUCCESS(Status)) {
        TraceInfo(TRACE_XSK, "Xsk=%p Set poll mode PollMode=%u", Xsk, PollMode);
    }

Exit:

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskIrpGetSockopt(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK *Xsk;
    UINT32 Option = 0;

    Xsk = IrpSp->FileObject->FsContext;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(Option)) {
        return STATUS_INVALID_PARAMETER;
    }

    Option = *(UINT32 *)Irp->AssociatedIrp.SystemBuffer;

    switch (Option) {
    case XSK_SOCKOPT_RING_INFO:
        Status = XskSockoptGetRingInfo(Xsk, Irp, IrpSp);
        break;
    case XSK_SOCKOPT_STATISTICS:
        Status = XskSockoptGetStatistics(Xsk, Irp, IrpSp);
        break;
    case XSK_SOCKOPT_RX_HOOK_ID:
    case XSK_SOCKOPT_TX_HOOK_ID:
        Status = XskSockoptGetHookId(Xsk, Option, Irp, IrpSp);
        break;
    case XSK_SOCKOPT_RX_ERROR:
    case XSK_SOCKOPT_RX_FILL_ERROR:
    case XSK_SOCKOPT_TX_ERROR:
    case XSK_SOCKOPT_TX_COMPLETION_ERROR:
        Status = XskSockoptGetError(Xsk, Option, Irp, IrpSp);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceInfo(TRACE_XSK, "Xsk=%p Option=%u Status=%!STATUS!", Xsk, Option, Status);

    return Status;
}

static
_Success_(NT_SUCCESS(IoStatus->Status))
VOID
XskGetIdealProcessor(
    _In_ XSK *Xsk,
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ VOID *OutputBuffer,
    _In_ UINT32 OutputBufferLength,
    _Out_ IO_STATUS_BLOCK *IoStatus
    )
{
    UINT32 ProcIndex;
    PROCESSOR_NUMBER ProcNumber;

    //
    // N.B. This function must be called within a try/catch block.
    //

    if (ReadUInt32Acquire((UINT32 *)&Xsk->State) < XskActive || Ring->Size == 0) {
        IoStatus->Status = STATUS_INVALID_DEVICE_STATE;
        return;
    }

    if (OutputBufferLength < sizeof(PROCESSOR_NUMBER)) {
        IoStatus->Status = STATUS_BUFFER_TOO_SMALL;
        return;
    }

    if (PreviousMode != KernelMode) {
        #pragma prefast(suppress:6001) // Using uninitialized memory '*OutputBuffer'
        ProbeForWrite(
            OutputBuffer, sizeof(PROCESSOR_NUMBER), PROBE_ALIGNMENT(PROCESSOR_NUMBER));
    }

    //
    // Reset the affinity flag before reading the affinity value.
    //
    InterlockedAnd((LONG *)&Ring->Shared->Flags, ~XSK_RING_FLAG_AFFINITY_CHANGED);

    ProcIndex = ReadUInt32NoFence(&Ring->IdealProcessor);
    if (ProcIndex == INVALID_PROCESSOR_INDEX) {
        IoStatus->Status = STATUS_DEVICE_NOT_READY;
        return;
    }

    NT_VERIFY(NT_SUCCESS(KeGetProcessorNumberFromIndex(ProcIndex, &ProcNumber)));

    *(PROCESSOR_NUMBER *)OutputBuffer = ProcNumber;
    IoStatus->Information = sizeof(PROCESSOR_NUMBER);
    IoStatus->Status = STATUS_SUCCESS;
}

#pragma warning(push)
#pragma warning(disable:6101) // We don't set OutputBuffer in some paths
static
BOOLEAN
XskFastGetSockopt(
    _In_ XDP_FILE_OBJECT_HEADER *FileObjectHeader,
    _In_opt_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength,
    _Out_opt_ VOID *OutputBuffer,
    _In_ UINT32 OutputBufferLength,
    _Out_ IO_STATUS_BLOCK *IoStatus
    )
{
    XSK *Xsk = (XSK *)FileObjectHeader;
    CONST KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UINT32 Option = 0;

    __try {
        #pragma prefast(push)
        #pragma prefast(disable:6011) // Dereferencing NULL pointer is safe within try/catch.
        #pragma prefast(disable:6387) // Dereferencing NULL pointer is safe within try/catch.

        if (InputBufferLength < sizeof(Option)) {
            IoStatus->Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (PreviousMode != KernelMode) {
            ProbeForRead(
                (VOID *)InputBuffer, InputBufferLength, PROBE_ALIGNMENT(UINT32));
        }

        Option = ReadUInt32NoFence(InputBuffer);

        switch (Option) {
        case XSK_SOCKOPT_RX_PROCESSOR_AFFINITY:
            XskGetIdealProcessor(
                Xsk, &Xsk->Rx.Ring, PreviousMode, OutputBuffer, OutputBufferLength, IoStatus);
            break;

        case XSK_SOCKOPT_TX_PROCESSOR_AFFINITY:
            XskGetIdealProcessor(
                Xsk, &Xsk->Tx.Ring, PreviousMode, OutputBuffer, OutputBufferLength, IoStatus);
            break;

        default:
            //
            // Fall back to the IRP-based socket option path.
            //
            return FALSE;
        }

        #pragma prefast(pop)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        IoStatus->Status = GetExceptionCode();
        goto Exit;
    }

Exit:

    return TRUE;
}
#pragma warning(pop)

static
NTSTATUS
XskIrpSetSockopt(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK *Xsk;
    XSK_SET_SOCKOPT_IN *Sockopt = NULL;

    Xsk = IrpSp->FileObject->FsContext;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(XSK_SET_SOCKOPT_IN)) {
        return STATUS_INVALID_PARAMETER;
    }

    Sockopt = Irp->AssociatedIrp.SystemBuffer;

    switch (Sockopt->Option) {
    case XSK_SOCKOPT_UMEM_REG:
        Status = XskSockoptSetUmem(Xsk, Sockopt, Irp->RequestorMode);
        break;
    case XSK_SOCKOPT_TX_RING_SIZE:
    case XSK_SOCKOPT_RX_RING_SIZE:
    case XSK_SOCKOPT_RX_FILL_RING_SIZE:
    case XSK_SOCKOPT_TX_COMPLETION_RING_SIZE:
        Status = XskSockoptSetRingSize(Xsk, Sockopt, Irp->RequestorMode);
        break;
    case XSK_SOCKOPT_RX_HOOK_ID:
    case XSK_SOCKOPT_TX_HOOK_ID:
        Status = XskSockoptSetHookId(Xsk, Sockopt, Irp->RequestorMode);
        break;
#if !defined(XDP_OFFICIAL_BUILD)
    case XSK_SOCKOPT_POLL_MODE:
        Status = XskSockoptSetPollMode(Xsk, Sockopt, Irp->RequestorMode);
        break;
#endif // !defined(XDP_OFFICIAL_BUILD)
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceInfo(
        TRACE_XSK, "Xsk=%p Option=%u Status=%!STATUS! Value=%!HEXDUMP!",
        Xsk, Sockopt->Option, Status, WppHexDump(Sockopt->InputBuffer, Sockopt->InputBufferLength));

    return Status;
}

static
NTSTATUS
XskNotifyValidateParams(
    _In_ XSK *Xsk,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_ PUINT32 TimeoutMilliseconds,
    _Out_ PUINT32 InFlags
    )
{
    if (Xsk->State != XskActive){
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (InputBufferLength < sizeof(XSK_NOTIFY_IN)) {
        return STATUS_INVALID_PARAMETER;
    }

    __try {
        ASSERT(InputBuffer);
        if (ExGetPreviousMode() != KernelMode) {
            ProbeForRead(
                (VOID*)InputBuffer, InputBufferLength, PROBE_ALIGNMENT(XSK_NOTIFY_IN));
        }

        *InFlags = ReadUInt32NoFence((UINT32 *)&((XSK_NOTIFY_IN *)InputBuffer)->Flags);
        *TimeoutMilliseconds =
            ReadUInt32NoFence(&((XSK_NOTIFY_IN *)InputBuffer)->WaitTimeoutMilliseconds);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }

    if (*InFlags == 0 || *InFlags &
            ~(XSK_NOTIFY_FLAG_POKE_RX | XSK_NOTIFY_FLAG_POKE_TX | XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX)) {
        return STATUS_INVALID_PARAMETER;
    }

    if ((*InFlags & (XSK_NOTIFY_FLAG_POKE_RX | XSK_NOTIFY_FLAG_WAIT_RX) && Xsk->Rx.Ring.Size == 0) ||
        (*InFlags & (XSK_NOTIFY_FLAG_POKE_TX | XSK_NOTIFY_FLAG_WAIT_TX) && Xsk->Tx.Ring.Size == 0)) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
XskPoke(
    _In_ XSK *Xsk,
    _In_ UINT32 Flags,
    _In_ UINT32 TimeoutMs
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    EventWriteXskNotifyPokeStart(&MICROSOFT_XDP_PROVIDER, Xsk, Flags);

    RtlAcquirePushLockExclusive(&Xsk->PollLock);

    if (Xsk->PollMode == XSK_POLL_MODE_SOCKET &&
        (Xsk->Rx.Xdp.PollHandle != NULL || Xsk->Tx.Xdp.PollHandle != NULL)) {
        //
        // Socket polling mode is active, so poll the interfaces synchronously.
        //
        Status = XskPollSocket(Xsk, Flags, TimeoutMs);
    } else {
        //
        // Notify the underlying interfaces that data is available.
        //

        if (Flags & XSK_NOTIFY_FLAG_POKE_RX) {
            ASSERT(Xsk->Rx.Ring.Size > 0);
            ASSERT(Xsk->Rx.FillRing.Size > 0);
            //
            // TODO: Driver poke routine for zero copy RX.
            //
            ASSERT(Status == STATUS_SUCCESS);
        }

        if (Flags & XSK_NOTIFY_FLAG_POKE_TX) {
            XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_TX;
            //
            // Before invoking the poke routine, atomically clear the need poke
            // flag on the TX ring. The poke routine is required to execute a
            // FlushTransmit, which guarantees the need poke flag can be set.
            //
            InterlockedAnd((LONG *)&Xsk->Tx.Ring.Shared->Flags, ~XSK_RING_FLAG_NEED_POKE);

            if (Xsk->Tx.Xdp.Flags.QueueActive) {
                XdpTxQueueInvokeInterfaceNotify(Xsk->Tx.Xdp.Queue, NotifyFlags);
            } else {
                Status = STATUS_DEVICE_REMOVED;
            }
        }
    }

    RtlReleasePushLockExclusive(&Xsk->PollLock);

    EventWriteXskNotifyPokeStop(&MICROSOFT_XDP_PROVIDER, Xsk, Flags);

    return Status;
}

static DRIVER_CANCEL XskCancelNotify;

static
_Use_decl_annotations_
VOID
XskCancelNotify(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp
    )
{
    XSK *Xsk;
    IO_STACK_LOCATION *IrpSp;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(DISPATCH_LEVEL);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Xsk = IrpSp->FileObject->FsContext;

    KeAcquireSpinLockAtDpcLevel(&Xsk->Lock);

    //
    // If the data path hasn't already dropped the IRP, reset the wait state
    // here.
    //

    if (Xsk->IoWaitIrp == Irp) {
        Xsk->IoWaitFlags = 0;
        Xsk->IoWaitIrp = NULL;
    }

    KeReleaseSpinLock(&Xsk->Lock, Irp->CancelIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;

    EventWriteXskNotifyAsyncComplete(&MICROSOFT_XDP_PROVIDER, Xsk, Irp, Irp->IoStatus.Status);
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static
_Success_(return == STATUS_SUCCESS)
NTSTATUS
XskNotify(
    _In_ XSK *Xsk,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_ ULONG_PTR *Information,
    _Inout_opt_ IRP *Irp
    )
{
    UINT32 TimeoutMilliseconds;
    UINT32 ReadyFlags;
    UINT32 InFlags;
    UINT32 OutFlags = 0;
    KIRQL OldIrql;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    XSK_IO_WAIT_FLAGS InternalFlags;
    CONST UINT32 WaitMask = (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX);

    Status =
        XskNotifyValidateParams(
            Xsk, InputBuffer, InputBufferLength, &TimeoutMilliseconds, &InFlags);
    if (Status != STATUS_SUCCESS) {
        TraceError(TRACE_XSK, "Xsk=%p Notify failed: Invalid params", Xsk);
        goto Exit;
    }

    EventWriteXskNotifyStart(&MICROSOFT_XDP_PROVIDER, Xsk, Irp, InFlags, TimeoutMilliseconds);

    //
    // Snap the XSK notification state before performing the poke and/or wait.
    //
    InternalFlags = ReadULongAcquire((ULONG *)&Xsk->IoWaitInternalFlags);

    if (InFlags & (XSK_NOTIFY_FLAG_POKE_RX | XSK_NOTIFY_FLAG_POKE_TX)) {
        Status = XskPoke(Xsk, InFlags, TimeoutMilliseconds);
        if (Status != STATUS_SUCCESS) {
            TraceError(
                TRACE_XSK, "Xsk=%p Notify failed: Poke failed Status=%!STATUS!",
                Xsk, Status);
            goto Exit;
        }
    }

    if ((InFlags & (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX)) == 0) {
        //
        // Not interested in waiting for IO.
        //
        ASSERT(Status == STATUS_SUCCESS);
        goto Exit;
    }

    //
    // Opportunistic check for ready IO to avoid setting up a wait context.
    //
    ReadyFlags = XskQueryReadyIo(Xsk, InFlags);
    if (ReadyFlags != 0) {
        OutFlags |= XskWaitInFlagsToOutFlags(ReadyFlags);
        ASSERT(Status == STATUS_SUCCESS);
        goto Exit;
    }

    //
    // Set up the wait context.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if (Xsk->State != XskActive) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        TraceInfo(TRACE_XSK, "Xsk=%p Notify wait failed: XSK not bound", Xsk);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    if (Xsk->IoWaitFlags != 0) {
        //
        // There is currently a wait active. Only a single wait is allowed.
        //
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        TraceError(TRACE_XSK, "Xsk=%p Notify wait failed: Wait already active", Xsk);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Xsk->IoWaitFlags = InFlags & WaitMask;
    if (Irp != NULL) {
        Xsk->IoWaitIrp = Irp;

        //
        // Mark the IRP as pending prior to enabling cancellation; once we mark
        // an IRP as pending, we must return STATUS_PENDING to the IO manager.
        //
        IoMarkIrpPending(Irp);
        Status = STATUS_PENDING;

        //
        // Enable cancellation and synchronize with the IO manager.
        //
        IoSetCancelRoutine(Irp, XskCancelNotify);
        if (Irp->Cancel) {
            if (IoSetCancelRoutine(Irp, NULL) != NULL) {
                //
                // The cancellation routine will not run; cancel the IRP here
                // and bail.
                //
                Xsk->IoWaitIrp = NULL;
                Xsk->IoWaitFlags = 0;
                KeReleaseSpinLock(&Xsk->Lock, OldIrql);
                Irp->IoStatus.Status = STATUS_CANCELLED;
                EventWriteXskNotifyAsyncComplete(
                    &MICROSOFT_XDP_PROVIDER, Xsk, Irp, Irp->IoStatus.Status);
                IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
            } else {
                //
                // The cancellation routine will run; bail.
                //
                KeReleaseSpinLock(&Xsk->Lock, OldIrql);
            }

            goto Exit;
        }
    } else {
        KeClearEvent(&Xsk->IoWaitEvent);
    }
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    //
    // N.B. A memory barrier between the write of the wait event and the read of
    // completed IO is needed in order to prevent a race condition when these
    // operations are re-ordered. The race is between this thread and a thread
    // completing IO. Memory barriers are also needed to protect against a
    // similar race condition when the inverse of these operations are done
    // during IO completion.
    //
    KeMemoryBarrier();

    //
    // Check for ready IO.
    //
    ReadyFlags = XskQueryReadyIo(Xsk, InFlags);
    if (ReadyFlags != 0) {
        XskSignalReadyIo(Xsk, ReadyFlags);
    }

    //
    // If the notification state changed while processing this request, abandon
    // the wait and allow the application to re-examine the shared ring flags.
    //
    if (InternalFlags != Xsk->IoWaitInternalFlags) {
        XskSignalReadyIo(Xsk, InFlags & WaitMask);
    }

    if (Irp == NULL) {
        //
        // Wait for IO.
        //
        Timeout.QuadPart = -1 * RTL_MILLISEC_TO_100NANOSEC(TimeoutMilliseconds);
        Status =
            KeWaitForSingleObject(
                &Xsk->IoWaitEvent, UserRequest, UserMode, FALSE,
                (TimeoutMilliseconds == INFINITE) ? NULL : &Timeout);
    } else {
        ASSERT(Status == STATUS_PENDING);
        goto Exit;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    Xsk->IoWaitFlags = 0;
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    //
    // Re-query ready IO regardless of the wait status.
    //
    ReadyFlags = XskQueryReadyIo(Xsk, InFlags);
    if (ReadyFlags != 0) {
        Status = STATUS_SUCCESS;
        OutFlags |= XskWaitInFlagsToOutFlags(ReadyFlags);
    }

Exit:

    //
    // This IOCTL is assumed to never pend for fast IO, and code elsewhere takes
    // advantage of this assumption.
    //
    ASSERT(Irp != NULL || Status != STATUS_PENDING);

    if (Status != STATUS_PENDING) {
        *Information = OutFlags;
    }

    EventWriteXskNotifyStop(&MICROSOFT_XDP_PROVIDER, Xsk, Irp, OutFlags, Status);

    return Status;
}

#pragma warning(push)
#pragma warning(disable:6101) // We don't set OutputBuffer in some paths
BOOLEAN
XskFastIo(
    _In_ XDP_FILE_OBJECT_HEADER *FileObjectHeader,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ VOID *OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ IO_STATUS_BLOCK *IoStatus
    )
{
    XSK *Xsk = CONTAINING_RECORD(FileObjectHeader, XSK, Header);

    switch (IoControlCode) {
    case IOCTL_XSK_NOTIFY:
        IoStatus->Status =
            XskNotify(Xsk, InputBuffer, InputBufferLength, &IoStatus->Information, NULL);
        return TRUE;

    case IOCTL_XSK_GET_SOCKOPT:
        return
            XskFastGetSockopt(
                FileObjectHeader, InputBuffer, InputBufferLength, OutputBuffer,
                OutputBufferLength, IoStatus);
    }

    return FALSE;
}
#pragma warning(pop)

static
FORCEINLINE
VOID
XskReceiveSingleFrame(
    _In_ XSK *Xsk,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FragmentIndex,
    _In_ UINT32 FillOffset,
    _Inout_ UINT32 *CompletionOffset
    )
{
    XDP_RING *FragmentRing = Xsk->Rx.Xdp.FragmentRing;
    XDP_FRAME *Frame = XdpRingGetElement(Xsk->Rx.Xdp.FrameRing, FrameIndex);
    XDP_FRAME_FRAGMENT *Fragment;
    XDP_BUFFER *Buffer = &Frame->Buffer;
    XDP_BUFFER_VIRTUAL_ADDRESS *Va = XdpGetVirtualAddressExtension(Buffer, &Xsk->Rx.Xdp.VaExtension);
    UCHAR *UmemChunk;
    UINT64 UmemAddress;
    UINT32 UmemOffset;
    UINT32 CopyLength;
    UINT32 RingIndex;

    XSK_FRAME_DESCRIPTOR *XskFrame;
    XSK_BUFFER_DESCRIPTOR *XskBuffer;

    RingIndex =
        (ReadUInt32NoFence(&Xsk->Rx.FillRing.Shared->ConsumerIndex) + FillOffset) &
            Xsk->Rx.FillRing.Mask;
    UmemAddress = *(UINT64 *)XskKernelRingGetElement(&Xsk->Rx.FillRing, RingIndex);

    if (UmemAddress > Xsk->Umem->Reg.TotalSize - Xsk->Umem->Reg.ChunkSize) {
        //
        // Invalid FILL descriptor.
        //
        Xsk->Statistics.RxInvalidDescriptors++;
        STAT_INC(XdpRxQueueGetStats(Xsk->Rx.Xdp.Queue), XskInvalidDescriptors);
        return;
    }

    UmemChunk = Xsk->Umem->Mapping.SystemAddress + UmemAddress;
    UmemOffset = Xsk->Umem->Reg.Headroom;
    CopyLength = min(Buffer->DataLength, Xsk->Umem->Reg.ChunkSize - UmemOffset);

    if (!XskGlobals.RxZeroCopy) {
        RtlCopyMemory(UmemChunk + UmemOffset, Va->VirtualAddress + Buffer->DataOffset, CopyLength);
    }
    if (CopyLength < Buffer->DataLength) {
        //
        // Not enough available space in Umem.
        //
        Xsk->Statistics.RxTruncated++;
        STAT_INC(XdpRxQueueGetStats(Xsk->Rx.Xdp.Queue), XskFramesTruncated);
    } else if (FragmentRing != NULL) {
        Fragment = XdpGetFragmentExtension(Frame, &Xsk->Rx.Xdp.FragmentExtension);

        for (UINT32 Index = 0; Index < Fragment->FragmentBufferCount; Index++) {
            Buffer = XdpRingGetElement(FragmentRing, (FragmentIndex + Index) & FragmentRing->Mask);
            Va = XdpGetVirtualAddressExtension(Buffer, &Xsk->Rx.Xdp.VaExtension);

            UmemOffset += CopyLength;
            CopyLength = min(Buffer->DataLength, Xsk->Umem->Reg.ChunkSize - UmemOffset);

            if (!XskGlobals.RxZeroCopy) {
                RtlCopyMemory(
                    UmemChunk + UmemOffset, Va->VirtualAddress + Buffer->DataOffset, CopyLength);
            }

            if (CopyLength < Buffer->DataLength) {
                //
                // Not enough available space in Umem.
                //
                Xsk->Statistics.RxTruncated++;
                STAT_INC(XdpRxQueueGetStats(Xsk->Rx.Xdp.Queue), XskFramesTruncated);
                break;
            }
        }
    }

    RingIndex =
        (ReadUInt32NoFence(&Xsk->Rx.Ring.Shared->ProducerIndex) + *CompletionOffset) &
            Xsk->Rx.Ring.Mask;
    XskFrame = XskKernelRingGetElement(&Xsk->Rx.Ring, RingIndex);
    XskBuffer = &XskFrame->Buffer;
    XskBuffer->Address.BaseAddress = UmemAddress;
    ASSERT(Xsk->Umem->Reg.Headroom <= MAXUINT16);
    XskBuffer->Address.Offset = (UINT16)Xsk->Umem->Reg.Headroom;
    XskBuffer->Length = UmemOffset - Xsk->Umem->Reg.Headroom + CopyLength;

    ++*CompletionOffset;
}

static
VOID
XskReceiveSubmitBatch(
    _In_ XSK *Xsk,
    _In_ UINT32 BatchCount,
    _In_ UINT32 RxFillConsumed,
    _In_ UINT32 RxProduced
    )
{
    if (RxProduced < BatchCount) {
        //
        // Dropped packets.
        //
        UINT32 Dropped = BatchCount - RxProduced;
        Xsk->Statistics.RxDropped += Dropped;
        STAT_ADD(XdpRxQueueGetStats(Xsk->Rx.Xdp.Queue), XskFramesDropped, Dropped);
    }

    XskRingConsRelease(&Xsk->Rx.FillRing, RxFillConsumed);

    XskKernelRingUpdateIdealProcessor(&Xsk->Rx.Ring);

    if (RxProduced > 0) {
        XskRingProdSubmit(&Xsk->Rx.Ring, RxProduced);

        EventWriteXskRxPostBatch(
            &MICROSOFT_XDP_PROVIDER, Xsk,
            Xsk->Rx.Ring.Shared->ProducerIndex - RxProduced, RxProduced);
        STAT_ADD(XdpRxQueueGetStats(Xsk->Rx.Xdp.Queue), XskFramesDelivered, RxProduced);

        //
        // N.B. See comment in XskNotify.
        //
        KeMemoryBarrier();

        if ((Xsk->IoWaitFlags & XSK_NOTIFY_FLAG_WAIT_RX) &&
            (KeReadStateEvent(&Xsk->IoWaitEvent) == 0 || Xsk->IoWaitIrp != NULL)) {
            XskSignalReadyIo(Xsk, XSK_NOTIFY_FLAG_WAIT_RX);
        }
    }
}

VOID
XskReceive(
    _In_ XDP_REDIRECT_BATCH *Batch
    )
{
    XSK *Xsk = Batch->Target;
    UINT32 ReservedCount;
    UINT32 RxCount = 0;

    if (!Xsk->Rx.Xdp.Flags.DatapathAttached || Xsk->Rx.Xdp.Queue != Batch->RxQueue) {
        goto Exit;
    }

    ReservedCount = XskRingProdReserve(&Xsk->Rx.Ring, Batch->Count);
    ReservedCount = XskRingConsPeek(&Xsk->Rx.FillRing, ReservedCount);

    for (UINT32 FillIndex = 0; FillIndex < ReservedCount; FillIndex++) {
        XskReceiveSingleFrame(
            Xsk, Batch->FrameIndexes[RxCount].FrameIndex,
            Batch->FrameIndexes[RxCount].FragmentIndex, FillIndex, &RxCount);
    }

    XskReceiveSubmitBatch(Xsk, Batch->Count, ReservedCount, RxCount);

Exit:
    return;
}

BOOLEAN
XskReceiveBatchedExclusive(
    _In_ VOID *Target
    )
{
    XSK *Xsk = Target;
    XDP_RING *FrameRing = Xsk->Rx.Xdp.FrameRing;
    XDP_RING *FragmentRing = Xsk->Rx.Xdp.FragmentRing;
    UINT32 BatchCount;
    UINT32 ReservedCount;
    UINT32 RxCount = 0;

    if (!Xsk->Rx.Xdp.Flags.DatapathAttached) {
        return FALSE;
    }

    BatchCount = FrameRing->ProducerIndex - FrameRing->ConsumerIndex;

    ReservedCount = XskRingProdReserve(&Xsk->Rx.Ring, BatchCount);
    ReservedCount = XskRingConsPeek(&Xsk->Rx.FillRing, ReservedCount);

    for (UINT32 Index = 0; Index < BatchCount; Index++) {
        UINT32 FrameIndex = FrameRing->ConsumerIndex & FrameRing->Mask;
        UINT32 FragmentIndex = 0;
        XDP_FRAME *Frame;
        XDP_FRAME_FRAGMENT *Fragment;
        XDP_FRAME_RX_ACTION *RxAction;

        Frame = XdpRingGetElement(FrameRing, FrameIndex);

        RxAction = XdpGetRxActionExtension(Frame, &Xsk->Rx.Xdp.RxActionExtension);
        RxAction->RxAction = XDP_RX_ACTION_DROP;

        if (FragmentRing != NULL) {
            FragmentIndex = FragmentRing->ConsumerIndex;
        }

        if (Index < ReservedCount) {
            XskReceiveSingleFrame(Xsk, FrameIndex, FragmentIndex, Index, &RxCount);
        }

        FrameRing->ConsumerIndex++;

        if (FragmentRing != NULL) {
            Fragment = XdpGetFragmentExtension(Frame, &Xsk->Rx.Xdp.FragmentExtension);
            FragmentRing->ConsumerIndex += Fragment->FragmentBufferCount;
        }
    }

    XskReceiveSubmitBatch(Xsk, BatchCount, ReservedCount, RxCount);

    return TRUE;
}

_Use_decl_annotations_
NTSTATUS
XskIrpDeviceIoControl(
    IRP *Irp,
    IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_XSK_BIND:
        Status = XskIrpBindSocket(Irp, IrpSp);
        break;
    case IOCTL_XSK_ACTIVATE:
        Status = XskIrpActivateSocket(Irp, IrpSp);
        break;
    case IOCTL_XSK_GET_SOCKOPT:
        Status = XskIrpGetSockopt(Irp, IrpSp);
        break;
    case IOCTL_XSK_SET_SOCKOPT:
        Status = XskIrpSetSockopt(Irp, IrpSp);
        break;
    case IOCTL_XSK_NOTIFY_ASYNC:
        Status =
            XskNotify(
                IrpSp->FileObject->FsContext, IrpSp->Parameters.DeviceIoControl.Type3InputBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength,
                &Irp->IoStatus.Information, Irp);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XskRegistryUpdate(
    VOID
    )
{
    DWORD Value;
    NTSTATUS Status;

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XskDisableTxBounce", &Value);
    if (NT_SUCCESS(Status)) {
        XskGlobals.DisableTxBounce = !!Value;
    } else {
        XskGlobals.DisableTxBounce = FALSE;
    }

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XskRxZeroCopy", &Value);
    if (NT_SUCCESS(Status)) {
        XskGlobals.RxZeroCopy = !!Value;
    } else {
        XskGlobals.RxZeroCopy = FALSE;
    }
}

NTSTATUS
XskStart(
    VOID
    )
{
    RtlZeroMemory(&XskGlobals, sizeof(XskGlobals));
    XdpRegWatcherAddClient(XdpRegWatcher, XskRegistryUpdate, &XskRegWatcherEntry);
    return STATUS_SUCCESS;
}

VOID
XskStop(
    VOID
    )
{
    XdpRegWatcherRemoveClient(XdpRegWatcher, &XskRegWatcherEntry);
}
