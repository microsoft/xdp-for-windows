//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "tx.tmh"

//
// This module configures XDP transmit queues on interfaces.
//

static XDP_REG_WATCHER_CLIENT_ENTRY XdpTxRegWatcherEntry;

#define XDP_DEFAULT_TX_RING_SIZE 32
static UINT32 XdpTxRingSize = XDP_DEFAULT_TX_RING_SIZE;

typedef struct _XDP_TX_QUEUE_KEY {
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
} XDP_TX_QUEUE_KEY;

typedef enum _XDP_TX_QUEUE_STATE {
    XdpTxQueueStateCreated,
    XdpTxQueueStateActive,
    XdpTxQueueStateDeleted,
} XDP_TX_QUEUE_STATE;

typedef struct _XDP_TX_QUEUE {
    XDP_REFERENCE_COUNT InterlockedReferenceCount;
    XDP_REFERENCE_COUNT ReferenceCount;
    XDP_BINDING_HANDLE Binding;
    XDP_TX_QUEUE_KEY Key;
    XDP_TX_QUEUE_STATE State;
    XDP_BINDING_CLIENT_ENTRY BindingClientEntry;
    PCW_INSTANCE *PcwInstance;

    XDP_TX_CAPABILITIES InterfaceTxCapabilities;
    XDP_DMA_CAPABILITIES InterfaceDmaCapabilities;

    NDIS_HANDLE InterfacePollHandle;

    XDP_QUEUE_INFO QueueInfo;

    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS ConfigCreate;
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS ConfigActivate;
    BOOLEAN IsChecksumOffloadEnabled;

    XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle;

    struct {
        KSPIN_LOCK Lock;
        BOOLEAN WorkerQueued : 1;
        BOOLEAN DeleteNeeded : 1;
        BOOLEAN OffloadNeeded : 1;
        XDP_BINDING_WORKITEM WorkItem;
        XDP_TX_QUEUE_NOTIFY_DETAILS Details;
        LIST_ENTRY Clients;
    } Notify;

    //
    // Data path fields.
    //
    XDP_INTERFACE_HANDLE InterfaceTxQueue;
    XDP_INTERFACE_TX_QUEUE_DISPATCH *InterfaceTxDispatch;
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    XDP_EXTENSION_SET *FrameExtensionSet;
    XDP_EXTENSION_SET *BufferExtensionSet;
    XDP_EXTENSION_SET *TxFrameCompletionExtensionSet;
    XDP_EXTENSION TxCompletionContextExtension;
    XDP_PCW_TX_QUEUE PcwStats;
    LIST_ENTRY ClientList;
    LIST_ENTRY *FillEntry;
    XDP_TX_QUEUE_DISPATCH Dispatch;
    XDP_QUEUE_SYNC Sync;
#if DBG
    XDP_DBG_QUEUE_EC DbgEc;
#endif
} XDP_TX_QUEUE;

//
// Data path routines.
//

