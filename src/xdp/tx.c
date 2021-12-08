//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "tx.tmh"

//
// This module configures XDP transmit queues on interfaces.
//

static XDP_REG_WATCHER_CLIENT_ENTRY XdpTxRegWatcherEntry;
static UINT32 XdpTxRingSize = 32;

typedef struct _XDP_TX_QUEUE_KEY {
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
} XDP_TX_QUEUE_KEY;

typedef struct _XDP_TX_QUEUE {
    XDP_REFERENCE_COUNT ReferenceCount;
    XDP_BINDING_HANDLE Binding;
    XDP_TX_QUEUE_KEY Key;
    XDP_BINDING_CLIENT_ENTRY BindingClientEntry;

    VOID *Client;
    TX_QUEUE_DETACH_EVENT *ClientDetachEvent;

    XDP_TX_CAPABILITIES InterfaceTxCapabilities;
    XDP_DMA_CAPABILITIES InterfaceDmaCapabilities;

    XDP_INTERFACE_HANDLE InterfaceTxQueue;
    XDP_INTERFACE_TX_QUEUE_DISPATCH *InterfaceTxDispatch;
    NDIS_HANDLE InterfacePollHandle;

    XDP_QUEUE_INFO QueueInfo;

    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS ConfigCreate;
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS ConfigActivate;

    VOID *InterfaceOffloadHandle;

    BOOLEAN DeleteNeeded;
    XDP_BINDING_WORKITEM DeleteWorkItem;
    XDP_TX_QUEUE_NOTIFY_DETAILS NotifyDetails;

    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;

    XDP_EXTENSION_SET *FrameExtensionSet;
    XDP_EXTENSION_SET *BufferExtensionSet;

    //
    // TODO: Implement shared TX queues and fix this abstraction.
    //
    XDP_TX_QUEUE_HANDLE ExclusiveTxQueue;
} XDP_TX_QUEUE;

static
VOID
XdpTxQueueReference(
    _Inout_ XDP_TX_QUEUE *TxQueue
    )
{
    XdpIncrementReferenceCount(&TxQueue->ReferenceCount);
}

static
VOID
XdpTxQueueDereference(
    _Inout_ XDP_TX_QUEUE *TxQueue
    )
{
    if (XdpDecrementReferenceCount(&TxQueue->ReferenceCount)) {
        ExFreePoolWithTag(TxQueue, XDP_POOLTAG_TXQUEUE);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushTransmit(
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue
    )
{
    //
    // Use the TX dispatch table, even within XDP.
    //
    XdpFlushTransmitThunk(XdpTxQueue);
}

static CONST XDP_EXTENSION_REGISTRATION XdpTxFrameExtensions[] = {
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_TX_COMPLETION_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_TX_COMPLETION_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_FRAME_TX_COMPLETION_CONTEXT),
        .Alignment              = __alignof(XDP_FRAME_TX_COMPLETION_CONTEXT),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = 0,
        .Alignment              = __alignof(UCHAR),
    },
};

static CONST XDP_EXTENSION_REGISTRATION XdpTxBufferExtensions[] = {
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

    TxQueue->InterfaceTxCapabilities = *Capabilities;

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
            TxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_TX_COMPLETION_CONTEXT_NAME);
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

    return (XDP_TX_QUEUE_NOTIFY_HANDLE)&TxQueue->NotifyDetails;
}

static
XDP_TX_QUEUE *
XdpTxQueueFromNotify(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle
    )
{
    return CONTAINING_RECORD(TxQueueNotifyHandle, XDP_TX_QUEUE, NotifyDetails);
}

typedef enum _XDP_TX_QUEUE_NOTIFICATION_TYPE {
    XDP_TX_QUEUE_NOTIFICATION_DETACH,
} XDP_TX_QUEUE_NOTIFICATION_TYPE;

static
VOID
XdpTxQueueNotifyClients(
    _In_ XDP_TX_QUEUE *TxQueue,
    _In_ XDP_TX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    ASSERT(NotificationType == XDP_TX_QUEUE_NOTIFICATION_DETACH);
    UNREFERENCED_PARAMETER(NotificationType);

    TraceInfo(
        TRACE_CORE, "TxQueue=%p NotificationType=%!TX_QUEUE_NOTIFICATION_TYPE!",
        TxQueue, NotificationType);

    if (TxQueue->ClientDetachEvent != NULL) {
        TX_QUEUE_DETACH_EVENT *ClientDetachEvent = TxQueue->ClientDetachEvent;

        TxQueue->ClientDetachEvent = NULL;
        ClientDetachEvent(TxQueue, TxQueue->Client);
    }
}

