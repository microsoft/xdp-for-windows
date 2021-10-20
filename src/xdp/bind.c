//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

//
// Implementation of XDP interface binding.
//
// This module provides:
//
// 1. A single abstraction for core XDP modules to manipulate disparate XDP
//    interface types. Core XDP does not need to consider the specifics of OIDs
//    for native XDP over NDIS6 or the generic XDP implementation, etc.
// 2. A single work queue for each interface, since the external-facing XDP
//    control path is serialized. Core XDP components can schedule their own
//    work on this queue, reducing the need for locking schemes across
//    components.
//

#include "precomp.h"
#include "bind.tmh"

typedef struct _XDP_INTERFACE_SET XDP_INTERFACE_SET;
typedef struct _XDP_INTERFACE_NMR XDP_INTERFACE_NMR;

typedef struct _XDP_INTERFACE {
    NET_IFINDEX IfIndex;
    XDP_INTERFACE_SET *IfSet;
    XDP_INTERFACE_HANDLE InterfaceBindingContext;

    CONST XDP_CAPABILITIES_INTERNAL Capabilities;
    XDP_DELETE_BINDING_COMPLETE *DeleteBindingComplete;
    XDP_INTERFACE_CONFIG_DETAILS OpenConfig;

    XDP_INTERFACE_NMR *Nmr;
    XDP_VERSION DriverApiVersion;
    CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch;
    VOID *InterfaceContext;

    LONG ReferenceCount;

    LIST_ENTRY Clients;         // Components bound to the NIC.
    ULONG ProviderReference;    // Active reference on the NIC.

    union {
        struct {
            BOOLEAN BindingDeleting : 1;    // The interface is being deleted.
            BOOLEAN NmrDeleting : 1;        // The NMR binding is being deleted.
        };
        BOOLEAN Rundown;            // Disable new active references on the NIC.
    };

    XDP_WORK_QUEUE *WorkQueue;
    union {
        XDP_BINDING_WORKITEM CloseWorkItem;    // Guaranteed item for close.
        XDP_BINDING_WORKITEM DeleteWorkItem;   // Guaranteed item for delete.
    };
} XDP_INTERFACE;

typedef struct _XDP_INTERFACE_NMR {
    //
    // To support NMR teardown by both XDP interface and XDP platform, this NMR
    // context lasts until the NMR binding is cleaned up (on the worker thread)
    // and the NMR detach notification work item executes (also on the worker
    // thread). The workers may execute in either order; after both have
    // executed, this NMR context is freed.
    //
    XDP_PROVIDER_HANDLE NmrHandle;
    KEVENT DetachNotification;
    UINT32 ReferenceCount;
    XDP_BINDING_WORKITEM WorkItem;
} XDP_INTERFACE_NMR;

typedef struct _XDP_INTERFACE_SET {
    LIST_ENTRY Link;
    NET_IFINDEX IfIndex;
    XDP_INTERFACE_HANDLE InterfaceBindingSetContext;
    XDP_INTERFACE *Interfaces[2];   // One binding for both generic and native.
} XDP_INTERFACE_SET;

//
// Latest version of the XDP driver API.
//
static CONST XDP_VERSION XdpDriverApiCurrentVersion = {
    .Major = XDP_DRIVER_API_MAJOR_VER,
    .Minor = XDP_DRIVER_API_MINOR_VER,
    .Patch = XDP_DRIVER_API_PATCH_VER
};

static EX_PUSH_LOCK XdpBindingLock;
static LIST_ENTRY XdpBindingSets;
static BOOLEAN XdpBindInitialized = FALSE;

static
XDP_INTERFACE *
XdpInterfaceFromConfig(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    )
{
    return CONTAINING_RECORD(InterfaceConfig, XDP_INTERFACE, OpenConfig);
}

static
BOOLEAN
XdpValidateCapabilitiesEx(
    _In_ CONST XDP_CAPABILITIES_EX *CapabilitiesEx,
    _In_ UINT32 TotalSize
    )
{
    UINT32 Size;
    NTSTATUS Status;

    if (CapabilitiesEx->Header.Revision < XDP_CAPABILITIES_EX_REVISION_1 ||
        CapabilitiesEx->Header.Size < XDP_SIZEOF_CAPABILITIES_EX_REVISION_1) {
        return FALSE;
    }

    Status =
        RtlUInt32Mult(
            CapabilitiesEx->DriverApiVersionCount, sizeof(XDP_VERSION), &Size);
    if (!NT_SUCCESS(Status)) {
        return FALSE;
    }

    Status =
        RtlUInt32Add(
            CapabilitiesEx->DriverApiVersionsOffset, Size, &Size);
    if (!NT_SUCCESS(Status)) {
        return FALSE;
    }

    return TotalSize >= Size;
}