VOID
XdpTxQueueInvokeInterfaceNotify(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    )
{
    ASSERT(TxQueue->State == XdpTxQueueStateActive);

    XdbgNotifyQueueEc(TxQueue, Flags);
    TxQueue->InterfaceTxDispatch->InterfaceNotifyQueue(TxQueue->InterfaceTxQueue, Flags);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueDatapathComplete(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    XDP_TX_FRAME_COMPLETION_CONTEXT *CompletionContext;

    //
    // Review: This algorithm is somewhat naive: depending on the number of XSKs
    // bound to the queue, it may be advantageous to pre-sort completions to
    // enable better batching, and/or to defer the XSK completion epilogues to
    // the end of this routine.
    //

    if (TxQueue->CompletionRing == NULL) {
        XDP_RING *FrameRing = TxQueue->FrameRing;
        XDP_FRAME *Frame;

        while ((FrameRing->ConsumerIndex - FrameRing->Reserved) > 0) {
            Frame = XdpRingGetElement(FrameRing, FrameRing->Reserved & FrameRing->Mask);
            CompletionContext =
                XdpGetFrameTxCompletionContextExtension(
                    Frame, &TxQueue->TxCompletionContextExtension);

            //
            // Consumes one or more completions via the completion ring.
            //
            XskFillTxCompletion(CompletionContext->Context);
        }
    } else {
        XDP_RING *CompletionRing = TxQueue->CompletionRing;
        XDP_TX_FRAME_COMPLETION *FrameCompletion;

        while (XdpRingCount(CompletionRing) > 0) {
            FrameCompletion =
                XdpRingGetElement(
                    CompletionRing, CompletionRing->ConsumerIndex & CompletionRing->Mask);
            CompletionContext =
                XdpGetTxCompletionContextExtension(
                    FrameCompletion, &TxQueue->TxCompletionContextExtension);

            //
            // Consumes one or more completions via the frame ring.
            //
            XskFillTxCompletion(CompletionContext->Context);
        }
    }
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueDatapathFill(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    LIST_ENTRY *FirstEntry = TxQueue->FillEntry;
    XDP_RING *FrameRing = TxQueue->FrameRing;
    UINT32 TxLimit = FrameRing->Mask + 1;
    UINT32 TxAvailable;

    if (TxQueue->CompletionRing == NULL) {
        TxAvailable = TxLimit - (FrameRing->ProducerIndex - FrameRing->Reserved);
    } else {
        ASSERT(TxQueue->CompletionRing->Mask == FrameRing->Mask);
        TxAvailable = TxLimit - XdpRingCount(FrameRing);
    }

    while (TxAvailable > 0) {
        UINT32 FrameCount;

        if (TxQueue->FillEntry == &TxQueue->ClientList) {
            goto NextEntry;
        }

        FrameCount =
            XskFillTx(
                CONTAINING_RECORD(TxQueue->FillEntry, XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY, Link),
                TxAvailable);

        ASSERT(FrameCount <= TxAvailable);
        TxAvailable -= FrameCount;

NextEntry:

        TxQueue->FillEntry = TxQueue->FillEntry->Flink;

        if (TxQueue->FillEntry == FirstEntry) {
            break;
        }
    }

    STAT_SET(XdpTxQueueGetStats(TxQueue), QueueDepth, TxLimit - TxAvailable);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushTransmit(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    )
{
    XDP_TX_QUEUE *TxQueue = CONTAINING_RECORD(XdpTxQueue, XDP_TX_QUEUE, Dispatch);

    XdbgEnterQueueEc(TxQueue);
    STAT_INC(XdpTxQueueGetStats(TxQueue), InjectionBatches);

    XdpTxQueueDatapathComplete(TxQueue);
    XdpTxQueueDatapathFill(TxQueue);

    XdpQueueDatapathSync(&TxQueue->Sync);

    XdbgFlushQueueEc(TxQueue);
    XdbgExitQueueEc(TxQueue);
}

XDP_PCW_TX_QUEUE *
XdpTxQueueGetStats(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return &TxQueue->PcwStats;
}

static const XDP_TX_QUEUE_DISPATCH XdpTxDispatch = {
    .FlushTransmit = XdpFlushTransmit,
};

//
// Control path routines.
//

static
VOID
XdpTxQueueInterlockedReference(
    _Inout_ XDP_TX_QUEUE *TxQueue
    )
{
    //
    // Unlike XdpTxQueueReference, this reference count can be called from
    // any thread context and protects only the XDP_TX_QUEUE pool allocation.
    //
    XdpIncrementReferenceCount(&TxQueue->InterlockedReferenceCount);
}

static
VOID
XdpTxQueueInterlockedDereference(
    _Inout_ XDP_TX_QUEUE *TxQueue
    )
{
    //
    // Unlike XdpTxQueueDereference, this reference count can be
    // called from any thread context and protects only the XDP_TX_QUEUE pool
    // allocation.
    //
    if (XdpDecrementReferenceCount(&TxQueue->InterlockedReferenceCount)) {
        ExFreePoolWithTag(TxQueue, XDP_POOLTAG_TXQUEUE);
    }
}

static const XDP_EXTENSION_REGISTRATION XdpTxFrameExtensions[] = {
    {
        .Info.ExtensionName     = XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
        .Info.ExtensionVersion  = XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_TX_FRAME_COMPLETION_CONTEXT),
        .Alignment              = __alignof(XDP_TX_FRAME_COMPLETION_CONTEXT),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = 0,
        .Alignment              = __alignof(UCHAR),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_LAYOUT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_LAYOUT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_FRAME_LAYOUT),
        .Alignment              = __alignof(XDP_FRAME_LAYOUT),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_CHECKSUM_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_CHECKSUM_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_FRAME_CHECKSUM),
        .Alignment              = __alignof(XDP_FRAME_CHECKSUM),
    },
};

static const XDP_EXTENSION_REGISTRATION XdpTxBufferExtensions[] = {
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = sizeof(XDP_BUFFER_VIRTUAL_ADDRESS),
        .Alignment              = __alignof(XDP_BUFFER_VIRTUAL_ADDRESS),
    },
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = sizeof(XDP_BUFFER_LOGICAL_ADDRESS),
        .Alignment              = __alignof(XDP_BUFFER_LOGICAL_ADDRESS),
    },
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_MDL_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_MDL_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = sizeof(XDP_BUFFER_MDL),
        .Alignment              = __alignof(XDP_BUFFER_MDL),
    },
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = 0,
        .Alignment              = __alignof(UCHAR),
    },
};

static const XDP_EXTENSION_REGISTRATION XdpTxFrameCompletionExtensions[] = {
    {
        .Info.ExtensionName     = XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
        .Info.ExtensionVersion  = XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION,
        .Size                   = sizeof(XDP_TX_FRAME_COMPLETION_CONTEXT),
        .Alignment              = __alignof(XDP_TX_FRAME_COMPLETION_CONTEXT),
    },
};

static
XDP_TX_QUEUE *
XdpTxQueueFromConfigCreate(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    return CONTAINING_RECORD(TxQueueConfig, XDP_TX_QUEUE, ConfigCreate);
}

static
XDP_EXTENSION_SET *
XdpTxQueueGetExtensionSet(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_EXTENSION_TYPE ExtensionType
    )
{
    XDP_EXTENSION_SET *ExtensionSet;

    switch (ExtensionType) {

    case XDP_EXTENSION_TYPE_FRAME:
        ExtensionSet = TxQueue->FrameExtensionSet;
        break;

    case XDP_EXTENSION_TYPE_BUFFER:
        ExtensionSet = TxQueue->BufferExtensionSet;
        break;

    case XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION:
        ExtensionSet = TxQueue->TxFrameCompletionExtensionSet;
        break;

    default:
        FRE_ASSERT(FALSE);

    }

    return ExtensionSet;
}

CONST XDP_QUEUE_INFO *
XdpTxQueueGetTargetQueueInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);

    return &TxQueue->QueueInfo;
}