static
VOID
XdpTxQueueDeleteWorker(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_TX_QUEUE *TxQueue = CONTAINING_RECORD(Item, XDP_TX_QUEUE, DeleteWorkItem);

    XdpTxQueueNotifyClients(TxQueue, XDP_TX_QUEUE_NOTIFICATION_DETACH);
    XdpTxQueueDereference(TxQueue);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpTxQueueNotify(
    _In_ XDP_TX_QUEUE_NOTIFY_HANDLE TxQueueNotifyHandle,
    _In_ XDP_TX_QUEUE_NOTIFY_CODE NotifyCode,
    _In_opt_ CONST VOID *NotifyBuffer,
    _In_ SIZE_T NotifyBufferSize
    )
{
    XDP_TX_QUEUE *TxQueue = XdpTxQueueFromNotify(TxQueueNotifyHandle);

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
        if (!InterlockedExchange8((CHAR *)&TxQueue->DeleteNeeded, 1)) {
            TxQueue->DeleteWorkItem.BindingHandle = TxQueue->Binding;
            TxQueue->DeleteWorkItem.WorkRoutine = XdpTxQueueDeleteWorker;
            XdpTxQueueReference(TxQueue);
            XdpIfQueueWorkItem(&TxQueue->DeleteWorkItem);
        }
        break;
    }
}

static CONST XDP_TX_QUEUE_CONFIG_RESERVED XdpTxConfigReservedDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_CONFIG_RESERVED_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_CONFIG_RESERVED_REVISION_1
    },
    .GetHookId                      = XdppTxQueueGetHookId,
    .GetNotifyHandle                = XdppTxQueueGetNotifyHandle,
};

static CONST XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH XdpTxConfigCreateDispatch = {
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

static CONST XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH XdpTxConfigActivateDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1
    },
    .GetFrameRing                   = XdpTxQueueGetFrameRing,
    .GetFragmentRing                = XdpTxQueueGetFragmentRing,
    .GetCompletionRing              = XdpTxQueueGetCompletionRing,
    .GetExtension                   = XdpTxQueueGetExtension,
    .IsTxCompletionContextEnabled   = XdpTxQueueIsTxCompletionContextEnabled,
    .IsFragmentationEnabled         = XdpTxQueueIsFragmentationEnabled,
    .IsOutOfOrderCompletionEnabled  = XdpTxQueueIsOutOfOrderCompletionEnabled,
};

static CONST XDP_TX_QUEUE_NOTIFY_DISPATCH XdpTxNotifyDispatch = {
    .Header                         = {
        .Revision                   = XDP_TX_QUEUE_NOTIFY_DISPATCH_REVISION_1,
        .Size                       = XDP_SIZEOF_TX_QUEUE_NOTIFY_DISPATCH_REVISION_1
    },
    .Notify                         = XdpTxQueueNotify,
};

NTSTATUS
XdpTxQueueActivateExclusive(
    _In_ XDP_TX_QUEUE *TxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxDispatch,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue
    )
{
    ASSERT(TxQueue->ExclusiveTxQueue != NULL);

    XdpIfActivateTxQueue(
        TxQueue->Binding, TxQueue->InterfaceTxQueue, TxQueue->ExclusiveTxQueue,
        (XDP_TX_QUEUE_CONFIG_ACTIVATE)&TxQueue->ConfigActivate);

    *InterfaceTxDispatch = TxQueue->InterfaceTxDispatch;
    *InterfaceTxQueue = TxQueue->InterfaceTxQueue;

    return STATUS_SUCCESS;
}

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
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    )
{
    RtlZeroMemory(Key, sizeof(*Key));
    Key->HookId = *HookId;
    Key->QueueId = QueueId;
}

