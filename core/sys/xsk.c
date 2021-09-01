//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "xsk.tmh"
#include <afxdp_helper.h>
#include <afxdp_experimental.h>

typedef enum _XSK_STATE {
    XskUnbound,
    XskBinding,
    XskBound,
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
    // XSK_BUFFER_DESCRIPTOR[] for rx/tx, UINT64[] for fill/completion
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
    VOID *OwningProcess;
    LONG ReferenceCount;
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

typedef struct _XSK_TX_XDP_EXTENSION_FLAGS {
    BOOLEAN VirtualAddress : 1;
    BOOLEAN LogicalAddress : 1;
    BOOLEAN Mdl : 1;
} XSK_TX_XDP_EXTENSION_FLAGS;

typedef struct _XSK_TX_XDP {
    //
    // XDP data path fields.
    //
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    XDP_EXTENSION VaExtension;
    XDP_EXTENSION LaExtension;
    XDP_EXTENSION MdlExtension;
    XSK_TX_XDP_EXTENSION_FLAGS ExtensionFlags;
    BOOLEAN OutOfOrderCompletion;
    UINT32 OutstandingFrames;
    UINT32 MaxBufferLength;
    UINT32 MaxFrameLength;
    XDP_INTERFACE_HANDLE InterfaceQueue;
    XDP_INTERFACE_NOTIFY_QUEUE *InterfaceNotify;
    NDIS_POLL_BACKCHANNEL *PollHandle;
    XDP_TX_QUEUE_DISPATCH ExclusiveDispatch;
#if DBG
    XDP_DBG_QUEUE_EC DbgEc;
#endif

    //
    // Control path fields.
    //
    XDP_BINDING_HANDLE IfHandle;
    XDP_HOOK_ID HookId;
    XDP_TX_QUEUE *Queue;
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
    INT64 ReferenceCount;
    XSK_STATE State;
    UMEM *Umem;
    XSK_RX Rx;
    XSK_TX Tx;
    KSPIN_LOCK Lock;
    UINT32 IoWaitFlags;
    XSK_IO_WAIT_FLAGS IoWaitInternalFlags;
    KEVENT IoWaitEvent;
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

C_ASSERT(XSK_NOTIFY_WAIT_RX == XSK_NOTIFY_WAIT_RESULT_RX_AVAILABLE);
C_ASSERT(XSK_NOTIFY_WAIT_TX == XSK_NOTIFY_WAIT_RESULT_TX_COMP_AVAILABLE);

static
VOID
XskReference(
    _In_ XSK* Xsk
    )
{
    FRE_ASSERT(InterlockedIncrementAcquire64(&Xsk->ReferenceCount) > 1);
}

static
VOID
XskDereference(
    _In_ XSK* Xsk
    )
{
    INT64 NewCount = InterlockedDecrementRelease64(&Xsk->ReferenceCount);
    FRE_ASSERT(NewCount >= 0);
    if (NewCount == 0) {
        ExFreePoolWithTag(Xsk, POOLTAG_XSK);
    }
}

static
VOID
XskSignalReadyIo(
    _In_ XSK *Xsk,
    _In_ UINT32 ReadyFlags
    )
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if ((Xsk->IoWaitFlags & ReadyFlags) != 0) {
        (VOID)KeSetEvent(&Xsk->IoWaitEvent, IO_NETWORK_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);
}

static
UINT32
XskRingProdReserve(
    _Inout_ XSK_KERNEL_RING *Ring,
    _In_ UINT32 Count
    )
{
    UINT32 Available = Ring->Size - (Ring->Shared->ProducerIndex - Ring->Shared->ConsumerIndex);
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
        ReadUInt32Acquire(&Ring->Shared->ProducerIndex) - Ring->Shared->ConsumerIndex;
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

    ChunkIndex = RelativeAddress / Umem->Reg.chunkSize;
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

    ChunkIndex = RelativeAddress / Umem->Reg.chunkSize;
    if (ChunkIndex != (RelativeAddress + Buffer->BufferLength - 1) / Umem->Reg.chunkSize) {
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
            ExAllocatePoolUninitialized(NonPagedPoolNx, Xsk->Umem->Reg.totalSize, POOLTAG_BOUNCE);
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

    ASSERT(Xsk->Umem->Reg.totalSize <= ULONG_MAX);
    Bounce->Mapping.Mdl =
        IoAllocateMdl(
            Bounce->Mapping.SystemAddress, (ULONG)Xsk->Umem->Reg.totalSize, FALSE, FALSE, NULL);
    if (Bounce->Mapping.Mdl == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    MmBuildMdlForNonPagedPool(Bounce->Mapping.Mdl);

    Status =
        RtlSizeTMult(
            Xsk->Umem->Reg.totalSize / Xsk->Umem->Reg.chunkSize, sizeof(SIZE_T),
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
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskFillTx(
    _In_ XSK *Xsk
    )
{
    NTSTATUS Status;
    ULONGLONG Result;
    XSK_BUFFER_DESCRIPTOR *Descriptor;
    UINT32 Count;
    UINT32 BufferCount = 0;
    UINT32 TxIndex;
    UINT32 XskCompletionAvailable;
    UINT32 XskTxAvailable;
    UINT32 XdpTxAvailable;
    XDP_RING *FrameRing = Xsk->Tx.Xdp.FrameRing;

    if (Xsk->State != XskBound) {
        return;
    }

    //
    // The need poke flag is cleared when a poke request is submitted. If no
    // input is available and no packets are outstanding, set the need poke flag
    // and then re-check for input.
    //
    if (Xsk->Tx.Xdp.PollHandle == NULL && XskRingConsPeek(&Xsk->Tx.Ring, 1) == 0 &&
        Xsk->Tx.Xdp.OutstandingFrames == 0) {
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

    if (Xsk->Tx.Xdp.OutOfOrderCompletion) {
        XdpTxAvailable = XdpRingFree(FrameRing);
    } else {
        XdpTxAvailable =
            FrameRing->Mask + 1 - (FrameRing->ProducerIndex - FrameRing->Reserved);
    }

    XskTxAvailable = XskRingConsPeek(&Xsk->Tx.Ring, MAXUINT32);

    XskCompletionAvailable = XskRingProdReserve(&Xsk->Tx.CompletionRing, MAXUINT32);
    if (!NT_VERIFY(XskCompletionAvailable >= Xsk->Tx.Xdp.OutstandingFrames)) {
        //
        // If the above condition does not hold, the XSK TX completion ring is
        // no longer valid. This implies an application programming error.
        //
        XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INVALID_RING);
        return;
    }
    XskCompletionAvailable -= Xsk->Tx.Xdp.OutstandingFrames;

    Count = min(min(XdpTxAvailable, XskTxAvailable), XskCompletionAvailable);

    for (ULONG i = 0; i < Count; i++) {
        XDP_FRAME *Frame;
        XDP_BUFFER *Buffer;
        UINT64 AddressDescriptor, RelativeAddress;
        UMEM_MAPPING *Mapping;

        TxIndex = (Xsk->Tx.Ring.Shared->ConsumerIndex + i) & (Xsk->Tx.Ring.Mask);
        Descriptor = XskKernelRingGetElement(&Xsk->Tx.Ring, TxIndex);

        Frame = XdpRingGetElement(FrameRing, FrameRing->ProducerIndex & FrameRing->Mask);
        Buffer = &Frame->Buffer;

        AddressDescriptor = ReadUInt64NoFence(&Descriptor->address);
        RelativeAddress = XskDescriptorGetAddress(AddressDescriptor);
        Buffer->DataOffset = XskDescriptorGetOffset(AddressDescriptor);
        Buffer->DataLength = (UINT32)ReadNoFence((LONG*)&Descriptor->length);
        Buffer->BufferLength = Buffer->DataLength + Buffer->DataOffset;

        Status = RtlUInt64Add(RelativeAddress, Buffer->DataLength, &Result);
        Status |= RtlUInt64Add(Buffer->DataOffset, Result, &Result);
        if (Result > Xsk->Umem->Reg.totalSize ||
            Buffer->DataLength == 0 ||
            Status != STATUS_SUCCESS) {
            ++Xsk->Statistics.txInvalidDescriptors;
            continue;
        }

        if (Buffer->DataLength > min(Xsk->Tx.Xdp.MaxBufferLength, Xsk->Tx.Xdp.MaxFrameLength)) {
            ++Xsk->Statistics.txInvalidDescriptors;
            continue;
        }

        if (!XskBounceBuffer(Xsk->Umem, &Xsk->Tx.Bounce, Buffer, RelativeAddress, &Mapping)) {
            ++Xsk->Statistics.txInvalidDescriptors;
            continue;
        }

        if (Xsk->Tx.Xdp.ExtensionFlags.VirtualAddress) {
            XDP_BUFFER_VIRTUAL_ADDRESS *Va;
            Va = XdpGetVirtualAddressExtension(Buffer, &Xsk->Tx.Xdp.VaExtension);
            Va->VirtualAddress = &Mapping->SystemAddress[RelativeAddress];
        }
        if (Xsk->Tx.Xdp.ExtensionFlags.LogicalAddress) {
            XDP_BUFFER_LOGICAL_ADDRESS *La;
            La = XdpGetLogicalAddressExtension(Buffer, &Xsk->Tx.Xdp.LaExtension);
            La->LogicalAddress = Mapping->DmaAddress.QuadPart + RelativeAddress;
        }
        if (Xsk->Tx.Xdp.ExtensionFlags.Mdl) {
            XDP_BUFFER_MDL *Mdl;
            Mdl = XdpGetMdlExtension(Buffer, &Xsk->Tx.Xdp.MdlExtension);
            Mdl->Mdl = Mapping->Mdl;
            Mdl->MdlOffset = RelativeAddress;
        }

        FrameRing->ProducerIndex++;
        ++BufferCount;
    }

    XskRingConsRelease(&Xsk->Tx.Ring, Count);

    Xsk->Tx.Xdp.OutstandingFrames += BufferCount;

    //
    // If input was processed, clear the need poke flag.
    //
    if (Xsk->Tx.Xdp.PollHandle == NULL && Xsk->Tx.Xdp.OutstandingFrames > 0 &&
        (Xsk->Tx.Ring.Shared->Flags & XSK_RING_FLAG_NEED_POKE)) {
        InterlockedAnd((LONG *)&Xsk->Tx.Ring.Shared->Flags, ~XSK_RING_FLAG_NEED_POKE);
    }
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
XskFillTxCompletion(
    _In_ XSK *Xsk
    )
{
    XSK_SHARED_RING *Ring = Xsk->Tx.CompletionRing.Shared;
    UINT32 LocalProducerIndex = Ring->ProducerIndex;
    UINT32 Count;
    UINT64 RelativeAddress;
    UMEM_MAPPING *Mapping = XskGetTxMapping(Xsk);

    if (Xsk->Tx.Xdp.OutOfOrderCompletion) {
        XDP_RING *XdpRing = Xsk->Tx.Xdp.CompletionRing;
        UINT64 *Completion;

        if (!NT_VERIFY(XskRingProdReserve(&Xsk->Tx.CompletionRing, MAXUINT32) >=
                XdpRingCount(XdpRing))) {
            //
            // If the above condition does not hold, the XSK TX completion ring is
            // no longer valid. This implies an application programming error.
            //
            XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INVALID_RING);
            return;
        }

        //
        // Move all entries from the XDP TX completion ring to the XSK TX completion
        // ring. We have ensured sufficient XSK TX completion ring space when we
        // consumed from the XSK TX ring.
        //
        while (XdpRingCount(XdpRing) > 0) {
            Completion = XdpRingGetElement(XdpRing, XdpRing->ConsumerIndex++ & XdpRing->Mask);

            if (Xsk->Tx.Xdp.ExtensionFlags.VirtualAddress) {
                RelativeAddress = *Completion - (UINT64)Mapping->SystemAddress;
            } else if (Xsk->Tx.Xdp.ExtensionFlags.LogicalAddress) {
                RelativeAddress = *Completion - Mapping->DmaAddress.QuadPart;
            } else if (Xsk->Tx.Xdp.ExtensionFlags.Mdl) {
                RelativeAddress = *Completion;
            } else {
                //
                // One of the above extensions must have be enabled.
                //
                ASSERT(FALSE);
                RelativeAddress = 0;
            }

            XskWriteUmemTxCompletion(Xsk, LocalProducerIndex++, RelativeAddress);
        }
    } else {
        XDP_RING *XdpRing = Xsk->Tx.Xdp.FrameRing;
        XDP_FRAME *Frame;

        if (!NT_VERIFY(XskRingProdReserve(&Xsk->Tx.CompletionRing, MAXUINT32) >=
                (XdpRing->ConsumerIndex - XdpRing->Reserved))) {
            //
            // If the above condition does not hold, the XSK TX completion ring is
            // no longer valid. This implies an application programming error.
            //
            XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INVALID_RING);
            return;
        }

        //
        // Move all completed entries from the XDP TX ring to the XSK TX completion
        // ring. We have ensured sufficient XSK TX completion ring space when we
        // consumed from the XSK TX ring.
        //
        while ((XdpRing->ConsumerIndex - XdpRing->Reserved) > 0) {
            Frame = XdpRingGetElement(XdpRing, XdpRing->Reserved++ & XdpRing->Mask);

            if (Xsk->Tx.Xdp.ExtensionFlags.VirtualAddress) {
                XDP_BUFFER_VIRTUAL_ADDRESS *Va;
                Va = XdpGetVirtualAddressExtension(&Frame->Buffer, &Xsk->Tx.Xdp.VaExtension);
                RelativeAddress = Va->VirtualAddress - Mapping->SystemAddress;
            } else if (Xsk->Tx.Xdp.ExtensionFlags.LogicalAddress) {
                XDP_BUFFER_LOGICAL_ADDRESS *La;
                La = XdpGetLogicalAddressExtension(&Frame->Buffer, &Xsk->Tx.Xdp.LaExtension);
                RelativeAddress = La->LogicalAddress - Mapping->DmaAddress.QuadPart;
            } else if (Xsk->Tx.Xdp.ExtensionFlags.Mdl) {
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

            XskWriteUmemTxCompletion(Xsk, LocalProducerIndex++, RelativeAddress);
        }
    }

    Count = LocalProducerIndex - Ring->ProducerIndex;

    if (Count > 0) {
        Xsk->Tx.Xdp.OutstandingFrames -= Count;

        XskRingProdSubmit(&Xsk->Tx.CompletionRing, Count);

        //
        // N.B. See comment in XskNotify.
        //
        KeMemoryBarrier();

        if (KeReadStateEvent(&Xsk->IoWaitEvent) == 0 &&
            (Xsk->IoWaitFlags & XSK_NOTIFY_WAIT_TX)) {
            XskSignalReadyIo(Xsk, XSK_NOTIFY_WAIT_TX);
        }
    }

    if (Xsk->State > XskBound) {
        if (Xsk->Tx.Xdp.OutstandingFrames == 0) {
            KeSetEvent(&Xsk->Tx.Xdp.OutstandingFlushComplete, 0, FALSE);
        }
    }
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XskFlushTransmit(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    )
{
    XSK *Xsk = CONTAINING_RECORD(XdpTxQueue, XSK, Tx.Xdp.ExclusiveDispatch);

    XdbgEnterQueueEc(&Xsk->Tx.Xdp, TRUE);

    XskFillTxCompletion(Xsk);
    XskFillTx(Xsk);

    XdbgExitQueueEc(&Xsk->Tx.Xdp);
}

static CONST XDP_TX_QUEUE_DISPATCH XskTxDispatch = {
    .FlushTransmit = XskFlushTransmit,
};

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
FORCEINLINE
HANDLE
ProbeAndReadHandle(
    _In_reads_bytes_(sizeof(HANDLE)) volatile CONST HANDLE *Address
    )
{
    C_ASSERT(sizeof(HANDLE) == sizeof(PVOID));
    if (Address >= (HANDLE * const)MM_USER_PROBE_ADDRESS) {
        Address = (HANDLE * const)MM_USER_PROBE_ADDRESS;
    }
    _ReadWriteBarrier();
    return (HANDLE)ReadPointerNoFence((PVOID *)Address);
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
        try {
            TargetHandle = ProbeAndReadHandle((HANDLE*)HandleBuffer);
        } except (EXCEPTION_EXECUTE_HANDLER) {
            Status = GetExceptionCode();
            goto Exit;
        }
    } else {
        TargetHandle = *(HANDLE*)HandleBuffer;
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

    TraceInfo(TRACE_XSK, "XSK=%p Status=%!STATUS!", Xsk, Status);

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

    TraceInfo(TRACE_XSK, "XSK=%p", Xsk);

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

    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    Xsk = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Xsk), POOLTAG_XSK);
    if (Xsk == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Xsk->Header.ObjectType = XDP_OBJECT_TYPE_XSK;
    Xsk->Header.Dispatch = &XskFileDispatch;
    Xsk->ReferenceCount = 1;
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
    XdbgInitializeQueueEc(&Xsk->Tx.Xdp);

    IrpSp->FileObject->FsContext = Xsk;

Exit:

    TraceInfo(TRACE_XSK, "XSK=%p Status=%!STATUS!", Xsk, Status);

    TraceExit(TRACE_XSK);

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
    ExAcquirePushLockExclusive(&Xsk->PollLock);
    InterlockedDecrement((LONG *)&Xsk->PollWaiters);
}

static
VOID
XskReleasePollLock(
    _In_ XSK *Xsk
    )
{
    ExReleasePushLockExclusive(&Xsk->PollLock);
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
        NotifyFlags |= XSK_NOTIFY_WAIT_RX;
    }

    if (Xsk->Tx.Ring.Size > 0 && Xsk->Tx.Xdp.PollHandle == NULL) {
        Status = XskAcquirePollModeSocketTx(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        NotifyFlags |= XSK_NOTIFY_WAIT_TX;
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
        if (Xsk->Tx.Xdp.InterfaceNotify != NULL) {
            XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_TX;

            XdbgNotifyQueueEc(&Xsk->Tx.Xdp, NotifyFlags);
            Xsk->Tx.Xdp.InterfaceNotify(Xsk->Tx.Xdp.InterfaceQueue, NotifyFlags);
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

    if (Xsk->State != XskBound && PollMode != XSK_POLL_MODE_DEFAULT) {
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

    if (XdpRxQueueGetMaxmimumFragments(Config) > 1) {
        Xsk->Rx.Xdp.FragmentRing = XdpRxQueueGetFragmentRing(Config);

        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
            XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpRxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Rx.Xdp.FragmentExtension);
    }

    if (XdpRxQueueIsRxBatchEnabled(Config)) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_FRAME_EXTENSION_RX_ACTION_NAME,
            XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpRxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Rx.Xdp.RxActionExtension);
    }

    XskAcquirePollLock(Xsk);

    if (Xsk->State == XskBound) {
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

    TraceInfo(TRACE_XSK, "XSK=%p NotificationType=%u", Xsk, NotificationType);

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
    _In_ HANDLE XskHandle,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    XSK *Xsk = (XSK *)XskHandle;

    if (Xsk->Rx.Xdp.Queue == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    if (Xsk->Rx.Xdp.Queue != RxQueue) {
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    return STATUS_SUCCESS;
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
    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

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

    TraceInfo(TRACE_XSK, "XSK=%p Status=%!STATUS!", Xsk, STATUS_SUCCESS);

    TraceExit(TRACE_XSK);

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
    DEVICE_DESCRIPTION DeviceDescription = { 0 };
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
            Xsk->Tx.DmaAdapter, (ULONG)Xsk->Umem->Reg.totalSize, &Mapping->DmaAddress, TRUE);
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
    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    if (Xsk->Rx.Xdp.Queue != NULL) {
        XdpRxQueueSync(Xsk->Rx.Xdp.Queue, XskRxSyncDetach, Xsk);
        XdpRxQueueDeregisterNotifications(Xsk->Rx.Xdp.Queue, &Xsk->Rx.Xdp.QueueNotificationEntry);
        XdpRxQueueDereference(Xsk->Rx.Xdp.Queue);
        Xsk->Rx.Xdp.Queue = NULL;
    }

    if (Xsk->Rx.Xdp.IfHandle != NULL) {
        XdpIfDereferenceBinding(Xsk->Rx.Xdp.IfHandle);
        Xsk->Rx.Xdp.IfHandle = NULL;
    }

    XskKernelRingSetError(&Xsk->Rx.Ring, XSK_ERROR_INTERFACE_DETACH);
    XskKernelRingSetError(&Xsk->Rx.FillRing, XSK_ERROR_INTERFACE_DETACH);

    TraceExit(TRACE_XSK);
}

static
VOID
XskDetachTxIf(
    _In_ XSK *Xsk
    )
{
    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    if (Xsk->Tx.Xdp.Queue != NULL) {
        if (Xsk->Tx.Xdp.InterfaceQueue != NULL) {
            if (Xsk->Tx.Xdp.InterfaceNotify != NULL) {
                //
                // Wait for all outstanding TX packets to complete.
                //
                NT_VERIFY(XskPoke(Xsk, XSK_NOTIFY_POKE_TX, 0) == STATUS_SUCCESS);
                KeWaitForSingleObject(
                    &Xsk->Tx.Xdp.OutstandingFlushComplete, Executive, KernelMode, FALSE, NULL);
                ASSERT(Xsk->Tx.Xdp.OutstandingFrames == 0);

                ExAcquirePushLockExclusive(&Xsk->PollLock);
                Xsk->Tx.Xdp.InterfaceNotify = NULL;
                ExReleasePushLockExclusive(&Xsk->PollLock);
            } else {
                ASSERT(Xsk->Tx.Xdp.OutstandingFrames == 0);
            }

            Xsk->Tx.Xdp.InterfaceQueue = NULL;
        }

        XskCleanupDma(Xsk);

        XskFreeBounceBuffer(&Xsk->Tx.Bounce);

        XdpTxQueueClose(Xsk->Tx.Xdp.Queue);
        Xsk->Tx.Xdp.Queue = NULL;
    }

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        XdpIfDereferenceBinding(Xsk->Tx.Xdp.IfHandle);
        Xsk->Tx.Xdp.IfHandle = NULL;
    }

    XskKernelRingSetError(&Xsk->Tx.Ring, XSK_ERROR_INTERFACE_DETACH);
    XskKernelRingSetError(&Xsk->Tx.CompletionRing, XSK_ERROR_INTERFACE_DETACH);

    TraceExit(TRACE_XSK);
}

VOID
XskDetachTxEvent(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ VOID *Client
    )
{
    XSK *Xsk = Client;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(TxQueue);

    //
    // Set the state to detached, except when socket closure has raced this
    // detach event.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if (Xsk->State != XskClosing) {
        ASSERT(Xsk->State == XskBinding || Xsk->State == XskBound);
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

    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    ASSERT(Xsk->Rx.Xdp.IfHandle == NULL);
    ASSERT(Xsk->Rx.Ring.Size > 0);
    Xsk->Rx.Xdp.IfHandle = WorkItem->IfWorkItem.BindingHandle;

    Status =
        XdpRxQueueFindOrCreate(
            Xsk->Rx.Xdp.IfHandle, &Xsk->Rx.Xdp.HookId, WorkItem->QueueId, &Xsk->Rx.Xdp.Queue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XdpRxQueueRegisterNotifications(
        Xsk->Rx.Xdp.Queue, &Xsk->Rx.Xdp.QueueNotificationEntry, XskNotifyRxQueue);

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
XskBindTxIf(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XSK_BINDING_WORKITEM *WorkItem = (XSK_BINDING_WORKITEM *)Item;
    XSK *Xsk = WorkItem->Xsk;
    CONST XDP_INTERFACE_TX_QUEUE_DISPATCH *InterfaceTxDispatch;
    XDP_TX_QUEUE_CONFIG_ACTIVATE Config;
    CONST XDP_TX_CAPABILITIES *InterfaceCapabilities;
    XDP_EXTENSION_INFO ExtensionInfo;
    NTSTATUS Status;

    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);
    ASSERT (Xsk->Tx.Ring.Size > 0);
    Xsk->Tx.Xdp.IfHandle = WorkItem->IfWorkItem.BindingHandle;
    Xsk->Tx.Xdp.ExclusiveDispatch = XskTxDispatch;

    Status =
        XdpTxQueueCreate(
            Xsk->Tx.Xdp.IfHandle, WorkItem->QueueId, &Xsk->Tx.Xdp.HookId, Xsk, XskDetachTxEvent,
            (XDP_TX_QUEUE_HANDLE)&Xsk->Tx.Xdp.ExclusiveDispatch, &Xsk->Tx.Xdp.Queue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    InterfaceCapabilities = XdpTxQueueGetCapabilities(Xsk->Tx.Xdp.Queue);
    Xsk->Tx.Xdp.MaxBufferLength = InterfaceCapabilities->MaximumBufferSize;
    Xsk->Tx.Xdp.MaxFrameLength = InterfaceCapabilities->MaximumFrameSize;
    Xsk->Tx.Xdp.OutOfOrderCompletion = InterfaceCapabilities->OutOfOrderCompletionEnabled;

    Config = XdpTxQueueGetConfig(Xsk->Tx.Xdp.Queue);

    Xsk->Tx.Xdp.FrameRing = XdpTxQueueGetFrameRing(Config);
    if (Xsk->Tx.Xdp.OutOfOrderCompletion) {
        Xsk->Tx.Xdp.CompletionRing = XdpTxQueueGetCompletionRing(Config);
    }

    Xsk->Tx.Xdp.ExtensionFlags.VirtualAddress = XdpTxQueueIsVirtualAddressEnabled(Config);
    if (Xsk->Tx.Xdp.ExtensionFlags.VirtualAddress) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
            XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.VaExtension);
    }

    Xsk->Tx.Xdp.ExtensionFlags.LogicalAddress = XdpTxQueueIsLogicalAddressEnabled(Config);
    if (Xsk->Tx.Xdp.ExtensionFlags.LogicalAddress) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME,
            XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.LaExtension);

        Status = XskSetupDma(Xsk);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Xsk->Tx.Xdp.ExtensionFlags.Mdl = XdpTxQueueIsMdlEnabled(Config);
    if (Xsk->Tx.Xdp.ExtensionFlags.Mdl) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_MDL_NAME,
            XDP_BUFFER_EXTENSION_MDL_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpTxQueueGetExtension(Config, &ExtensionInfo, &Xsk->Tx.Xdp.MdlExtension);
    }

    Status = XskAllocateTxBounceBuffer(Xsk);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpTxQueueActivateExclusive(
            Xsk->Tx.Xdp.Queue, &InterfaceTxDispatch, &Xsk->Tx.Xdp.InterfaceQueue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ExAcquirePushLockExclusive(&Xsk->PollLock);
    Xsk->Tx.Xdp.InterfaceNotify = InterfaceTxDispatch->InterfaceNotifyQueue;
    ExReleasePushLockExclusive(&Xsk->PollLock);

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
XskReferenceUmem(
    UMEM *Umem
    )
{
    InterlockedIncrement(&Umem->ReferenceCount);
}

static
VOID
XskDereferenceUmem(
    UMEM *Umem
    )
{
    if (InterlockedDecrement(&Umem->ReferenceCount) == 0) {
        TraceInfo(TRACE_XSK, "Destroying UMEM=%p", Umem);

        if (Umem->Mapping.Mdl != NULL) {
            VOID *CurrentProcess = PsGetCurrentProcess();
            KAPC_STATE ApcState;

            ASSERT(Umem->OwningProcess != NULL);

            if (CurrentProcess != Umem->OwningProcess) {
                KeStackAttachProcess(Umem->OwningProcess, &ApcState);
            }

            if (Umem->ReservedMapping != NULL) {
                if (Umem->Mapping.SystemAddress != NULL) {
                    MmUnmapReservedMapping(Umem->ReservedMapping, POOLTAG_UMEM, Umem->Mapping.Mdl);
                }
                MmFreeMappingAddress(Umem->ReservedMapping, POOLTAG_UMEM);
            }
            if (Umem->Mapping.Mdl->MdlFlags & MDL_PAGES_LOCKED) {
                MmUnlockPages(Umem->Mapping.Mdl);
            }

            if (CurrentProcess != Umem->OwningProcess) {
#pragma prefast(suppress:6001, "ApcState is correctly initialized in KeStackAttachProcess above.")
                KeUnstackDetachProcess(&ApcState);
            }

            ObDereferenceObject(Umem->OwningProcess);
            Umem->OwningProcess = NULL;

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

    TraceEnter(TRACE_XSK, "XSK=%p", Xsk);

    UNREFERENCED_PARAMETER(Irp);

    ASSERT(Xsk->State == XskClosing);

    if (Xsk->Tx.Xdp.IfHandle != NULL) {
        XSK_BINDING_WORKITEM WorkItem = {0};

        KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
        WorkItem.Xsk = Xsk;
        WorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;
        WorkItem.IfWorkItem.WorkRoutine = XskDetachTxIfWorker;

        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);
        KeWaitForSingleObject(
            &WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);

        ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);
    }

    if (Xsk->Rx.Xdp.IfHandle != NULL) {
        XSK_BINDING_WORKITEM WorkItem = {0};

        KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
        WorkItem.Xsk = Xsk;
        WorkItem.IfWorkItem.BindingHandle = Xsk->Rx.Xdp.IfHandle;
        WorkItem.IfWorkItem.WorkRoutine = XskDetachRxIfWorker;

        XdpIfQueueWorkItem(&WorkItem.IfWorkItem);
        KeWaitForSingleObject(
            &WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);

        ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);
    }

    if (Xsk->Umem != NULL) {
        XskDereferenceUmem(Xsk->Umem);
    }

    XskFreeRing(&Xsk->Rx.Ring);
    XskFreeRing(&Xsk->Rx.FillRing);
    XskFreeRing(&Xsk->Tx.Ring);
    XskFreeRing(&Xsk->Tx.CompletionRing);

    XskDereference(Xsk);

    TraceInfo(TRACE_XSK, "XSK=%p Status=%!STATUS!", Xsk, STATUS_SUCCESS);

    TraceExit(TRACE_XSK);

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
    UMEM *Umem;
    XSK_BIND_IN Bind = {0};
    XSK_BINDING_WORKITEM WorkItem = {0};
    UMEM *ReferencedSharedUmem = NULL;
    KIRQL OldIrql;
    XDP_INTERFACE_MODE *ModeFilter = NULL;
    XDP_INTERFACE_MODE RequiredMode;
    BOOLEAN BindIfInitiated = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(XSK_BIND_IN)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Bind = *(XSK_BIND_IN*)Irp->AssociatedIrp.SystemBuffer;

    TraceEnter(
        TRACE_XSK, "XSK=%p IfIndex=%u QueueId=%u Flags=%x",
        Xsk, Bind.IfIndex, Bind.QueueId, Bind.Flags);

    if ((Bind.Flags & ~(XSK_BIND_GENERIC | XSK_BIND_NATIVE)) ||
        !RTL_IS_CLEAR_OR_SINGLE_FLAG(Bind.Flags, XSK_BIND_GENERIC | XSK_BIND_NATIVE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Bind.Flags & XSK_BIND_GENERIC) {
        RequiredMode = XDP_INTERFACE_MODE_GENERIC;
        ModeFilter = &RequiredMode;
    }

    if (Bind.Flags & XSK_BIND_NATIVE) {
        RequiredMode = XDP_INTERFACE_MODE_NATIVE;
        ModeFilter = &RequiredMode;
    }

    if (Bind.SharedUmemSock != NULL) {
        //
        // Acquire reference on the shared UMEM first to avoid XSK deadlock.
        //
        XSK *XskUmemOwner;
        FILE_OBJECT *FileObject = NULL;

        Status =
            XdpReferenceObjectByHandle(
                Bind.SharedUmemSock, XDP_OBJECT_TYPE_XSK, Irp->RequestorMode,
                ((IrpSp->Parameters.DeviceIoControl.IoControlCode >> 14) & 3),
                &FileObject);
        if (Status != STATUS_SUCCESS) {
            goto Exit;
        }

        XskUmemOwner = (XSK*)FileObject->FsContext;
        KeAcquireSpinLock(&XskUmemOwner->Lock, &OldIrql);
        if (XskUmemOwner->State >= XskClosing || XskUmemOwner->Umem == NULL) {
            KeReleaseSpinLock(&XskUmemOwner->Lock, OldIrql);
            ObDereferenceObject(FileObject);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        XskReferenceUmem(XskUmemOwner->Umem);
        ReferencedSharedUmem = XskUmemOwner->Umem;
        KeReleaseSpinLock(&XskUmemOwner->Lock, OldIrql);
        ObDereferenceObject(FileObject);
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    Umem = (ReferencedSharedUmem != NULL) ? ReferencedSharedUmem : Xsk->Umem;
    if (Umem == NULL) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }

    if (Xsk->State != XskUnbound) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->Rx.Ring.Size == 0 && Xsk->Tx.Ring.Size == 0) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->Rx.Ring.Size > 0 && Xsk->Rx.FillRing.Size == 0) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }
    if (Xsk->Tx.Ring.Size > 0 && Xsk->Tx.CompletionRing.Size == 0) {
        Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        goto Exit;
    }

    Xsk->State = XskBinding;
    Xsk->Umem = Umem;
    BindIfInitiated = TRUE;

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    KeInitializeEvent(&WorkItem.CompletionEvent, SynchronizationEvent, FALSE);

    if (Xsk->Rx.Ring.Size > 0) {
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

    if (Xsk->Tx.Ring.Size > 0) {
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

    ReferencedSharedUmem = NULL;

Exit:

    if (!NT_SUCCESS(Status) && BindIfInitiated) {
        if (Xsk->Tx.Xdp.IfHandle != NULL) {
            WorkItem.Xsk = Xsk;
            WorkItem.QueueId = Bind.QueueId;
            WorkItem.IfWorkItem.WorkRoutine = XskDetachTxIfWorker;
            WorkItem.IfWorkItem.BindingHandle = Xsk->Tx.Xdp.IfHandle;

            XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

            KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
            ASSERT(Xsk->Tx.Xdp.IfHandle == NULL);
        }

        if (Xsk->Rx.Xdp.IfHandle != NULL) {
            WorkItem.Xsk = Xsk;
            WorkItem.QueueId = Bind.QueueId;
            WorkItem.IfWorkItem.WorkRoutine = XskDetachRxIfWorker;
            WorkItem.IfWorkItem.BindingHandle = Xsk->Rx.Xdp.IfHandle;

            XdpIfQueueWorkItem(&WorkItem.IfWorkItem);

            KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
            ASSERT(Xsk->Rx.Xdp.IfHandle == NULL);
        }
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (ReferencedSharedUmem != NULL) {
        XskDereferenceUmem(ReferencedSharedUmem);
    }

    if (BindIfInitiated) {
        if (Xsk->Umem == ReferencedSharedUmem) {
            ASSERT(!NT_SUCCESS(Status));
            Xsk->Umem = NULL;
        }
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
        TRACE_XSK, "XSK=%p IfIndex=%u QueueId=%u Flags=%x Status=%!STATUS!",
        Xsk, Bind.IfIndex, Bind.QueueId, Bind.Flags, Status);

    TraceExitStatus(TRACE_XSK);

    return Status;
}

static
NTSTATUS
XskSockoptStatistics(
    _In_ XSK *Xsk,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    XSK_STATISTICS *Statistics;
    KIRQL OldIrql;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(XSK_STATISTICS)) {
        return STATUS_INVALID_PARAMETER;
    }

    Statistics = (XSK_STATISTICS*)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Statistics, sizeof(*Statistics));

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (Xsk->State != XskBound) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    *Statistics = Xsk->Statistics;

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    Irp->IoStatus.Information = sizeof(*Statistics);

    return STATUS_SUCCESS;
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

    Info->ring = Ring->UserVa;
    Info->descriptorsOffset = sizeof(XSK_SHARED_RING);
    Info->producerIndexOffset = FIELD_OFFSET(XSK_SHARED_RING, ProducerIndex);
    Info->consumerIndexOffset = FIELD_OFFSET(XSK_SHARED_RING, ConsumerIndex);
    Info->flagsOffset = FIELD_OFFSET(XSK_SHARED_RING, Flags);
    Info->size = Ring->Size;
    Info->elementStride = Ring->ElementStride;
}

static
NTSTATUS
XskSockoptRingInfo(
    _In_ XSK *Xsk,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    XSK_RING_INFO_SET *InfoSet = Irp->AssociatedIrp.SystemBuffer;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(XSK_RING_INFO_SET)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(InfoSet, sizeof(*InfoSet));

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

    if (Xsk->State == XskClosing) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    if (Xsk->Rx.Ring.Size != 0) {
        XskFillRingInfo(&Xsk->Rx.Ring, &InfoSet->rx);
    }
    if (Xsk->Rx.FillRing.Size != 0) {
        XskFillRingInfo(&Xsk->Rx.FillRing, &InfoSet->fill);
    }
    if (Xsk->Tx.Ring.Size != 0) {
        XskFillRingInfo(&Xsk->Tx.Ring, &InfoSet->tx);
    }
    if (Xsk->Tx.CompletionRing.Size != 0) {
        XskFillRingInfo(&Xsk->Tx.CompletionRing, &InfoSet->completion);
    }

    Irp->IoStatus.Information = sizeof(*InfoSet);

Exit:

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    return Status;
}

static
NTSTATUS
XskSockoptUmemReg(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    const VOID * SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    UMEM *Umem = NULL;
    KIRQL OldIrql = { 0 };
    BOOLEAN IsLockHeld = FALSE;

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

    Umem->ReferenceCount = 1;

    try {
        if (RequestorMode != KernelMode) {
            ProbeForRead(
                (VOID*)SockoptInputBuffer, SockoptInputBufferLength,
                PROBE_ALIGNMENT(XSK_UMEM_REG));
        }
        Umem->Reg = *(XSK_UMEM_REG*)SockoptInputBuffer;
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    if (Umem->Reg.totalSize == 0 || Umem->Reg.totalSize > MAXULONG) {
        // TODO: support up to MAXUINT64?
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    if (Umem->Reg.headroom > MAXUINT16 ||
        Umem->Reg.headroom > Umem->Reg.chunkSize ||
        Umem->Reg.chunkSize > Umem->Reg.totalSize ||
        Umem->Reg.chunkSize == 0) {
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
            Umem->Reg.address,
            (ULONG)Umem->Reg.totalSize,
            FALSE, // SecondaryBuffer
            FALSE, // ChargeQuota
            NULL); // Irp
    if (Umem->Mapping.Mdl == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Umem->OwningProcess = PsGetCurrentProcess();
    ObReferenceObject(Umem->OwningProcess);

    try {
        MmProbeAndLockPages(Umem->Mapping.Mdl, RequestorMode, IoWriteAccess);
    } except (EXCEPTION_EXECUTE_HANDLER) {
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
            BYTE_OFFSET(Umem->Reg.address) + Umem->Reg.totalSize, POOLTAG_UMEM);
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

    if (Umem->Reg.totalSize % Umem->Reg.chunkSize != 0) {
        //
        // The final chunk is truncated, which might be required for alignment
        // reasons. Ignore the final chunk.
        //
        ASSERT(Umem->Reg.totalSize > Umem->Reg.chunkSize);
        Umem->Reg.totalSize -= (Umem->Reg.totalSize % Umem->Reg.chunkSize);
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
        TRACE_XSK, "XSK=%p Set UMEM=%p TotalSize=%llu ChunkSize=%llu Headroom=%u",
        Xsk, Umem, Umem->Reg.totalSize, Umem->Reg.chunkSize, Umem->Reg.headroom);

    Xsk->Umem = Umem;
    Umem = NULL;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }
    if (Umem != NULL) {
        XskDereferenceUmem(Umem);
    }
    return Status;
}

static
NTSTATUS
XskSockoptRingSize(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    const VOID * SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    XSK_SHARED_RING *Shared = NULL;
    MDL *Mdl = NULL;
    VOID *UserVa = NULL;
    XSK_KERNEL_RING *Ring = NULL;
    UINT32 NumDescriptors;
    ULONG DescriptorSize;
    ULONG AllocationSize;
    KIRQL OldIrql = { 0 };
    BOOLEAN IsLockHeld = FALSE;

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptInputBuffer = Sockopt->InputBuffer;
    SockoptInputBufferLength = Sockopt->InputBufferLength;

    if (SockoptInputBufferLength < sizeof(UINT32)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)SockoptInputBuffer, SockoptInputBufferLength, PROBE_ALIGNMENT(UINT32));
        }
        NumDescriptors = *(UINT32 *)SockoptInputBuffer;
    } except (EXCEPTION_EXECUTE_HANDLER) {
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
        DescriptorSize = sizeof(XSK_BUFFER_DESCRIPTOR);
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

    try {
        UserVa =
            MmMapLockedPagesSpecifyCache(
                Mdl,
                RequestorMode,
                MmNonCached,
                NULL, // RequestedAddress
                FALSE,// BugCheckOnFailure
                NormalPagePriority | MdlMappingNoExecute);
        if (UserVa == NULL) {
            goto Exit;
        }
    } except (EXCEPTION_EXECUTE_HANDLER) {
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
        TRACE_XSK, "XSK=%p Set ring Type=%u Size=%u",
        Xsk, Sockopt->Option, NumDescriptors);

    Ring->Shared = Shared;
    Ring->Mdl = Mdl;
    Ring->UserVa = UserVa;
    Ring->Size = NumDescriptors;
    Ring->Mask = NumDescriptors - 1;
    Ring->ElementStride = DescriptorSize;
    Ring->OwningProcess = PsGetCurrentProcess();
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
    NTSTATUS Status = STATUS_SUCCESS;
    XDP_HOOK_ID *HookId = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*HookId)) {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);

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

    Irp->IoStatus.Information = sizeof(*HookId);

Exit:

    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

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
    NTSTATUS Status = STATUS_SUCCESS;
    CONST VOID *SockoptIn;
    UINT32 SockoptInSize;
    XDP_HOOK_ID HookId;
    KIRQL OldIrql = {0};
    BOOLEAN IsLockHeld = FALSE;

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptIn = Sockopt->InputBuffer;
    SockoptInSize = Sockopt->InputBufferLength;

    if (SockoptInSize < sizeof(HookId)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)SockoptIn, SockoptInSize, PROBE_ALIGNMENT(XDP_HOOK_ID));
        }
        HookId = *(CONST XDP_HOOK_ID *)SockoptIn;
    } except (EXCEPTION_EXECUTE_HANDLER) {
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
            "XSK=%p Set XSK_SOCKOPT_RX_HOOK_ID Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
            Xsk, HookId.Layer, HookId.Direction, HookId.SubLayer);
        Xsk->Rx.Xdp.HookId = HookId;
        break;

    case XSK_SOCKOPT_TX_HOOK_ID:
        TraceInfo(
            TRACE_XSK,
            "XSK=%p Set XSK_SOCKOPT_TX_HOOK_ID Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
            Xsk, HookId.Layer, HookId.Direction, HookId.SubLayer);
        Xsk->Tx.Xdp.HookId = HookId;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
    }

    return Status;
}