VOID
XdpTxQueueSetCapabilities(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);

    //
    // TODO: Some capabilities are not implemented.
    //
    FRE_ASSERT(Capabilities->Header.Revision >= XDP_TX_CAPABILITIES_REVISION_1);
    FRE_ASSERT(Capabilities->Header.Size >= XDP_SIZEOF_TX_CAPABILITIES_REVISION_1);
    FRE_ASSERT(Capabilities->MaximumFragments <= 1);
    FRE_ASSERT(
        Capabilities->VirtualAddressEnabled ||
        Capabilities->MdlEnabled ||
        Capabilities->DmaCapabilities != NULL);

    RtlCopyMemory(
        &TxQueue->InterfaceTxCapabilities, Capabilities,
        min(Capabilities->Header.Size, sizeof(TxQueue->InterfaceTxCapabilities)));

    if (Capabilities->VirtualAddressEnabled) {
        XdpExtensionSetEnableEntry(TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME);
    }

    if (Capabilities->MdlEnabled) {
        XdpExtensionSetEnableEntry(TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_MDL_NAME);
    }

    if (Capabilities->DmaCapabilities != NULL) {
        FRE_ASSERT(Capabilities->DmaCapabilities->PhysicalDeviceObject != NULL);
        TxQueue->InterfaceDmaCapabilities = *Capabilities->DmaCapabilities;
        TxQueue->InterfaceTxCapabilities.DmaCapabilities = &TxQueue->InterfaceDmaCapabilities;
        XdpExtensionSetEnableEntry(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME);
    }
}

VOID
XdpTxQueueRegisterExtensionVersion(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    )
{
    FRE_ASSERT(ExtensionInfo->Header.Revision >= XDP_EXTENSION_INFO_REVISION_1);
    FRE_ASSERT(ExtensionInfo->Header.Size >= XDP_SIZEOF_EXTENSION_INFO_REVISION_1);

    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);
    XDP_EXTENSION_SET *Set = XdpTxQueueGetExtensionSet(TxQueue, ExtensionInfo->ExtensionType);

    TraceInfo(
        TRACE_CORE,
        "TxQueue=%p ExtensionName=%S ExtensionVersion=%u ExtensionType=%!EXTENSION_TYPE!",
        TxQueue, ExtensionInfo->ExtensionName, ExtensionInfo->ExtensionVersion,
        ExtensionInfo->ExtensionType);

    XdpExtensionSetRegisterEntry(Set, ExtensionInfo);
}

VOID
XdpTxQueueSetDescriptorContexts(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_DESCRIPTOR_CONTEXTS *Descriptors
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);

    FRE_ASSERT(Descriptors->Header.Revision >= XDP_TX_DESCRIPTOR_CONTEXTS_REVISION_1);
    FRE_ASSERT(Descriptors->Header.Size >= XDP_SIZEOF_TX_DESCRIPTOR_CONTEXTS_REVISION_1);

    FRE_ASSERT((Descriptors->FrameContextSize == 0) == (Descriptors->FrameContextAlignment == 0));
    FRE_ASSERT((Descriptors->BufferContextSize == 0) == (Descriptors->BufferContextAlignment == 0));

    if (Descriptors->FrameContextSize > 0) {
        XdpExtensionSetResizeEntry(
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
            Descriptors->FrameContextSize, Descriptors->FrameContextAlignment);
        XdpExtensionSetEnableEntry(
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME);
    }

    if (Descriptors->BufferContextSize > 0) {
        XdpExtensionSetResizeEntry(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME,
            Descriptors->BufferContextSize, Descriptors->BufferContextAlignment);
        XdpExtensionSetEnableEntry(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME);
    }
}

VOID
XdpTxQueueSetPollInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    )
{
    FRE_ASSERT(PollInfo->Header.Revision >= XDP_POLL_INFO_REVISION_1);
    FRE_ASSERT(PollInfo->Header.Size >= XDP_SIZEOF_POLL_INFO_REVISION_1);

    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);
    TxQueue->InterfacePollHandle = PollInfo->PollHandle;
}

XDP_TX_QUEUE *
XdpTxQueueFromConfigActivate(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    return CONTAINING_RECORD(TxQueueConfig, XDP_TX_QUEUE, ConfigActivate);
}

XDP_RING *
XdpTxQueueGetFrameRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return TxQueue->FrameRing;
}

XDP_RING *
XdpTxQueueGetFragmentRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    UNREFERENCED_PARAMETER(TxQueueConfig);

    //
    // TODO: Not implemented.
    //
    FRE_ASSERT(FALSE);
}

XDP_RING *
XdpTxQueueGetCompletionRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return TxQueue->CompletionRing;
}

VOID
XdpTxQueueGetExtension(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    )
{
    FRE_ASSERT(ExtensionInfo->Header.Revision >= XDP_EXTENSION_INFO_REVISION_1);
    FRE_ASSERT(ExtensionInfo->Header.Size >= XDP_SIZEOF_EXTENSION_INFO_REVISION_1);

    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);
    XDP_EXTENSION_SET *Set = XdpTxQueueGetExtensionSet(TxQueue, ExtensionInfo->ExtensionType);

    XdpExtensionSetGetExtension(Set, ExtensionInfo, Extension);
}

BOOLEAN
XdpTxQueueIsTxCompletionContextEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            TxQueue->FrameExtensionSet, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME);
}

BOOLEAN
XdpTxQueueIsFragmentationEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    UNREFERENCED_PARAMETER(TxQueueConfig);

    return FALSE;
}

BOOLEAN
XdpTxQueueIsOutOfOrderCompletionEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return TxQueue->InterfaceTxCapabilities.OutOfOrderCompletionEnabled;
}

BOOLEAN
XdpTxQueueIsChecksumOffloadEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return TxQueue->IsChecksumOffloadEnabled;
}

static
CONST XDP_HOOK_ID *
XdppTxQueueGetHookId(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);

    return &TxQueue->Key.HookId;
}

static
XDP_TX_QUEUE_NOTIFY_HANDLE
XdppTxQueueGetNotifyHandle(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigCreate(TxQueueConfig);

    return (XDP_TX_QUEUE_NOTIFY_HANDLE)&TxQueue->Notify.Details;
}