CONST XDP_VERSION *
XdpGetDriverApiVersion(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    )
{
    XDP_INTERFACE *Interface = XdpInterfaceFromConfig(InterfaceConfig);
    return &Interface->DriverApiVersion;
}

static CONST XDP_INTERFACE_CONFIG_DISPATCH XdpOpenDispatch = {
    .Header         = {
        .Revision   = XDP_INTERFACE_CONFIG_DISPATCH_REVISION_1,
        .Size       = XDP_SIZEOF_INTERFACE_CONFIG_DISPATCH_REVISION_1
    },
    .GetDriverApiVersion = XdpGetDriverApiVersion
};

static
VOID
XdpIfpBindingNmrDelete(
    _In_ XDP_BINDING_WORKITEM *Item
    );

static
VOID
XdpIfpReferenceBinding(
    _Inout_ XDP_INTERFACE *Binding
    )
{
    InterlockedIncrement(&Binding->ReferenceCount);
}

static
VOID
XdpIfpDereferenceBinding(
    _Inout_ XDP_INTERFACE *Binding
    )
{
    if (InterlockedDecrement(&Binding->ReferenceCount) == 0) {
        ASSERT(Binding->ProviderReference == 0);
        if (Binding->WorkQueue != NULL) {
            XdpShutdownWorkQueue(Binding->WorkQueue, FALSE);
        }
        ExFreePoolWithTag(Binding, XDP_POOLTAG_BINDING);
    }
}

VOID
XdpIfDereferenceBinding(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XdpIfpDereferenceBinding((XDP_INTERFACE *)BindingHandle);
}

static
VOID
XdpIfpDereferenceNmr(
    _In_ XDP_INTERFACE_NMR *Nmr
    )
{
    if (--Nmr->ReferenceCount == 0) {
        ASSERT(Nmr->NmrHandle == NULL);
        ExFreePoolWithTag(Nmr, XDP_POOLTAG_BINDING);
    }
}

static
VOID
XdpIfpDetachNmrInterface(
    _In_ VOID *ProviderContext
    )
{
    XDP_INTERFACE_NMR *Nmr = ProviderContext;
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)Nmr->WorkItem.BindingHandle;

    TraceVerbose(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! NMR detach notification",
        Binding->IfIndex, Binding->Capabilities.Mode);

    KeSetEvent(&Nmr->DetachNotification, 0, FALSE);
    XdpIfQueueWorkItem(&Nmr->WorkItem);
    XdpIfpDereferenceBinding(Binding);
}

static
VOID
XdpIfpCloseNmrInterface(
    _In_ XDP_INTERFACE *Binding
    )
{
    XDP_INTERFACE_NMR *Nmr = Binding->Nmr;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    ASSERT(Binding->ProviderReference == 0);
    ASSERT(Binding->InterfaceContext == NULL);
    ASSERT(Nmr != NULL && Nmr->NmrHandle != NULL);

    XdpCloseProvider(Nmr->NmrHandle);
    KeWaitForSingleObject(&Nmr->DetachNotification, Executive, KernelMode, FALSE, NULL);
    XdpCleanupProvider(Nmr->NmrHandle);
    Nmr->NmrHandle = NULL;

    Binding->Nmr = NULL;
    XdpIfpDereferenceNmr(Nmr);

    TraceExit(TRACE_CORE);
}

static
VOID
XdpIfpInvokeCloseInterface(
    _In_ XDP_INTERFACE *Binding
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    if (Binding->InterfaceDispatch->CloseInterface != NULL) {
        Binding->InterfaceDispatch->CloseInterface(Binding->InterfaceContext);
    }

    TraceExit(TRACE_CORE);
}

static
VOID
XdpIfpCloseInterface(
    _In_ XDP_INTERFACE *Binding
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    if (Binding->InterfaceContext != NULL) {
        XdpIfpInvokeCloseInterface(Binding);
        Binding->InterfaceDispatch = NULL;
        Binding->InterfaceContext = NULL;
    }

    if (Binding->Nmr != NULL) {
        XdpIfpCloseNmrInterface(Binding);
        Binding->NmrDeleting = FALSE;

        TraceVerbose(TRACE_CORE, "interface closed");
    }

    if (Binding->BindingDeleting && Binding->InterfaceBindingContext != NULL) {
        Binding->DeleteBindingComplete(Binding->InterfaceBindingContext);
        Binding->InterfaceBindingContext = NULL;

        TraceVerbose(TRACE_CORE, "interface deregistration completed");
    }

    TraceExit(TRACE_CORE);
}