static
XskSockoptGetError(
    _In_ XSK *Xsk,
    _In_ UINT32 Option,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XSK_ERROR *XskError = Irp->AssociatedIrp.SystemBuffer;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*XskError)) {
        return STATUS_INVALID_PARAMETER;
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

    Irp->IoStatus.Information = sizeof(*XskError);

Exit:

    return Status;
}

static
UINT32
XskQueryReadyIo(
    _In_ XSK* Xsk,
    _In_ UINT32 InFlags
    )
{
    UINT32 OutFlags = 0;

    if (InFlags & XSK_NOTIFY_WAIT_TX && XskRingConsPeek(&Xsk->Tx.CompletionRing, 1) > 0) {
        OutFlags |= XSK_NOTIFY_WAIT_TX;
    }
    if (InFlags & XSK_NOTIFY_WAIT_RX && XskRingConsPeek(&Xsk->Rx.Ring, 1) > 0) {
        OutFlags |= XSK_NOTIFY_WAIT_RX;
    }

    return OutFlags;
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

    WaitFlags = Flags & (XSK_NOTIFY_WAIT_RX | XSK_NOTIFY_WAIT_TX);

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
            ExReleasePushLockExclusive(&Xsk->PollLock);
            ExAcquirePushLockExclusive(&Xsk->PollLock);

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
XskSockoptPollMode(
    _In_ XSK *Xsk,
    _In_ XSK_SET_SOCKOPT_IN *Sockopt,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    const VOID * SockoptInputBuffer;
    UINT32 SockoptInputBufferLength;
    XSK_POLL_MODE PollMode;

    //
    // This is a nested buffer not copied by IO manager, so it needs special care.
    //
    SockoptInputBuffer = Sockopt->InputBuffer;
    SockoptInputBufferLength = Sockopt->InputBufferLength;

    if (SockoptInputBufferLength < sizeof(XSK_POLL_MODE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    try {
        if (RequestorMode != KernelMode) {
            ProbeForRead(
                (VOID*)SockoptInputBuffer, SockoptInputBufferLength, PROBE_ALIGNMENT(XSK_POLL_MODE));
        }
        PollMode = *(XSK_POLL_MODE *)SockoptInputBuffer;
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    XskAcquirePollLock(Xsk);
    Status = XskSetPollMode(Xsk, PollMode);
    XskReleasePollLock(Xsk);

    if (NT_SUCCESS(Status)) {
        TraceInfo(TRACE_XSK, "XSK=%p Set poll mode PollMode=%u", Xsk, PollMode);
    }

Exit:

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
        Status = XskSockoptRingInfo(Xsk, Irp, IrpSp);
        break;
    case XSK_SOCKOPT_STATISTICS:
        Status = XskSockoptStatistics(Xsk, Irp, IrpSp);
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

    TraceInfo(
        TRACE_XSK, "Xsk=%p Option=%u Status=%!STATUS!", Xsk, Option, Status);

    return Status;
}

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
        Status = XskSockoptUmemReg(Xsk, Sockopt, Irp->RequestorMode);
        break;
    case XSK_SOCKOPT_TX_RING_SIZE:
    case XSK_SOCKOPT_RX_RING_SIZE:
    case XSK_SOCKOPT_RX_FILL_RING_SIZE:
    case XSK_SOCKOPT_TX_COMPLETION_RING_SIZE:
        Status = XskSockoptRingSize(Xsk, Sockopt, Irp->RequestorMode);
        break;
    case XSK_SOCKOPT_RX_HOOK_ID:
    case XSK_SOCKOPT_TX_HOOK_ID:
        Status = XskSockoptSetHookId(Xsk, Sockopt, Irp->RequestorMode);
        break;
    case XSK_SOCKOPT_POLL_MODE:
        Status = XskSockoptPollMode(Xsk, Sockopt, Irp->RequestorMode);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    TraceInfo(
        TRACE_XSK, "XSK=%p Option=%u Status=%!STATUS!",
        Xsk, (Sockopt != NULL) ? Sockopt->Option : 0, Status);

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
    if (Xsk->State != XskBound){
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (InputBufferLength < sizeof(XSK_NOTIFY_IN)) {
        return STATUS_INVALID_PARAMETER;
    }

    try {
        ASSERT(InputBuffer);
        if (ExGetPreviousMode() != KernelMode) {
            ProbeForRead(
                (VOID*)InputBuffer, InputBufferLength, PROBE_ALIGNMENT(XSK_NOTIFY_IN));
        }

        *InFlags = ((XSK_NOTIFY_IN*)InputBuffer)->Flags;
        *TimeoutMilliseconds =
            ((XSK_NOTIFY_IN*)InputBuffer)->WaitTimeoutMilliseconds;
    } except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }

    if (*InFlags == 0 || *InFlags &
            ~(XSK_NOTIFY_POKE_RX | XSK_NOTIFY_POKE_TX | XSK_NOTIFY_WAIT_RX | XSK_NOTIFY_WAIT_TX)) {
        return STATUS_INVALID_PARAMETER;
    }

    if ((*InFlags & (XSK_NOTIFY_POKE_RX | XSK_NOTIFY_WAIT_RX) && Xsk->Rx.Ring.Size == 0) ||
        (*InFlags & (XSK_NOTIFY_POKE_TX | XSK_NOTIFY_WAIT_TX) && Xsk->Tx.Ring.Size == 0)) {
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

    ExAcquirePushLockExclusive(&Xsk->PollLock);

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

        if (Flags & XSK_NOTIFY_POKE_RX) {
            ASSERT(Xsk->Rx.Ring.Size > 0);
            ASSERT(Xsk->Rx.FillRing.Size > 0);
            //
            // TODO: Driver poke routine for zero copy RX.
            //
            ASSERT(Status == STATUS_SUCCESS);
        }

        if (Flags & XSK_NOTIFY_POKE_TX) {
            XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_TX;
            //
            // Before invoking the poke routine, atomically clear the need poke
            // flag on the TX ring. The poke routine is required to execute a
            // FlushTransmit, which guarantees the need poke flag can be set.
            //
            InterlockedAnd((LONG *)&Xsk->Tx.Ring.Shared->Flags, ~XSK_RING_FLAG_NEED_POKE);

            if (Xsk->Tx.Xdp.InterfaceNotify != NULL) {
                XdbgNotifyQueueEc(&Xsk->Tx.Xdp, NotifyFlags);
                Xsk->Tx.Xdp.InterfaceNotify(Xsk->Tx.Xdp.InterfaceQueue, NotifyFlags);
            } else {
                Status = STATUS_DEVICE_REMOVED;
            }
        }
    }

    ExReleasePushLockExclusive(&Xsk->PollLock);

    return Status;
}

static
NTSTATUS
XskNotify(
    _In_ XDP_FILE_OBJECT_HEADER *FileObjectHeader,
    _In_opt_ VOID *InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_ ULONG_PTR *Information
    )
{
    UINT32 TimeoutMilliseconds;
    UINT32 ReadyFlags;
    UINT32 InFlags;
    UINT32 OutFlags = 0;
    KIRQL OldIrql;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    XSK* Xsk = (XSK*)FileObjectHeader;
    XSK_IO_WAIT_FLAGS InternalFlags;
    CONST UINT32 WaitMask = (XSK_NOTIFY_WAIT_RX | XSK_NOTIFY_WAIT_TX);

    Status =
        XskNotifyValidateParams(
            Xsk, InputBuffer, InputBufferLength, &TimeoutMilliseconds, &InFlags);
    if (Status != STATUS_SUCCESS) {
        TraceError(TRACE_XSK, "Notify failed: Invalid params. XSK=%p", Xsk);
        goto Exit;
    }

    //
    // Snap the XSK notification state before performing the poke and/or wait.
    //
    InternalFlags = ReadULongAcquire((ULONG *)&Xsk->IoWaitInternalFlags);

    if (InFlags & (XSK_NOTIFY_POKE_RX | XSK_NOTIFY_POKE_TX)) {
        Status = XskPoke(Xsk, InFlags, TimeoutMilliseconds);
        if (Status != STATUS_SUCCESS) {
            TraceError(
                TRACE_XSK, "Notify failed: Poke failed. XSK=%p Status=%!STATUS!",
                Xsk, Status);
            goto Exit;
        }
    }

    if ((InFlags & (XSK_NOTIFY_WAIT_RX | XSK_NOTIFY_WAIT_TX)) == 0) {
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
        OutFlags |= ReadyFlags;
        ASSERT(Status == STATUS_SUCCESS);
        goto Exit;
    }

    //
    // Set up the wait context.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    if (Xsk->State != XskBound) {
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        TraceInfo(TRACE_XSK, "Notify wait failed: XSK not bound. XSK=%p", Xsk);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    if (Xsk->IoWaitFlags != 0) {
        //
        // There is currently a wait active. Only a single wait is allowed.
        //
        KeReleaseSpinLock(&Xsk->Lock, OldIrql);
        TraceError(TRACE_XSK, "Notify wait failed: Wait already active. XSK=%p", Xsk);
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Xsk->IoWaitFlags = InFlags & WaitMask;
    KeClearEvent(&Xsk->IoWaitEvent);
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

    //
    // Wait for IO.
    //
    Timeout.QuadPart = -1 * RTL_MILLISEC_TO_100NANOSEC(TimeoutMilliseconds);
    Status =
        KeWaitForSingleObject(
            &Xsk->IoWaitEvent, UserRequest, UserMode, FALSE,
            (TimeoutMilliseconds == INFINITE) ? NULL : &Timeout);

    //
    // Clean up the wait context.
    //
    KeAcquireSpinLock(&Xsk->Lock, &OldIrql);
    Xsk->IoWaitFlags = 0;
    KeReleaseSpinLock(&Xsk->Lock, OldIrql);

    //
    // Re-query ready IO regardless of the wait status.
    //
    ReadyFlags = XskQueryReadyIo(Xsk, InFlags);
    if (ReadyFlags != 0) {
        Status = STATUS_SUCCESS;
        OutFlags |= ReadyFlags;
    }

Exit:

    //
    // This IOCTL is assumed to never pend and code elsewhere takes advantage of
    // this assumption.
    //
    FRE_ASSERT(Status != STATUS_PENDING);

    *Information = OutFlags;
    return Status;
}

#pragma warning(push)
#pragma warning(disable:6101) // We don't set OutputBuffer
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
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    if (IoControlCode == IOCTL_XSK_NOTIFY) {
        IoStatus->Status =
            XskNotify(
                FileObjectHeader, InputBuffer, InputBufferLength,
                &IoStatus->Information);
        return TRUE;
    }

    IoStatus->Status = STATUS_INVALID_PARAMETER;

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

    XSK_BUFFER_DESCRIPTOR *Descriptor;

    RingIndex =
        (Xsk->Rx.FillRing.Shared->ConsumerIndex + FillOffset) &
            (Xsk->Rx.FillRing.Mask);
    UmemAddress = *(UINT64 *)XskKernelRingGetElement(&Xsk->Rx.FillRing, RingIndex);

    if (UmemAddress > Xsk->Umem->Reg.totalSize - Xsk->Umem->Reg.chunkSize) {
        //
        // Invalid FILL descriptor.
        //
        ++Xsk->Statistics.rxInvalidDescriptors;
        return;
    }

    UmemChunk = Xsk->Umem->Mapping.SystemAddress + UmemAddress;
    UmemOffset = Xsk->Umem->Reg.headroom;
    CopyLength = min(Buffer->DataLength, Xsk->Umem->Reg.chunkSize - UmemOffset);

    if (!XskGlobals.RxZeroCopy) {
        RtlCopyMemory(UmemChunk + UmemOffset, Va->VirtualAddress + Buffer->DataOffset, CopyLength);
    }
    if (CopyLength < Buffer->DataLength) {
        //
        // Not enough available space in Umem.
        //
        ++Xsk->Statistics.rxTruncated;
    } else if (FragmentRing != NULL) {
        Fragment = XdpGetFragmentExtension(Frame, &Xsk->Rx.Xdp.FragmentExtension);

        for (UINT32 Index = 0; Index < Fragment->FragmentBufferCount; Index++) {
            Buffer = XdpRingGetElement(FragmentRing, (FragmentIndex + Index) & FragmentRing->Mask);
            Va = XdpGetVirtualAddressExtension(Buffer, &Xsk->Rx.Xdp.VaExtension);

            UmemOffset += CopyLength;
            CopyLength = min(Buffer->DataLength, Xsk->Umem->Reg.chunkSize - UmemOffset);

            if (!XskGlobals.RxZeroCopy) {
                RtlCopyMemory(
                    UmemChunk + UmemOffset, Va->VirtualAddress + Buffer->DataOffset, CopyLength);
            }

            if (CopyLength < Buffer->DataLength) {
                //
                // Not enough available space in Umem.
                //
                ++Xsk->Statistics.rxTruncated;
                break;
            }
        }
    }

    RingIndex = (Xsk->Rx.Ring.Shared->ProducerIndex + *CompletionOffset) & (Xsk->Rx.Ring.Mask);
    Descriptor = XskKernelRingGetElement(&Xsk->Rx.Ring, RingIndex);
    Descriptor->address = UmemAddress;
    ASSERT(Xsk->Umem->Reg.headroom <= MAXUINT16);
    XskDescriptorSetOffset(&Descriptor->address, (UINT16)Xsk->Umem->Reg.headroom);
    Descriptor->length = UmemOffset - Xsk->Umem->Reg.headroom + CopyLength;

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
        Xsk->Statistics.rxDropped += BatchCount - RxProduced;
    }

    XskRingConsRelease(&Xsk->Rx.FillRing, RxFillConsumed);

    if (RxProduced > 0) {
        XskRingProdSubmit(&Xsk->Rx.Ring, RxProduced);

        //
        // N.B. See comment in XskNotify.
        //
        KeMemoryBarrier();

        if (KeReadStateEvent(&Xsk->IoWaitEvent) == 0 &&
            (Xsk->IoWaitFlags & XSK_NOTIFY_WAIT_RX)) {
            XskSignalReadyIo(Xsk, XSK_NOTIFY_WAIT_RX);
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

    if (!Xsk->Rx.Xdp.Flags.DatapathAttached) {
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
    case IOCTL_XSK_GET_SOCKOPT:
        Status = XskIrpGetSockopt(Irp, IrpSp);
        break;
    case IOCTL_XSK_SET_SOCKOPT:
        Status = XskIrpSetSockopt(Irp, IrpSp);
        break;
    case IOCTL_XSK_NOTIFY:
        Status =
            XskNotify(
                (XDP_FILE_OBJECT_HEADER *)IrpSp->FileObject,
                Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength,
                &Irp->IoStatus.Information);
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
    XskRegistryUpdate();
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