static
XDP_TX_QUEUE *
XdpTxQueueFromNotify(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle
    )
{
    return CONTAINING_RECORD(TxQueueNotifyHandle, XDP_TX_QUEUE, Notify.Details);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpTxQueueNotifyClients(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_TX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    LIST_ENTRY *Entry = TxQueue->Notify.Clients.Flink;

    TraceInfo(
        TRACE_CORE, "TxQueue=%p NotificationType=%!TX_QUEUE_NOTIFICATION_TYPE!",
        TxQueue, NotificationType);

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    while (Entry != &TxQueue->Notify.Clients) {
        XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry;

        NotifyEntry = CONTAINING_RECORD(Entry, XDP_TX_QUEUE_NOTIFICATION_ENTRY, Link);
        Entry = Entry->Flink;

        NotifyEntry->NotifyRoutine(NotifyEntry, NotificationType);
    }
}

static
_Requires_lock_held_(TxQueue->Notify.Lock)
VOID
XdpTxQueueNotifyClientsUnderNotifyLock(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_TX_QUEUE_NOTIFICATION_TYPE NotificationType,
    _In_ _IRQL_saves_ _IRQL_restores_ KIRQL *OldIrql
    )
{
    //
    // N.B. This routine releases and re-acquires the notify lock.
    //
    ASSERT(*OldIrql == PASSIVE_LEVEL);
    KeReleaseSpinLock(&TxQueue->Notify.Lock, *OldIrql);
    XdpTxQueueNotifyClients(TxQueue, NotificationType);
    KeAcquireSpinLock(&TxQueue->Notify.Lock, OldIrql);
}

static
VOID
XdpTxQueueNotifyWorker(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_TX_QUEUE *TxQueue = CONTAINING_RECORD(Item, XDP_TX_QUEUE, Notify.WorkItem);
    KIRQL OldIrql;

    KeAcquireSpinLock(&TxQueue->Notify.Lock, &OldIrql);

    ASSERT(TxQueue->Notify.WorkerQueued);

    while (TRUE) {
        if (TxQueue->Notify.DeleteNeeded) {
            TxQueue->Notify.DeleteNeeded = FALSE;
            XdpTxQueueNotifyClientsUnderNotifyLock(
                TxQueue, XDP_TX_QUEUE_NOTIFICATION_DETACH, &OldIrql);
        } else if (TxQueue->Notify.OffloadNeeded) {
            TxQueue->Notify.OffloadNeeded = FALSE;
            XdpTxQueueNotifyClientsUnderNotifyLock(
                TxQueue, XDP_TX_QUEUE_NOTIFICATION_OFFLOAD_CURRENT_CONFIG, &OldIrql);
        } else {
            break;
        }
    }

    TxQueue->Notify.WorkerQueued = FALSE;

    KeReleaseSpinLock(&TxQueue->Notify.Lock, OldIrql);

    XdpTxQueueInterlockedDereference(TxQueue);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueNotify(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle,
    _In_ XDP_TX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ const VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromNotify(TxQueueNotifyHandle);
    KIRQL OldIrql;
    BOOLEAN NeedNotification = FALSE;

    //
    // This routine can be invoked from arbitrary contexts, including passive
    // worker threads and data paths at up to dispatch level.
    //

    KeAcquireSpinLock(&TxQueue->Notify.Lock, &OldIrql);

    switch (NotifyCode) {
    case XDP_TX_QUEUE_NOTIFY_MAX_FRAME_SIZE:
        FRE_ASSERT(NotifyBufferSize == sizeof(UINT32));
        ASSERT(NotifyBuffer);
        FRE_ASSERT(*(UINT32 *)NotifyBuffer > 0);

        TraceVerbose(TRACE_CORE, "TxQueue=%p MTU=%u", TxQueue, *(CONST UINT32 *)NotifyBuffer);

        //
        // Similar to NetAdapter, changing MTU after a queue is created causes
        // the queue to be torn down, and perhaps re-created.
        //
        TxQueue->Notify.DeleteNeeded = TRUE;
        NeedNotification = TRUE;
        break;

    case XDP_TX_QUEUE_NOTIFY_OFFLOAD_CURRENT_CONFIG:
        TraceVerbose(TRACE_CORE, "TxQueue=%p Offload=%p", TxQueue, NotifyBuffer);

        TxQueue->Notify.OffloadNeeded = TRUE;
        NeedNotification = TRUE;
        break;
    }

    if (NeedNotification) {
        if (!TxQueue->Notify.WorkerQueued) {
            TxQueue->Notify.WorkerQueued = TRUE;
            TxQueue->Notify.WorkItem.BindingHandle = TxQueue->Binding;
            TxQueue->Notify.WorkItem.WorkRoutine = XdpTxQueueNotifyWorker;
            XdpTxQueueInterlockedReference(TxQueue);
            XdpIfQueueWorkItem(&TxQueue->Notify.WorkItem);
        }
    }

    KeReleaseSpinLock(&TxQueue->Notify.Lock, OldIrql);
}

static const XDP_TX_QUEUE_CONFIG_RESERVED XdpTxConfigReservedDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_CONFIG_RESERVED_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_CONFIG_RESERVED_REVISION_1
    },
    .GetHookId                      = XdppTxQueueGetHookId,
    .GetNotifyHandle                = XdppTxQueueGetNotifyHandle,
};

static const XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH XdpTxConfigCreateDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1
    },
    .Reserved                       = &XdpTxConfigReservedDispatch,
    .GetTargetQueueInfo             = XdpTxQueueGetTargetQueueInfo,
    .SetTxQueueCapabilities         = XdpTxQueueSetCapabilities,
    .RegisterExtensionVersion       = XdpTxQueueRegisterExtensionVersion,
    .SetTxDescriptorContexts        = XdpTxQueueSetDescriptorContexts,
    .SetPollInfo                    = XdpTxQueueSetPollInfo,
};