static
NTSTATUS
XdpIfpInvokeOpenInterface(
    _In_ XDP_INTERFACE *Binding,
    _In_opt_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    if (InterfaceDispatch->OpenInterface != NULL) {
        ASSERT(InterfaceContext);
        Status =
            InterfaceDispatch->OpenInterface(
                InterfaceContext, (XDP_INTERFACE_CONFIG)&Binding->OpenConfig);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
BOOLEAN
XdpVersionIsSupported(
    _In_ CONST XDP_VERSION *Version,
    _In_ CONST XDP_VERSION *MinimumSupportedVersion
    )
{
    return
        Version->Major == MinimumSupportedVersion->Major &&
        Version->Minor >= MinimumSupportedVersion->Minor &&
        Version->Patch >= MinimumSupportedVersion->Patch;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpRequestClientDispatch(
    _In_ CONST XDP_CAPABILITIES_EX *ClientCapabilitiesEx,
    _In_ VOID *GetInterfaceContext,
    _In_ XDP_GET_INTERFACE_DISPATCH  *GetInterfaceDispatch,
    _Inout_ XDP_INTERFACE *Binding,
    _Out_ VOID **InterfaceContext,
    _Out_ CONST XDP_INTERFACE_DISPATCH  **InterfaceDispatch
    )
{
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    XDP_VERSION *DriverApiVersions =
        RTL_PTR_ADD(
            ClientCapabilitiesEx, ClientCapabilitiesEx->DriverApiVersionsOffset);

    for (UINT32 i = 0; i < ClientCapabilitiesEx->DriverApiVersionCount; i++) {
        CONST XDP_VERSION *ClientVersion = &DriverApiVersions[i];

        if (XdpVersionIsSupported(&XdpDriverApiCurrentVersion, ClientVersion)) {
            Status =
                GetInterfaceDispatch(
                    ClientVersion, GetInterfaceContext,
                    InterfaceContext, InterfaceDispatch);
            if (NT_SUCCESS(Status)) {
                Binding->DriverApiVersion = *ClientVersion;
                TraceInfo(
                    TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Received interface dispatch"
                    " table for ClientVersion=%u.%u.%u",
                    Binding->IfIndex, Binding->Capabilities.Mode,
                    ClientVersion->Major, ClientVersion->Minor, ClientVersion->Patch);
                break;
            } else {
                TraceWarn(
                    TRACE_CORE,
                    "IfIndex=%u Mode=%!XDP_MODE! Failed to get interface dispatch table"
                    " Status=%!STATUS!", Binding->IfIndex, Binding->Capabilities.Mode,
                    Status);
                Status = STATUS_NOT_SUPPORTED;
            }
        }
    }

    if (!NT_SUCCESS(Status)) {
        TraceWarn(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! No compatible interface was found",
            Binding->IfIndex, Binding->Capabilities.Mode);
    }
    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpOpenInterface(
    _Inout_ XDP_INTERFACE *Binding
    )
{
    CONST XDP_CAPABILITIES_EX *CapabilitiesEx = Binding->Capabilities.CapabilitiesEx;
    VOID *GetInterfaceContext;
    XDP_GET_INTERFACE_DISPATCH  *GetInterfaceDispatch;
    VOID *InterfaceContext;
    CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    if (CapabilitiesEx->Header.Revision < XDP_CAPABILITIES_EX_REVISION_1 ||
        CapabilitiesEx->Header.Size < XDP_SIZEOF_CAPABILITIES_EX_REVISION_1) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Invalid capabilities",
            Binding->IfIndex, Binding->Capabilities.Mode);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    ASSERT(Binding->Nmr == NULL);

    Binding->Nmr =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Binding->Nmr), XDP_POOLTAG_BINDING);
    if (Binding->Nmr == NULL) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! NMR allocation failed",
            Binding->IfIndex, Binding->Capabilities.Mode);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    if (!XdpIsFeOrLater() && Binding->Capabilities.Mode == XDP_INTERFACE_MODE_NATIVE) {
        TraceWarn(TRACE_CORE, "Opening a native XDP interface on an unsupported OS");
    }

    XdpIfpReferenceBinding(Binding);
    Binding->Nmr->ReferenceCount = 2;
    Binding->Nmr->WorkItem.BindingHandle = (XDP_BINDING_HANDLE)Binding;
    Binding->Nmr->WorkItem.WorkRoutine = XdpIfpBindingNmrDelete;
    KeInitializeEvent(&Binding->Nmr->DetachNotification, NotificationEvent, FALSE);

    Status =
        XdpOpenProvider(
            Binding->IfIndex, &CapabilitiesEx->InstanceId, Binding->Nmr,
            XdpIfpDetachNmrInterface, &GetInterfaceContext, &GetInterfaceDispatch,
            &Binding->Nmr->NmrHandle);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Failed to open NMR binding",
            Binding->IfIndex, Binding->Capabilities.Mode);
        goto Exit;
    }

    Status =
        XdpRequestClientDispatch(
            CapabilitiesEx, GetInterfaceContext, GetInterfaceDispatch,
            Binding, &InterfaceContext, &InterfaceDispatch);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Binding->InterfaceContext = InterfaceContext;
    Binding->InterfaceDispatch = InterfaceDispatch;

    Status = XdpIfpInvokeOpenInterface(Binding, InterfaceContext, InterfaceDispatch);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Interface open failed",
            Binding->IfIndex, Binding->Capabilities.Mode);
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Binding->Nmr != NULL) {
            if (Binding->Nmr->NmrHandle != NULL) {
                XdpIfpCloseInterface(Binding);
            } else {
                XdpIfpDereferenceBinding(Binding);
                ExFreePoolWithTag(Binding->Nmr, XDP_POOLTAG_BINDING);
            }
            Binding->Nmr = NULL;
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
_Requires_lock_held_(&XdpBindingLock)
XDP_INTERFACE_SET *
XdpIfpFindIfSet(
    _In_ NET_IFINDEX IfIndex
    )
{
    XDP_INTERFACE_SET *IfSet = NULL;
    LIST_ENTRY *Entry = XdpBindingSets.Flink;

    while (Entry != &XdpBindingSets) {
        XDP_INTERFACE_SET *Candidate = CONTAINING_RECORD(Entry, XDP_INTERFACE_SET, Link);
        Entry = Entry->Flink;

        if (Candidate->IfIndex == IfIndex) {
            IfSet = Candidate;
            break;
        }
    }

    return IfSet;
}

_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN
XdpIfSupportsHookId(
    _In_ CONST XDP_CAPABILITIES_INTERNAL *Capabilities,
    _In_ CONST XDP_HOOK_ID *Target
    )
{
    for (UINT32 Index = 0; Index < Capabilities->HookCount; Index++) {
        CONST XDP_HOOK_ID *Candidate = &Capabilities->Hooks[Index];

        if (Target->Layer == Candidate->Layer &&
            Target->Direction == Candidate->Direction &&
            Target->SubLayer == Candidate->SubLayer) {
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
XdpIfpSupportsHookIds(
    _In_ CONST XDP_CAPABILITIES_INTERNAL *Capabilities,
    _In_ CONST XDP_HOOK_ID *TargetIds,
    _In_ UINT32 TargetCount
    )
{
    for (UINT32 TargetIndex = 0; TargetIndex < TargetCount; TargetIndex++) {
        if (!XdpIfSupportsHookId(Capabilities, &TargetIds[TargetIndex])) {
            return FALSE;
        }
    }

    return TRUE;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
_Requires_lock_held_(&XdpBindingLock)
XDP_INTERFACE *
XdpIfpFindBinding(
    _In_ NET_IFINDEX IfIndex,
    _In_ CONST XDP_HOOK_ID *HookIds,
    _In_ UINT32 HookCount,
    _In_opt_ XDP_INTERFACE_MODE *RequiredMode
    )
{
    XDP_INTERFACE_SET *IfSet = NULL;
    XDP_INTERFACE *Interface = NULL;

    IfSet = XdpIfpFindIfSet(IfIndex);
    if (IfSet == NULL) {
        goto Exit;
    }

    //
    // Find the best interface matching the caller constraints.
    //
    for (XDP_INTERFACE_MODE Mode = XDP_INTERFACE_MODE_GENERIC;
        Mode <= XDP_INTERFACE_MODE_NATIVE;
        Mode++) {
        XDP_INTERFACE *CandidateIf = IfSet->Interfaces[Mode];

        if (CandidateIf == NULL) {
            continue;
        }

        if (RequiredMode != NULL && *RequiredMode != Mode) {
            continue;
        }

        if (!XdpIfpSupportsHookIds(&CandidateIf->Capabilities, HookIds, HookCount)) {
            continue;
        }

        Interface = CandidateIf;
    }

Exit:

    return Interface;
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_BINDING_HANDLE
XdpIfFindAndReferenceBinding(
    _In_ NET_IFINDEX IfIndex,
    _In_ CONST XDP_HOOK_ID *HookIds,
    _In_ UINT32 HookCount,
    _In_opt_ XDP_INTERFACE_MODE *RequiredMode
    )
{
    XDP_INTERFACE *Binding = NULL;

    ExAcquirePushLockShared(&XdpBindingLock);
    Binding = XdpIfpFindBinding(IfIndex, HookIds, HookCount, RequiredMode);
    if (Binding != NULL) {
        XdpIfpReferenceBinding(Binding);
    }
    ExReleasePushLockShared(&XdpBindingLock);

    return (XDP_BINDING_HANDLE)Binding;
}

VOID
XdpIfQueueWorkItem(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)WorkItem->BindingHandle;

    KeGetCurrentProcessorNumberEx(&WorkItem->IdealProcessor);
    XdpIfpReferenceBinding(Binding);
    XdpInsertWorkQueue(Binding->WorkQueue, &WorkItem->Link);
}

CONST XDP_CAPABILITIES_INTERNAL *
XdpIfGetCapabilities(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    return &Binding->Capabilities;
}

static
VOID
XdpIfpStartRundown(
    _In_ XDP_INTERFACE *Binding
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    if (Binding->ProviderReference == 0) {
        XdpIfpCloseInterface(Binding);
    }

    while (!IsListEmpty(&Binding->Clients)) {
        XDP_BINDING_CLIENT_ENTRY *ClientEntry =
            CONTAINING_RECORD(Binding->Clients.Flink, XDP_BINDING_CLIENT_ENTRY, Link);

        RemoveEntryList(&ClientEntry->Link);
        InitializeListHead(&ClientEntry->Link);

        ClientEntry->Client->BindingDetached(ClientEntry);

        XdpIfpDereferenceBinding(Binding);
    }

    TraceExit(TRACE_CORE);
}

static
VOID
XdpIfpBindingDelete(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)Item->BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Binding->IfIndex, Binding->Capabilities.Mode);

    Binding->BindingDeleting = TRUE;

    XdpIfpStartRundown(Binding);

    //
    // Release the initial binding reference.
    //
    XdpIfpDereferenceBinding(Binding);

    TraceExit(TRACE_CORE);
}

static
VOID
XdpIfpBindingNmrDelete(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)Item->BindingHandle;
    XDP_INTERFACE_NMR *Nmr = CONTAINING_RECORD(Item, XDP_INTERFACE_NMR, WorkItem);

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!", Binding->IfIndex, Binding->Capabilities.Mode);

    if (Nmr->NmrHandle != NULL) {
        ASSERT(Binding->NmrDeleting == FALSE);
        Binding->NmrDeleting = TRUE;

        XdpIfpStartRundown(Binding);
    }

    XdpIfpDereferenceNmr(Nmr);

    TraceExit(TRACE_CORE);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfpBindingWorker(
    _In_ SINGLE_LIST_ENTRY *WorkQueueHead
    )
{
    while (WorkQueueHead != NULL) {
        XDP_BINDING_WORKITEM *Item;
        XDP_INTERFACE *Binding;
        GROUP_AFFINITY Affinity = {0};
        GROUP_AFFINITY OldAffinity;

        Item = CONTAINING_RECORD(WorkQueueHead, XDP_BINDING_WORKITEM, Link);
        Binding = (XDP_INTERFACE *)Item->BindingHandle;
        WorkQueueHead = WorkQueueHead->Next;

        //
        // Perform work on the original caller's NUMA node. For simplicity,
        // target the CPU itself.
        //
        Affinity.Group = Item->IdealProcessor.Group;
        Affinity.Mask = 1ui64 << Item->IdealProcessor.Number;
        KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

        Item->WorkRoutine(Item);

        KeRevertToUserGroupAffinityThread(&OldAffinity);

        XdpIfpDereferenceBinding(Binding);
    }
}

static
_IRQL_requires_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&XdpBindingLock)
VOID
XdpIfpTrimIfSet(
    _In_ XDP_INTERFACE_SET *IfSet
    )
{
    BOOLEAN IsEmpty = TRUE;

    for (XDP_INTERFACE_MODE Mode = XDP_INTERFACE_MODE_GENERIC;
        Mode <= XDP_INTERFACE_MODE_NATIVE;
        Mode++) {
        if (IfSet->Interfaces[Mode] != NULL) {
            IsEmpty = FALSE;
            break;
        }
    }

    if (IsEmpty) {
        RemoveEntryList(&IfSet->Link);
        ExFreePoolWithTag(IfSet, XDP_POOLTAG_BINDING);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateBindings(
    _In_ NET_IFINDEX IfIndex,
    _Inout_ XDP_REGISTER_IF *Interfaces,
    _In_ UINT32 InterfaceCount,
    _In_ VOID *BindingSetContext,
    _Out_ XDP_IF_BINDING_SET_HANDLE *BindingSetHandle
    )
{
    NTSTATUS Status;
    XDP_INTERFACE_SET *IfSet = NULL;

    //
    // This function is invoked by an interface provider (e.g. NDIS6 via XdpLwf)
    // when a NIC is added.
    //

    TraceEnter(TRACE_CORE, "IfIndex=%u", IfIndex);

    ExAcquirePushLockExclusive(&XdpBindingLock);

    //
    // Check for duplicate binding set.
    //
    IfSet = XdpIfpFindIfSet(IfIndex);
    if (IfSet != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    //
    // Create a helper entry per interface index.
    //
    IfSet = ExAllocatePoolZero(PagedPool, sizeof(*IfSet), XDP_POOLTAG_BINDING);
    if (IfSet == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    IfSet->IfIndex = IfIndex;
    IfSet->InterfaceBindingSetContext = BindingSetContext;
    InitializeListHead(&IfSet->Link);
    InsertTailList(&XdpBindingSets, &IfSet->Link);

    for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
        XDP_REGISTER_IF *Registration = &Interfaces[Index];
        XDP_INTERFACE *Binding = NULL;

        if (!XdpValidateCapabilitiesEx(
                Registration->InterfaceCapabilities->CapabilitiesEx,
                Registration->InterfaceCapabilities->CapabilitiesSize)) {
            TraceError(
                TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Invalid capabilities",
                IfIndex, Registration->InterfaceCapabilities->Mode);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }

        Binding = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Binding), XDP_POOLTAG_BINDING);
        if (Binding == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        Binding->IfIndex = IfIndex;
        Binding->IfSet = IfSet;
        Binding->DeleteBindingComplete = Registration->DeleteBindingComplete;
        Binding->InterfaceBindingContext = Registration->BindingContext;
        Binding->OpenConfig.Dispatch = &XdpOpenDispatch;
        Binding->ReferenceCount = 1;
        RtlCopyMemory(
            (XDP_CAPABILITIES_INTERNAL *)&Binding->Capabilities,
            Registration->InterfaceCapabilities,
            sizeof(Binding->Capabilities));
        InitializeListHead(&Binding->Clients);

        Binding->WorkQueue =
            XdpCreateWorkQueue(XdpIfpBindingWorker, DISPATCH_LEVEL, XdpDriverObject, NULL);
        if (Binding->WorkQueue == NULL) {
            ExFreePoolWithTag(Binding, XDP_POOLTAG_BINDING);
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        ASSERT(IfSet->Interfaces[Binding->Capabilities.Mode] == NULL);
        IfSet->Interfaces[Binding->Capabilities.Mode] = Binding;
        *Registration->BindingHandle = (XDP_IF_BINDING_HANDLE)Binding;

        TraceVerbose(TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! BindingContext=%p registered",
            Binding->IfIndex, Binding->Capabilities.Mode, Binding->InterfaceBindingContext);
    }

    *BindingSetHandle = (XDP_IF_BINDING_SET_HANDLE)IfSet;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
            if (*Interfaces[Index].BindingHandle != NULL) {
                XDP_INTERFACE *Binding;
                Binding = (XDP_INTERFACE *)(*Interfaces[Index].BindingHandle);
                ASSERT(IfSet);
                IfSet->Interfaces[Binding->Capabilities.Mode] = NULL;
                *Interfaces[Index].BindingHandle = NULL;
                XdpIfpDereferenceBinding(Binding);
            }
        }
        if (IfSet != NULL) {
            XdpIfpTrimIfSet(IfSet);
        }
    }

    ExReleasePushLockExclusive(&XdpBindingLock);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfDeleteBindings(
    _In_reads_(HandleCount) XDP_IF_BINDING_HANDLE *BindingHandles,
    _In_ UINT32 HandleCount
    )
{
    //
    // This function is invoked by an interface provider (e.g. XDP LWF)
    // when a NIC is deleted.
    //

    ExAcquirePushLockExclusive(&XdpBindingLock);

    for (UINT32 Index = 0; Index < HandleCount; Index++) {
        XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandles[Index];

        TraceVerbose(TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! deregistering",
            Binding->IfIndex, Binding->Capabilities.Mode);
        Binding->IfSet->Interfaces[Binding->Capabilities.Mode] = NULL;
        XdpIfpTrimIfSet(Binding->IfSet);
        Binding->IfSet = NULL;

        Binding->DeleteWorkItem.BindingHandle = (XDP_BINDING_HANDLE)Binding;
        Binding->DeleteWorkItem.WorkRoutine = XdpIfpBindingDelete;
        XdpIfQueueWorkItem(&Binding->DeleteWorkItem);
    }

    ExReleasePushLockExclusive(&XdpBindingLock);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfInitializeClientEntry(
    _Out_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    RtlZeroMemory(ClientEntry, sizeof(*ClientEntry));
    InitializeListHead(&ClientEntry->Link);
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfRegisterClient(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ CONST XDP_BINDING_CLIENT *Client,
    _In_ CONST VOID *Key,
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;
    LIST_ENTRY *Entry;

    FRE_ASSERT(Client->ClientId != XDP_BINDING_CLIENT_ID_INVALID);
    FRE_ASSERT(Client->KeySize > 0);
    FRE_ASSERT(Key != NULL);

    if (Binding->BindingDeleting) {
        TraceInfo(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! client registration failed: binding deleting",
            Binding->IfIndex, Binding->Capabilities.Mode);
        return STATUS_DELETE_PENDING;
    }

    //
    // Verify we're not inserting a duplicate client.
    //
    Entry = Binding->Clients.Flink;
    while (Entry != &Binding->Clients) {
        XDP_BINDING_CLIENT_ENTRY *Candidate =
            CONTAINING_RECORD(Entry, XDP_BINDING_CLIENT_ENTRY, Link);
        Entry = Entry->Flink;

        if (!NT_VERIFY(
                (Candidate->Client->ClientId != Client->ClientId) ||
                !RtlEqualMemory(Candidate->Key, Key, Client->KeySize))) {
            TraceInfo(
                TRACE_CORE,
                "IfIndex=%u Mode=%!XDP_MODE! client registration failed: duplicate client",
                Binding->IfIndex, Binding->Capabilities.Mode);
            return STATUS_DUPLICATE_OBJECTID;
        }
    }

    ClientEntry->Client = Client;
    ClientEntry->Key = Key;
    XdpIfpReferenceBinding(Binding);
    InsertTailList(&Binding->Clients, &ClientEntry->Link);

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeregisterClient(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    //
    // Invoked by XDP components (e.g. programs, XSKs) to detach from an
    // interface binding.
    //

    if (!IsListEmpty(&ClientEntry->Link)) {
        RemoveEntryList(&ClientEntry->Link);
        InitializeListHead(&ClientEntry->Link);
        XdpIfpDereferenceBinding(Binding);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_BINDING_CLIENT_ENTRY *
XdpIfFindClientEntry(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ CONST XDP_BINDING_CLIENT *Client,
    _In_ CONST VOID *Key
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;
    LIST_ENTRY *Entry;
    XDP_BINDING_CLIENT_ENTRY *Candidate;

    Entry = Binding->Clients.Flink;
    while (Entry != &Binding->Clients) {
        Candidate = CONTAINING_RECORD(Entry, XDP_BINDING_CLIENT_ENTRY, Link);
        Entry = Entry->Flink;

        if (Candidate->Client->ClientId == Client->ClientId &&
            RtlEqualMemory(Candidate->Key, Key, Client->KeySize)) {
            return Candidate;
        }
    }

    return NULL;
}

_IRQL_requires_(PASSIVE_LEVEL)
NET_IFINDEX
XdpIfGetIfIndex(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    return Binding->IfIndex;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpReferenceInterface(
    _In_ XDP_INTERFACE *Binding
    )
{
    NTSTATUS Status;

    if (Binding->Rundown) {
        Status = STATUS_DELETE_PENDING;
        TraceInfo(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! reference failed: rundown",
            Binding->IfIndex, Binding->Capabilities.Mode);
        goto Exit;
    }

    if (Binding->InterfaceContext == NULL) {
        ASSERT(Binding->ProviderReference == 0);
        Status = XdpIfpOpenInterface(Binding);
        if (!NT_SUCCESS(Status)) {
            TraceInfo(
                TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! reference failed: open interface",
                Binding->IfIndex, Binding->Capabilities.Mode);
            goto Exit;
        }
    }

    Binding->ProviderReference++;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfpDereferenceInterface(
    _In_ XDP_INTERFACE *Binding
    )
{
    if (--Binding->ProviderReference == 0) {
        XdpIfpCloseInterface(Binding);
    }
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpInvokeCreateRxQueue(
    _In_ XDP_INTERFACE *Binding,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Binding->IfIndex, Binding->Capabilities.Mode,
        XdpRxQueueGetTargetQueueInfo(Config)->QueueId);

    Status =
        Binding->InterfaceDispatch->CreateRxQueue(
            Binding->InterfaceContext, Config, InterfaceRxQueue, InterfaceRxQueueDispatch);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateRxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Binding->IfIndex, Binding->Capabilities.Mode,
        XdpRxQueueGetTargetQueueInfo(Config)->QueueId);

    *InterfaceRxQueue = NULL;
    *InterfaceRxQueueDispatch = NULL;

    Status = XdpIfpReferenceInterface(Binding);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpIfpInvokeCreateRxQueue(Binding, Config, InterfaceRxQueue, InterfaceRxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        XdpIfpDereferenceInterface(Binding);
        goto Exit;
    }

    FRE_ASSERT(*InterfaceRxQueue != NULL);
    FRE_ASSERT(*InterfaceRxQueueDispatch != NULL);
    TraceVerbose(TRACE_CORE, "Created InterfaceQueue=%p", *InterfaceRxQueue);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfActivateRxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Binding->IfIndex, Binding->Capabilities.Mode, InterfaceRxQueue);

    Binding->InterfaceDispatch->ActivateRxQueue(InterfaceRxQueue, XdpRxQueue, Config);

    TraceExit(TRACE_CORE);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeleteRxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Binding->IfIndex, Binding->Capabilities.Mode, InterfaceRxQueue);

    Binding->InterfaceDispatch->DeleteRxQueue(InterfaceRxQueue);

    XdpIfpDereferenceInterface(Binding);

    TraceExit(TRACE_CORE);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpInvokeCreateTxQueue(
    _In_ XDP_INTERFACE *Binding,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Binding->IfIndex, Binding->Capabilities.Mode,
        XdpTxQueueGetTargetQueueInfo(Config)->QueueId);

    Status =
        Binding->InterfaceDispatch->CreateTxQueue(
            Binding->InterfaceContext, Config, InterfaceTxQueue, InterfaceTxQueueDispatch);

    TraceExitStatus(TRACE_CORE);

    return Status;
}
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateTxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Binding->IfIndex, Binding->Capabilities.Mode,
        XdpTxQueueGetTargetQueueInfo(Config)->QueueId);

    *InterfaceTxQueue = NULL;
    *InterfaceTxQueueDispatch = NULL;

    Status = XdpIfpReferenceInterface(Binding);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpIfpInvokeCreateTxQueue(Binding, Config, InterfaceTxQueue, InterfaceTxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        XdpIfpDereferenceInterface(Binding);
        goto Exit;
    }

    FRE_ASSERT(*InterfaceTxQueue != NULL);
    FRE_ASSERT(*InterfaceTxQueueDispatch != NULL);
    TraceVerbose(TRACE_CORE, "Created InterfaceQueue=%p", *InterfaceTxQueue);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfActivateTxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Binding->IfIndex, Binding->Capabilities.Mode, InterfaceTxQueue);

    Binding->InterfaceDispatch->ActivateTxQueue(InterfaceTxQueue, XdpTxQueue, Config);

    TraceExit(TRACE_CORE);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeleteTxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    )
{
    XDP_INTERFACE *Binding = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Binding->IfIndex, Binding->Capabilities.Mode, InterfaceTxQueue);

    Binding->InterfaceDispatch->DeleteTxQueue(InterfaceTxQueue);

    XdpIfpDereferenceInterface(Binding);

    TraceExit(TRACE_CORE);
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIfStart(
    VOID
    )
{
    ExInitializePushLock(&XdpBindingLock);
    InitializeListHead(&XdpBindingSets);
    XdpBindInitialized = TRUE;
    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
XdpIfStop(
    VOID
    )
{
    if (!XdpBindInitialized) {
        return;
    }

    ExAcquirePushLockExclusive(&XdpBindingLock);

    ASSERT(IsListEmpty(&XdpBindingSets));

    ExReleasePushLockExclusive(&XdpBindingLock);
}