NTSTATUS
XdpTxQueueCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ UINT32 QueueId,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ VOID *Client,
    _In_ TX_QUEUE_DETACH_EVENT *DetachHandler,
    _In_opt_ XDP_TX_QUEUE_HANDLE ExclusiveTxQueue,
    _Out_ XDP_TX_QUEUE **NewTxQueue
    )
{
    XDP_TX_QUEUE_KEY Key;
    XDP_TX_QUEUE *TxQueue = NULL;
    NTSTATUS Status;
    UINT32 BufferSize, FrameSize, FrameOffset, FrameCount;
    UINT8 BufferAlignment, FrameAlignment;

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

    XdpTxQueueInitializeKey(&Key, HookId, QueueId);
    if (XdpIfFindClientEntry(Binding, &TxQueueBindingClient, &Key) != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    TxQueue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*TxQueue), XDP_POOLTAG_TXQUEUE);
    if (TxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    TxQueue->Binding = Binding;
    TxQueue->Key = Key;
    XdpIfInitializeClientEntry(&TxQueue->BindingClientEntry);
    XdpInitializeQueueInfo(&TxQueue->QueueInfo, XDP_QUEUE_TYPE_DEFAULT_RSS, QueueId);
    XdpInitializeReferenceCount(&TxQueue->ReferenceCount);
    TxQueue->Client = Client;
    TxQueue->ClientDetachEvent = DetachHandler;
    TxQueue->ExclusiveTxQueue = ExclusiveTxQueue;
    TxQueue->ConfigCreate.Dispatch = &XdpTxConfigCreateDispatch;
    TxQueue->ConfigActivate.Dispatch = &XdpTxConfigActivateDispatch;
    TxQueue->NotifyDetails.Dispatch = &XdpTxNotifyDispatch;

    Status =
        XdpIfRegisterClient(
            Binding, &TxQueueBindingClient, &TxQueue->Key, &TxQueue->BindingClientEntry);
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
            XdpRingAllocate(sizeof(VOID *), FrameCount, __alignof(VOID *), &TxQueue->CompletionRing);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status =
        XdpIfOpenInterfaceOffloadHandle(
            XdpIfGetIfSetHandle(TxQueue->Binding), &TxQueue->Key.HookId,
            &TxQueue->InterfaceOffloadHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpIfReferenceInterfaceOffload(
            XdpIfGetIfSetHandle(TxQueue->Binding), TxQueue->InterfaceOffloadHandle, XdpOffloadRss);
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
            XdpTxQueueClose(TxQueue);
        }
    }

    return Status;
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

static
VOID
XdpTxQueueDelete(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    if (TxQueue->InterfaceOffloadHandle != NULL) {
        XdpIfCloseInterfaceOffloadHandle(
            XdpIfGetIfSetHandle(TxQueue->Binding), TxQueue->InterfaceOffloadHandle);
        TxQueue->InterfaceOffloadHandle = NULL;
    }

    if (TxQueue->InterfaceTxQueue != NULL) {
        XdpIfDeleteTxQueue(TxQueue->Binding, TxQueue->InterfaceTxQueue);
    }
    if (TxQueue->CompletionRing != NULL) {
        XdpRingFreeRing(TxQueue->CompletionRing);
    }
    if (TxQueue->FrameRing != NULL) {
        XdpRingFreeRing(TxQueue->FrameRing);
    }
    if (TxQueue->BufferExtensionSet != NULL) {
        XdpExtensionSetCleanup(TxQueue->BufferExtensionSet);
    }
    if (TxQueue->FrameExtensionSet != NULL) {
        XdpExtensionSetCleanup(TxQueue->FrameExtensionSet);
    }

    XdpIfDeregisterClient(TxQueue->Binding, &TxQueue->BindingClientEntry);

    XdpTxQueueDereference(TxQueue);
}

VOID
XdpTxQueueClose(
    _In_ XDP_TX_QUEUE *TxQueue
    )
{
    //
    // Currently only one, exclusive client is supported.
    //
    TraceInfo(TRACE_CORE, "Deleting TxQueue=%p", TxQueue);
    XdpTxQueueDelete(TxQueue);
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
    }
}

NTSTATUS
XdpTxStart(
    VOID
    )
{
    XdpTxRegistryUpdate();
    XdpRegWatcherAddClient(XdpRegWatcher, XdpTxRegistryUpdate, &XdpTxRegWatcherEntry);
    return STATUS_SUCCESS;
}

VOID
XdpTxStop(
    VOID
    )
{
    XdpRegWatcherRemoveClient(XdpRegWatcher, &XdpTxRegWatcherEntry);
}