static const XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH XdpTxConfigActivateDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1,
        .Size                       = sizeof(XdpTxConfigActivateDispatch),
    },
    .GetFrameRing                   = XdpTxQueueGetFrameRing,
    .GetFragmentRing                = XdpTxQueueGetFragmentRing,
    .GetCompletionRing              = XdpTxQueueGetCompletionRing,
    .GetExtension                   = XdpTxQueueGetExtension,
    .IsTxCompletionContextEnabled   = XdpTxQueueIsTxCompletionContextEnabled,
    .IsFragmentationEnabled         = XdpTxQueueIsFragmentationEnabled,
    .IsOutOfOrderCompletionEnabled  = XdpTxQueueIsOutOfOrderCompletionEnabled,
    .IsChecksumOffloadEnabled       = XdpTxQueueIsChecksumOffloadEnabled,
};

static const XDP_TX_QUEUE_NOTIFY_DISPATCH XdpTxNotifyDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_NOTIFY_DISPATCH_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_NOTIFY_DISPATCH_REVISION_1
    },
    .Notify                         = XdpTxQueueNotify,
};

static
XDP_TX_QUEUE *
XdpTxQueueFromBindingEntry(
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    return CONTAINING_RECORD(ClientEntry, XDP_TX_QUEUE, BindingClientEntry);
}

static
VOID
XdpTxQueueDetachEvent(
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromBindingEntry(ClientEntry);

    XdpTxQueueNotifyClients(TxQueue, XDP_TX_QUEUE_NOTIFICATION_DETACH);
}

static
CONST
XDP_BINDING_CLIENT TxQueueBindingClient = {
    .ClientId           = XDP_BINDING_CLIENT_ID_TX_QUEUE,
    .KeySize            = sizeof(XDP_TX_QUEUE_KEY),
    .BindingDetached    = XdpTxQueueDetachEvent,
};

static
VOID
XdpTxQueueInitializeKey(
    _Out_ XDP_TX_QUEUE_KEY *Key,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    )
{
    RtlZeroMemory(Key, sizeof(*Key));
    Key->HookId = *HookId;
    Key->QueueId = QueueId;
}

static
NTSTATUS
XdpTxQueueCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_TX_QUEUE **NewTxQueue
    )
{
    XDP_TX_QUEUE_KEY Key;
    XDP_TX_QUEUE *TxQueue = NULL;
    NTSTATUS Status;
    DECLARE_UNICODE_STRING_SIZE(
        Name, ARRAYSIZE("if_" MAXUINT32_STR "_queue_" MAXUINT32_STR "_rx"));
    const WCHAR *DirectionString;

    *NewTxQueue = NULL;

    if (HookId->SubLayer != XDP_HOOK_INJECT) {
        //
        // Chaining injection queues onto inspection queues is not implemented.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (!NT_VERIFY(XdpIfSupportsHookId(XdpIfGetCapabilities(Binding), HookId))) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (HookId->Direction == XDP_HOOK_RX) {
        DirectionString = L"_rx";
    } else {
        ASSERT(HookId->Direction == XDP_HOOK_TX);
        DirectionString = L"";
    }

    XdpTxQueueInitializeKey(&Key, HookId, QueueId);
    if (!NT_VERIFY(XdpIfFindClientEntry(Binding, &TxQueueBindingClient, &Key) == NULL)) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    TxQueue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*TxQueue), XDP_POOLTAG_TXQUEUE);
    if (TxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&TxQueue->InterlockedReferenceCount);
    XdpInitializeReferenceCount(&TxQueue->ReferenceCount);
    TxQueue->Binding = Binding;
    TxQueue->Key = Key;
    TxQueue->State = XdpTxQueueStateCreated;
    XdpIfInitializeClientEntry(&TxQueue->BindingClientEntry);
    KeInitializeSpinLock(&TxQueue->Notify.Lock);
    InitializeListHead(&TxQueue->Notify.Clients);
    XdpInitializeQueueInfo(&TxQueue->QueueInfo, XDP_QUEUE_TYPE_DEFAULT_RSS, QueueId);
    InitializeListHead(&TxQueue->ClientList);
    TxQueue->FillEntry = &TxQueue->ClientList;
    XdpQueueSyncInitialize(&TxQueue->Sync);
    XdbgInitializeQueueEc(TxQueue);

    TxQueue->Dispatch = XdpTxDispatch;
    TxQueue->ConfigCreate.Dispatch = &XdpTxConfigCreateDispatch;
    TxQueue->ConfigActivate.Dispatch = &XdpTxConfigActivateDispatch;
    TxQueue->Notify.Details.Dispatch = &XdpTxNotifyDispatch;

    Status =
        XdpIfRegisterClient(
            Binding, &TxQueueBindingClient, &TxQueue->Key, &TxQueue->BindingClientEntry);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        RtlUnicodeStringPrintf(
            &Name, L"if_%u_queue_%u%s", XdpIfGetIfIndex(Binding), QueueId, DirectionString);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpPcwCreateTxQueue(&TxQueue->PcwInstance, &Name, &TxQueue->PcwStats);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetCreate(
            XDP_EXTENSION_TYPE_FRAME, XdpTxFrameExtensions, RTL_NUMBER_OF(XdpTxFrameExtensions),
            &TxQueue->FrameExtensionSet);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetCreate(
            XDP_EXTENSION_TYPE_BUFFER, XdpTxBufferExtensions, RTL_NUMBER_OF(XdpTxBufferExtensions),
            &TxQueue->BufferExtensionSet);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetCreate(
            XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION, XdpTxFrameCompletionExtensions,
            RTL_NUMBER_OF(XdpTxFrameCompletionExtensions), &TxQueue->TxFrameCompletionExtensionSet);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpIfCreateTxQueue(
            Binding, (XDP_TX_QUEUE_CONFIG_CREATE)&TxQueue->ConfigCreate,
            &TxQueue->InterfaceTxQueue, &TxQueue->InterfaceTxDispatch);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Ensure the interface driver has registered its capabilities.
    //
    FRE_ASSERT(TxQueue->InterfaceTxCapabilities.Header.Revision >= XDP_TX_CAPABILITIES_REVISION_1);
    FRE_ASSERT(TxQueue->InterfaceTxCapabilities.Header.Size >= XDP_SIZEOF_TX_CAPABILITIES_REVISION_1);

    //
    // We don't support exclusive TX queues yet, so all TX queues need
    // completion contexts enabled.
    //
    XdpExtensionSetEnableEntry(
        TxQueue->FrameExtensionSet, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME);
    XdpExtensionSetEnableEntry(
        TxQueue->TxFrameCompletionExtensionSet, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME);

    if (!TxQueue->InterfaceTxCapabilities.OutOfOrderCompletionEnabled) {
        //
        // The frame completion extension is only used by XDP itself for in-order
        // TX completions.
        //
        XdpExtensionSetSetInternalEntry(
            TxQueue->FrameExtensionSet, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME);
    }

    Status =
        XdpIfOpenInterfaceOffloadHandle(
            XdpIfGetIfSetHandle(TxQueue->Binding), &TxQueue->Key.HookId,
            &TxQueue->InterfaceOffloadHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *NewTxQueue = TxQueue;
    Status = STATUS_SUCCESS;

Exit:

    TraceInfo(
        TRACE_CORE,
        "TxQueue=%p Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} Status=%!STATUS!",
        TxQueue, HookId->Layer, HookId->Direction, HookId->SubLayer, Status);

    if (!NT_SUCCESS(Status)) {
        if (TxQueue != NULL) {
            XdpTxQueueDereference(TxQueue);
        }
    }

    return Status;
}

static
VOID
XdpTxQueueDeleteRings(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    ASSERT(TxQueue->State == XdpTxQueueStateCreated || TxQueue->State == XdpTxQueueStateDeleted);

    if (TxQueue->CompletionRing != NULL) {
        XdpRingFreeRing(TxQueue->CompletionRing);
        TxQueue->CompletionRing = NULL;
    }
    if (TxQueue->FrameRing != NULL) {
        XdpRingFreeRing(TxQueue->FrameRing);
        TxQueue->FrameRing = NULL;
    }
}

NTSTATUS
XdpTxQueueActivate(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    NTSTATUS Status;
    UINT32 BufferSize, FrameSize, FrameOffset, FrameCount, TxCompletionSize;
    UINT8 BufferAlignment, FrameAlignment, TxCompletionAlignment;
    BOOLEAN AssigningLayouts = FALSE;
    XDP_EXTENSION_INFO ExtensionInfo;

    TraceEnter(TRACE_CORE, "TxQueue=%p", TxQueue);

    if (TxQueue->State == XdpTxQueueStateActive) {
        //
        // Already activated - this is a no-op.
        //
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (TxQueue->State != XdpTxQueueStateCreated) {
        //
        // The queue can be activated only from its original created state.
        //
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    AssigningLayouts = TRUE;

    Status =
        XdpExtensionSetAssignLayout(
            TxQueue->BufferExtensionSet, sizeof(XDP_BUFFER), __alignof(XDP_BUFFER),
            &BufferSize, &BufferAlignment);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlUInt32Add(FIELD_OFFSET(XDP_FRAME, Buffer), BufferSize, &FrameOffset);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetAssignLayout(
            TxQueue->FrameExtensionSet, FrameOffset, max(__alignof(XDP_FRAME), BufferAlignment),
            &FrameSize, &FrameAlignment);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        RtlUInt32RoundUpToPowerOfTwo(
            max(XdpTxRingSize, TxQueue->InterfaceTxCapabilities.TransmitFrameCountHint),
            &FrameCount);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpRingAllocate(FrameSize, FrameCount, FrameAlignment, &TxQueue->FrameRing);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (TxQueue->InterfaceTxCapabilities.OutOfOrderCompletionEnabled) {
        Status =
            XdpExtensionSetAssignLayout(
                TxQueue->TxFrameCompletionExtensionSet, sizeof(XDP_TX_FRAME_COMPLETION),
                __alignof(XDP_TX_FRAME_COMPLETION), &TxCompletionSize, &TxCompletionAlignment);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            XdpRingAllocate(
                TxCompletionSize, FrameCount, TxCompletionAlignment, &TxQueue->CompletionRing);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
            XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
            XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION);
        XdpTxQueueGetExtension(
            (XDP_TX_QUEUE_CONFIG_ACTIVATE)&TxQueue->ConfigActivate, &ExtensionInfo,
            &TxQueue->TxCompletionContextExtension);
    } else {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME,
            XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1,
            XDP_EXTENSION_TYPE_FRAME);
        XdpTxQueueGetExtension(
            (XDP_TX_QUEUE_CONFIG_ACTIVATE)&TxQueue->ConfigActivate, &ExtensionInfo,
            &TxQueue->TxCompletionContextExtension);
    }

    Status =
        XdpIfActivateTxQueue(
            TxQueue->Binding, TxQueue->InterfaceTxQueue,
            (XDP_TX_QUEUE_HANDLE)&TxQueue->Dispatch,
            (XDP_TX_QUEUE_CONFIG_ACTIVATE)&TxQueue->ConfigActivate);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "TxQueue=%p XdpIfActivateTxQueue failed Status=%!STATUS!",
            TxQueue, Status);
        goto Exit;
    }

    TxQueue->State = XdpTxQueueStateActive;
    TraceInfo(TRACE_CORE, "TxQueue=%p Activated", TxQueue);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (AssigningLayouts) {
            if (TxQueue->InterfaceTxCapabilities.OutOfOrderCompletionEnabled) {
                XdpExtensionSetResetLayout(TxQueue->TxFrameCompletionExtensionSet);
            }
            XdpExtensionSetResetLayout(TxQueue->FrameExtensionSet);
            XdpExtensionSetResetLayout(TxQueue->BufferExtensionSet);

        }
        XdpTxQueueDeleteRings(TxQueue);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
VOID
XdpTxQueueReference(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    XdpIncrementReferenceCount(&TxQueue->ReferenceCount);
}

static
XDP_TX_QUEUE *
XdpTxQueueFind(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    )
{
    XDP_TX_QUEUE_KEY Key;
    XDP_BINDING_CLIENT_ENTRY *ClientEntry;
    XDP_TX_QUEUE *TxQueue;

    XdpTxQueueInitializeKey(&Key, HookId, QueueId);
    ClientEntry = XdpIfFindClientEntry(Binding, &TxQueueBindingClient, &Key);
    if (ClientEntry == NULL) {
        return NULL;
    }

    TxQueue = XdpTxQueueFromBindingEntry(ClientEntry);
    XdpTxQueueReference(TxQueue);

    return TxQueue;
}

NTSTATUS
XdpTxQueueFindOrCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_TX_QUEUE **TxQueue
    )
{
    *TxQueue = XdpTxQueueFind(Binding, HookId, QueueId);
    if (*TxQueue != NULL) {
        return STATUS_SUCCESS;
    }

    return XdpTxQueueCreate(Binding, HookId, QueueId, TxQueue);
}

VOID
XdpTxQueueRegisterNotifications(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry,
    _In_ XDP_TX_QUEUE_NOTIFICATION_ROUTINE *NotifyRoutine
    )
{
    NotifyEntry->NotifyRoutine = NotifyRoutine;
    InsertTailList(&TxQueue->Notify.Clients, &NotifyEntry->Link);
}

VOID
XdpTxQueueDeregisterNotifications(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry
    )
{
    UNREFERENCED_PARAMETER(TxQueue);

    RemoveEntryList(&NotifyEntry->Link);
}

NTSTATUS
XdpTxQueueEnableChecksumOffload(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "TxQueue=%p", TxQueue);

    if (TxQueue->IsChecksumOffloadEnabled) {
        ASSERT(XdpExtensionSetIsExtensionEnabled(
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_LAYOUT_NAME));
        ASSERT(XdpExtensionSetIsExtensionEnabled(
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_CHECKSUM_NAME));
        Status = STATUS_SUCCESS;
    } else if (TxQueue->State == XdpTxQueueStateCreated) {
        if (TxQueue->InterfaceTxCapabilities.ChecksumOffload) {
            XdpExtensionSetEnableEntry(
                TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_LAYOUT_NAME);
            XdpExtensionSetEnableEntry(
                TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_CHECKSUM_NAME);
            TxQueue->IsChecksumOffloadEnabled = TRUE;
            Status = STATUS_SUCCESS;
        } else {
            Status = STATUS_NOT_SUPPORTED;
        }
    } else {
        Status = STATUS_INVALID_DEVICE_STATE;
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpTxQueueSync(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    )
{
    XDP_QUEUE_BLOCKING_SYNC_CONTEXT SyncEntry = {0};
    XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_TX_FLUSH;

    //
    // Serialize a callback with the datapath execution context. This routine
    // must be called from the interface binding thread.
    //

    if (TxQueue->State != XdpTxQueueStateActive) {
        //
        // If the TX queue is not active (i.e. the XDP data path cannot be
        // invoked by interfaces), simply invoke the callback.
        //
        Callback(CallbackContext);
        return;
    }

    XdpQueueBlockingSyncInsert(&TxQueue->Sync, &SyncEntry, Callback, CallbackContext);

    XdpTxQueueInvokeInterfaceNotify(TxQueue, NotifyFlags);

    KeWaitForSingleObject(&SyncEntry.Event, Executive, KernelMode, FALSE, NULL);
}

typedef struct _XDP_TX_QUEUE_SYNC_ADD_CLIENT {
    XDP_TX_QUEUE *TxQueue;
    XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry;
} XDP_TX_QUEUE_SYNC_ADD_CLIENT;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueSyncAddDatapathClient(
    _In_opt_ VOID *Context
    )
{
    XDP_TX_QUEUE_SYNC_ADD_CLIENT *Params = Context;

    ASSERT(Params != NULL);

    InsertTailList(&Params->TxQueue->ClientList, &Params->TxClientEntry->Link);
}

NTSTATUS
XdpTxQueueAddDatapathClient(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry,
    _In_ XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE TxClientType
    )
{
    XDP_TX_QUEUE_SYNC_ADD_CLIENT SyncParams = {0};
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "TxQueue=%p TxClientEntry=%p", TxQueue, TxClientEntry);

    ASSERT(TxClientType == XDP_TX_QUEUE_DATAPATH_CLIENT_TYPE_XSK);
    UNREFERENCED_PARAMETER(TxClientType);

    if (TxQueue->State != XdpTxQueueStateActive) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    SyncParams.TxQueue = TxQueue;
    SyncParams.TxClientEntry = TxClientEntry;
    XdpTxQueueSync(TxQueue, XdpTxQueueSyncAddDatapathClient, &SyncParams);

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

typedef struct _XDP_TX_QUEUE_SYNC_REMOVE_CLIENT {
    XDP_TX_QUEUE *TxQueue;
    XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry;
} XDP_TX_QUEUE_SYNC_REMOVE_CLIENT;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueSyncRemoveDatapathClient(
    _In_opt_ VOID *Context
    )
{
    XDP_TX_QUEUE_SYNC_REMOVE_CLIENT *Params = Context;

    ASSERT(Params != NULL);
    ASSERT(!IsListEmpty(&Params->TxClientEntry->Link));

    if (Params->TxQueue->FillEntry == &Params->TxClientEntry->Link) {
        Params->TxQueue->FillEntry = Params->TxClientEntry->Link.Flink;
    }

    RemoveEntryList(&Params->TxClientEntry->Link);
    InitializeListHead(&Params->TxClientEntry->Link);
}

VOID
XdpTxQueueRemoveDatapathClient(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Inout_ XDP_TX_QUEUE_DATAPATH_CLIENT_ENTRY *TxClientEntry
    )
{
    XDP_TX_QUEUE_SYNC_REMOVE_CLIENT SyncParams = {0};

    TraceEnter(TRACE_CORE, "TxQueue=%p TxClientEntry=%p", TxQueue, TxClientEntry);

    SyncParams.TxQueue = TxQueue;
    SyncParams.TxClientEntry = TxClientEntry;
    XdpTxQueueSync(TxQueue, XdpTxQueueSyncRemoveDatapathClient, &SyncParams);

    TraceExitSuccess(TRACE_CORE);
}

CONST XDP_TX_CAPABILITIES *
XdpTxQueueGetCapabilities(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return &TxQueue->InterfaceTxCapabilities;
}

CONST XDP_DMA_CAPABILITIES *
XdpTxQueueGetDmaCapabilities(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return &TxQueue->InterfaceDmaCapabilities;
}

NDIS_HANDLE
XdpTxQueueGetInterfacePollHandle(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return TxQueue->InterfacePollHandle;
}

XDP_IF_OFFLOAD_HANDLE
XdpTxQueueGetInterfaceOffloadHandle(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return TxQueue->InterfaceOffloadHandle;
}

XDP_TX_QUEUE_CONFIG_ACTIVATE
XdpTxQueueGetConfig(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    return (XDP_TX_QUEUE_CONFIG_ACTIVATE)&TxQueue->ConfigActivate;
}

BOOLEAN
XdpTxQueueIsVirtualAddressEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME);
}

BOOLEAN
XdpTxQueueIsLogicalAddressEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME);
}

BOOLEAN
XdpTxQueueIsMdlEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            TxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_MDL_NAME);
}

BOOLEAN
XdpTxQueueIsLayoutExtensionEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromConfigActivate(TxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_LAYOUT_NAME);
}

static
VOID
XdpTxQueueDelete(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    TraceEnter(TRACE_CORE, "TxQueue=%p", TxQueue);
    TraceInfo(TRACE_CORE, "Deleting TxQueue=%p", TxQueue);

    ASSERT(IsListEmpty(&TxQueue->Notify.Clients));
    ASSERT(IsListEmpty(&TxQueue->ClientList));

    if (TxQueue->InterfaceOffloadHandle != NULL) {
        XdpIfCloseInterfaceOffloadHandle(
            XdpIfGetIfSetHandle(TxQueue->Binding), TxQueue->InterfaceOffloadHandle);
        TxQueue->InterfaceOffloadHandle = NULL;
    }

    if (TxQueue->InterfaceTxQueue != NULL) {
        XdpIfDeleteTxQueue(TxQueue->Binding, TxQueue->InterfaceTxQueue);
    }

    TxQueue->State = XdpTxQueueStateDeleted;

    XdpTxQueueDeleteRings(TxQueue);
    if (TxQueue->TxFrameCompletionExtensionSet != NULL) {
        XdpExtensionSetCleanup(TxQueue->TxFrameCompletionExtensionSet);
    }
    if (TxQueue->BufferExtensionSet != NULL) {
        XdpExtensionSetCleanup(TxQueue->BufferExtensionSet);
    }
    if (TxQueue->FrameExtensionSet != NULL) {
        XdpExtensionSetCleanup(TxQueue->FrameExtensionSet);
    }

    if (TxQueue->PcwInstance != NULL) {
        PcwCloseInstance(TxQueue->PcwInstance);
        TxQueue->PcwInstance = NULL;
    }

    XdpIfDeregisterClient(TxQueue->Binding, &TxQueue->BindingClientEntry);

    XdpTxQueueInterlockedDereference(TxQueue);
    TraceExitSuccess(TRACE_CORE);
}

VOID
XdpTxQueueDereference(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    if (XdpDecrementReferenceCount(&TxQueue->ReferenceCount)) {
        XdpTxQueueDelete(TxQueue);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpTxRegistryUpdate(
    VOID
    )
{
    NTSTATUS Status;
    DWORD Value;

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpTxRingSize", &Value);
    if (NT_SUCCESS(Status) && RTL_IS_POWER_OF_TWO(Value) && Value >= 8 && Value <= 8192) {
        XdpTxRingSize = Value;
    } else {
        XdpTxRingSize = XDP_DEFAULT_TX_RING_SIZE;
    }
}

NTSTATUS
XdpTxStart(
    VOID
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "-");

    XdpRegWatcherAddClient(XdpRegWatcher, XdpTxRegistryUpdate, &XdpTxRegWatcherEntry);

    Status = XdpPcwRegisterTxQueue(NULL, NULL);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

VOID
XdpTxStop(
    VOID
    )
{
    TraceEnter(TRACE_CORE, "-");

    if (XdpPcwTxQueue != NULL) {
        PcwUnregister(XdpPcwTxQueue);
        XdpPcwTxQueue = NULL;
    }

    XdpRegWatcherRemoveClient(XdpRegWatcher, &XdpTxRegWatcherEntry);

    TraceExitSuccess(TRACE_CORE);
}
