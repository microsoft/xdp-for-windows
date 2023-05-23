//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module provides:
//
// 1. A single abstraction for core XDP modules to manipulate disparate XDP
//    interface types. This module implements the core XDP side of the XDP IF
//    API.
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
    XDP_REFERENCE_COUNT ReferenceCount;
    CONST XDP_CAPABILITIES_INTERNAL Capabilities;
    XDP_WORK_QUEUE *WorkQueue;
    XDP_BINDING_WORKITEM RemoveWorkItem;   // Guaranteed item for remove.

    //
    // XDP core components bound to this XDP interface.
    //
    LIST_ENTRY Clients;

    XDP_INTERFACE_NMR *Nmr;
    BOOLEAN NmrDeleting;

    struct {
        VOID *InterfaceContext;
        XDP_REMOVE_INTERFACE_COMPLETE *RemoveInterfaceComplete;
        BOOLEAN InterfaceRemoving;
    } XdpIfApi;

    struct {
        XDP_VERSION Version;
        CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch;
        VOID *InterfaceContext;
        ULONG ProviderReference;
        XDP_INTERFACE_CONFIG_DETAILS OpenConfig;
    } XdpDriverApi;
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
    XDP_REFERENCE_COUNT ReferenceCount;
    XDP_BINDING_WORKITEM WorkItem;
} XDP_INTERFACE_NMR;

typedef struct _XDP_INTERFACE_SET {
    LIST_ENTRY Link;
    NET_IFINDEX IfIndex;
    XDP_REFERENCE_COUNT ReferenceCount;
    EX_PUSH_LOCK Lock;

    //
    // The offload rundown must be acquired to create new interface offload
    // handles or invoke interface routines, except cleanup.
    //
    EX_RUNDOWN_REF OffloadRundown;

    //
    // The offload reference count must be held until all interface offload
    // downcalls are complete, including cleanup.
    //
    XDP_REFERENCE_COUNT OffloadReferenceCount;

    //
    // This event is set once all offload downcalls are complete.
    //
    KEVENT *OffloadDowncallsComplete;
    LIST_ENTRY OffloadObjects;
    CONST XDP_OFFLOAD_DISPATCH *OffloadDispatch;
    VOID *XdpIfInterfaceSetContext;
    XDP_INTERFACE *Interfaces[2];   // One interface for both generic and native.
} XDP_INTERFACE_SET;

typedef struct _XDP_IF_OFFLOAD_OBJECT {
    XDP_REFERENCE_COUNT ReferenceCount;
    LIST_ENTRY Entry;
    VOID *InterfaceOffloadHandle;
    XDP_OFFLOAD_IF_SETTINGS OffloadSettings;
} XDP_IF_OFFLOAD_OBJECT;

//
// Latest version of the XDP driver API.
//
static CONST XDP_VERSION XdpDriverApiCurrentVersion = {
    .Major = XDP_DRIVER_API_MAJOR_VER,
    .Minor = XDP_DRIVER_API_MINOR_VER,
    .Patch = XDP_DRIVER_API_PATCH_VER
};

static EX_PUSH_LOCK XdpInterfaceSetsLock;
static LIST_ENTRY XdpInterfaceSets;
static BOOLEAN XdpBindInitialized = FALSE;

static
XDP_INTERFACE *
XdpInterfaceFromConfig(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    )
{
    return CONTAINING_RECORD(InterfaceConfig, XDP_INTERFACE, XdpDriverApi.OpenConfig);
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
    return &Interface->XdpDriverApi.Version;
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
XdpIfpInterfaceNmrDelete(
    _In_ XDP_BINDING_WORKITEM *Item
    );

static
VOID
XdpIfpReferenceInterface(
    _Inout_ XDP_INTERFACE *Interface
    )
{
    XdpIncrementReferenceCount(&Interface->ReferenceCount);
}

static
VOID
XdpIfpReferenceIfSet(
    _Inout_ XDP_INTERFACE_SET *IfSet
    )
{
    XdpIncrementReferenceCount(&IfSet->ReferenceCount);
}

static
VOID
XdpIfpDereferenceInterface(
    _Inout_ XDP_INTERFACE *Interface
    )
{
    if (XdpDecrementReferenceCount(&Interface->ReferenceCount)) {
        ASSERT(Interface->XdpDriverApi.ProviderReference == 0);
        if (Interface->WorkQueue != NULL) {
            XdpShutdownWorkQueue(Interface->WorkQueue, FALSE);
        }
        ExFreePoolWithTag(Interface, XDP_POOLTAG_IF);
    }
}

static
VOID
XdpIfpDereferenceIfSet(
    _Inout_ XDP_INTERFACE_SET *IfSet
    )
{
    if (XdpDecrementReferenceCount(&IfSet->ReferenceCount)) {
        TraceVerbose(TRACE_CORE, "IfIndex=%u IfSet=%p cleaned up", IfSet->IfIndex, IfSet);
        ExFreePoolWithTag(IfSet, XDP_POOLTAG_IFSET);
    }
}

VOID
XdpIfDereferenceBinding(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XdpIfpDereferenceInterface((XDP_INTERFACE *)BindingHandle);
}

VOID
XdpIfDereferenceIfSet(
    _In_ XDP_IFSET_HANDLE IfSetHandle
    )
{
    XdpIfpDereferenceIfSet((XDP_INTERFACE_SET *)IfSetHandle);
}

XDP_IFSET_HANDLE
XdpIfGetIfSetHandle(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    return (XDP_IFSET_HANDLE)Interface->IfSet;
}

static
VOID
XdpIfpDereferenceNmr(
    _In_ XDP_INTERFACE_NMR *Nmr
    )
{
    if (--Nmr->ReferenceCount == 0) {
        ASSERT(Nmr->NmrHandle == NULL);
        ExFreePoolWithTag(Nmr, XDP_POOLTAG_NMR);
    }
}

static
VOID
XdpIfpDetachNmrInterface(
    _In_ VOID *ProviderContext
    )
{
    XDP_INTERFACE_NMR *Nmr = ProviderContext;
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)Nmr->WorkItem.BindingHandle;

    TraceVerbose(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! NMR detach notification",
        Interface->IfIndex, Interface->Capabilities.Mode);

    KeSetEvent(&Nmr->DetachNotification, 0, FALSE);
    XdpIfQueueWorkItem(&Nmr->WorkItem);
    XdpIfpDereferenceInterface(Interface);
}

static
VOID
XdpIfpCloseNmrInterface(
    _In_ XDP_INTERFACE *Interface
    )
{
    XDP_INTERFACE_NMR *Nmr = Interface->Nmr;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    ASSERT(Interface->XdpDriverApi.ProviderReference == 0);
    ASSERT(Interface->XdpDriverApi.InterfaceContext == NULL);
    ASSERT(Nmr != NULL && Nmr->NmrHandle != NULL);

    XdpCloseProvider(Nmr->NmrHandle);
    KeWaitForSingleObject(&Nmr->DetachNotification, Executive, KernelMode, FALSE, NULL);
    XdpCleanupProvider(Nmr->NmrHandle);
    Nmr->NmrHandle = NULL;

    Interface->Nmr = NULL;
    XdpIfpDereferenceNmr(Nmr);

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpIfpInvokeDriverCloseInterface(
    _In_ XDP_INTERFACE *Interface
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    if (Interface->XdpDriverApi.InterfaceDispatch->CloseInterface != NULL) {
        Interface->XdpDriverApi.InterfaceDispatch->CloseInterface(
            Interface->XdpDriverApi.InterfaceContext);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpIfpCloseDriverInterface(
    _In_ XDP_INTERFACE *Interface
    )
{
    if (Interface->XdpDriverApi.InterfaceContext != NULL) {
        XdpIfpInvokeDriverCloseInterface(Interface);
        Interface->XdpDriverApi.InterfaceDispatch = NULL;
        Interface->XdpDriverApi.InterfaceContext = NULL;
    }
}

static
VOID
XdpIfpCloseInterface(
    _In_ XDP_INTERFACE *Interface
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    XdpIfpCloseDriverInterface(Interface);

    if (Interface->Nmr != NULL) {
        XdpIfpCloseNmrInterface(Interface);
        Interface->NmrDeleting = FALSE;
    }

    TraceExitSuccess(TRACE_CORE);
}

static
NTSTATUS
XdpIfpInvokeDriverOpenInterface(
    _In_ XDP_INTERFACE *Interface,
    _In_opt_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    if (InterfaceDispatch->OpenInterface != NULL) {
        ASSERT(InterfaceContext);
        Status =
            InterfaceDispatch->OpenInterface(
                InterfaceContext, (XDP_INTERFACE_CONFIG)&Interface->XdpDriverApi.OpenConfig);
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
    _Inout_ XDP_INTERFACE *Interface,
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
                Interface->XdpDriverApi.Version = *ClientVersion;
                TraceInfo(
                    TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Received interface dispatch"
                    " table for ClientVersion=%u.%u.%u",
                    Interface->IfIndex, Interface->Capabilities.Mode,
                    ClientVersion->Major, ClientVersion->Minor, ClientVersion->Patch);
                break;
            } else {
                TraceWarn(
                    TRACE_CORE,
                    "IfIndex=%u Mode=%!XDP_MODE! Failed to get interface dispatch table"
                    " Status=%!STATUS!", Interface->IfIndex, Interface->Capabilities.Mode,
                    Status);
                Status = STATUS_NOT_SUPPORTED;
            }
        }
    }

    if (!NT_SUCCESS(Status)) {
        TraceWarn(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! No compatible interface was found",
            Interface->IfIndex, Interface->Capabilities.Mode);
    }
    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpOpenInterface(
    _Inout_ XDP_INTERFACE *Interface
    )
{
    CONST XDP_CAPABILITIES_EX *CapabilitiesEx = Interface->Capabilities.CapabilitiesEx;
    VOID *GetInterfaceContext;
    XDP_GET_INTERFACE_DISPATCH  *GetInterfaceDispatch;
    VOID *InterfaceContext;
    CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    if (CapabilitiesEx->Header.Revision < XDP_CAPABILITIES_EX_REVISION_1 ||
        CapabilitiesEx->Header.Size < XDP_SIZEOF_CAPABILITIES_EX_REVISION_1) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Invalid capabilities",
            Interface->IfIndex, Interface->Capabilities.Mode);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    ASSERT(Interface->Nmr == NULL);

    Interface->Nmr =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Interface->Nmr), XDP_POOLTAG_NMR);
    if (Interface->Nmr == NULL) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! NMR allocation failed",
            Interface->IfIndex, Interface->Capabilities.Mode);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    if (!XdpIsFeOrLater() && Interface->Capabilities.Mode == XDP_INTERFACE_MODE_NATIVE) {
        TraceWarn(TRACE_CORE, "Opening a native XDP interface on an unsupported OS");
    }

    XdpIfpReferenceInterface(Interface);
    XdpInitializeReferenceCountEx(&Interface->Nmr->ReferenceCount, 2);
    Interface->Nmr->WorkItem.BindingHandle = (XDP_BINDING_HANDLE)Interface;
    Interface->Nmr->WorkItem.WorkRoutine = XdpIfpInterfaceNmrDelete;
    KeInitializeEvent(&Interface->Nmr->DetachNotification, NotificationEvent, FALSE);

    Status =
        XdpOpenProvider(
            Interface->IfIndex, &CapabilitiesEx->InstanceId, Interface->Nmr,
            XdpIfpDetachNmrInterface, &GetInterfaceContext, &GetInterfaceDispatch,
            &Interface->Nmr->NmrHandle);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Failed to open NMR binding",
            Interface->IfIndex, Interface->Capabilities.Mode);
        goto Exit;
    }

    Status =
        XdpRequestClientDispatch(
            CapabilitiesEx, GetInterfaceContext, GetInterfaceDispatch,
            Interface, &InterfaceContext, &InterfaceDispatch);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Interface->XdpDriverApi.InterfaceContext = InterfaceContext;
    Interface->XdpDriverApi.InterfaceDispatch = InterfaceDispatch;

    Status = XdpIfpInvokeDriverOpenInterface(Interface, InterfaceContext, InterfaceDispatch);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Interface open failed",
            Interface->IfIndex, Interface->Capabilities.Mode);
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Interface->Nmr != NULL) {
            if (Interface->Nmr->NmrHandle != NULL) {
                XdpIfpCloseInterface(Interface);
            } else {
                XdpIfpDereferenceInterface(Interface);
                ExFreePoolWithTag(Interface->Nmr, XDP_POOLTAG_NMR);
            }
            Interface->Nmr = NULL;
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
_Requires_lock_held_(&XdpInterfaceSetsLock)
XDP_INTERFACE_SET *
XdpIfpFindIfSet(
    _In_ NET_IFINDEX IfIndex
    )
{
    XDP_INTERFACE_SET *IfSet = NULL;
    LIST_ENTRY *Entry = XdpInterfaceSets.Flink;

    while (Entry != &XdpInterfaceSets) {
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
_Requires_lock_held_(&XdpInterfaceSetsLock)
XDP_INTERFACE *
XdpIfpFindInterface(
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
    XDP_INTERFACE *Interface = NULL;

    RtlAcquirePushLockShared(&XdpInterfaceSetsLock);
    Interface = XdpIfpFindInterface(IfIndex, HookIds, HookCount, RequiredMode);
    if (Interface != NULL) {
        XdpIfpReferenceInterface(Interface);
    }
    RtlReleasePushLockShared(&XdpInterfaceSetsLock);

    return (XDP_BINDING_HANDLE)Interface;
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_IFSET_HANDLE
XdpIfFindAndReferenceIfSet(
    _In_ NET_IFINDEX IfIndex,
    _In_ CONST XDP_HOOK_ID *HookIds,
    _In_ UINT32 HookCount,
    _In_opt_ XDP_INTERFACE_MODE *RequiredMode
    )
{
    XDP_INTERFACE *Interface = NULL;
    XDP_INTERFACE_SET *IfSet = NULL;

    RtlAcquirePushLockShared(&XdpInterfaceSetsLock);
    Interface = XdpIfpFindInterface(IfIndex, HookIds, HookCount, RequiredMode);
    if (Interface != NULL) {
        XdpIfpReferenceIfSet(Interface->IfSet);
        IfSet = Interface->IfSet;
    }
    RtlReleasePushLockShared(&XdpInterfaceSetsLock);

    return (XDP_IFSET_HANDLE)IfSet;
}

VOID
XdpIfQueueWorkItem(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)WorkItem->BindingHandle;

    WorkItem->IdealNode = KeGetCurrentNodeNumber();
    XdpIfpReferenceInterface(Interface);
    XdpInsertWorkQueue(Interface->WorkQueue, &WorkItem->Link);
}

CONST XDP_CAPABILITIES_INTERNAL *
XdpIfGetCapabilities(
    _In_ XDP_BINDING_HANDLE BindingHandle
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;

    return &Interface->Capabilities;
}

BOOLEAN
XdpIfAcquireOffloadRundown(
    _In_ XDP_IFSET_HANDLE IfSetHandle
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;

    return ExAcquireRundownProtection(&IfSet->OffloadRundown);
}

VOID
XdpIfReleaseOffloadRundown(
    _In_ XDP_IFSET_HANDLE IfSetHandle
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;

    ExReleaseRundownProtection(&IfSet->OffloadRundown);
}

static
VOID
XdpIfpAcquireOffloadReference(
    _In_ XDP_INTERFACE_SET *IfSet
    )
{
    XdpIncrementReferenceCount(&IfSet->OffloadReferenceCount);
}

static
VOID
XdpIfpReleaseOffloadReference(
    _In_ XDP_INTERFACE_SET *IfSet
    )
{
    if (XdpDecrementReferenceCount(&IfSet->OffloadReferenceCount)) {
        KeSetEvent(IfSet->OffloadDowncallsComplete, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
XdpIfpReferenceInterfaceOffloadObject(
    _In_ XDP_IF_OFFLOAD_OBJECT *OffloadObject
    )
{
    XdpIncrementReferenceCount(&OffloadObject->ReferenceCount);
}

static
VOID
XdpIfpDereferenceInterfaceOffloadObject(
    _In_ XDP_IF_OFFLOAD_OBJECT *OffloadObject
    )
{
    if (XdpDecrementReferenceCount(&OffloadObject->ReferenceCount)) {
        ExFreePoolWithTag(OffloadObject, XDP_POOLTAG_IF_OFFLOAD);
    }
}

XDP_OFFLOAD_IF_SETTINGS *
XdpIfGetOffloadIfSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    )
{
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;

    UNREFERENCED_PARAMETER(IfSetHandle);

    return &OffloadObject->OffloadSettings;
}

static
VOID
XdpIfpCleanupInterfaceOffloadObject(
    _In_ XDP_INTERFACE_SET *IfSet,
    _In_ XDP_IF_OFFLOAD_OBJECT *OffloadObject
    )
{
    ASSERT(IsListEmpty(&OffloadObject->Entry));
    ASSERT(OffloadObject->InterfaceOffloadHandle != NULL);

    //
    // We can be here in one of two cases, whichever comes first:
    //
    // 1. Interface removal, in which case the interface offload has been
    //    run down and cleanup can safely proceed.
    // 2. Interface handle closure, in which case cleanup can safely
    //    proceed.
    //

    XdpOffloadRevertSettings((XDP_IFSET_HANDLE)IfSet, (XDP_IF_OFFLOAD_HANDLE)OffloadObject);

    IfSet->OffloadDispatch->CloseInterfaceOffloadHandle(OffloadObject->InterfaceOffloadHandle);
    OffloadObject->InterfaceOffloadHandle = NULL;
    XdpIfpReleaseOffloadReference(IfSet);
}

NTSTATUS
XdpIfOpenInterfaceOffloadHandle(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ CONST XDP_HOOK_ID *HookId,
    _Out_ XDP_IF_OFFLOAD_HANDLE *InterfaceOffloadHandle
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = NULL;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "IfIndex=%u IfSet=%p", IfSet->IfIndex, IfSet);

    OffloadObject =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*OffloadObject), XDP_POOLTAG_IF_OFFLOAD);
    if (OffloadObject == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&OffloadObject->ReferenceCount);
    InitializeListHead(&OffloadObject->Entry);
    XdpOffloadInitializeIfSettings(&OffloadObject->OffloadSettings);

    if (!ExAcquireRundownProtection(&IfSet->OffloadRundown)) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    Status =
        IfSet->OffloadDispatch->OpenInterfaceOffloadHandle(
            IfSet->XdpIfInterfaceSetContext, HookId, &OffloadObject->InterfaceOffloadHandle);

    if (NT_SUCCESS(Status)) {
        *InterfaceOffloadHandle = (XDP_IF_OFFLOAD_HANDLE)OffloadObject;

        XdpIfpAcquireOffloadReference(IfSet);

        RtlAcquirePushLockExclusive(&IfSet->Lock);
        InsertTailList(&IfSet->OffloadObjects, &OffloadObject->Entry);
        RtlReleasePushLockExclusive(&IfSet->Lock);

        TraceVerbose(
            TRACE_CORE, "IfIndex=%u IfSet=%p created OffloadObject=%p InterfaceOffloadHandle=%p",
            IfSet->IfIndex, IfSet, OffloadObject, OffloadObject->InterfaceOffloadHandle);
    }

    ExReleaseRundownProtection(&IfSet->OffloadRundown);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (OffloadObject != NULL) {
            XdpIfpDereferenceInterfaceOffloadObject(OffloadObject);
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpIfCloseInterfaceOffloadHandle(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;
    BOOLEAN NeedCleanup = FALSE;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u IfSet=%p OffloadObject=%p",
        IfSet->IfIndex, IfSet, OffloadObject);

    RtlAcquirePushLockExclusive(&IfSet->Lock);

    if (!IsListEmpty(&OffloadObject->Entry)) {
        RemoveEntryList(&OffloadObject->Entry);
        InitializeListHead(&OffloadObject->Entry);
        NeedCleanup = TRUE;
    }

    RtlReleasePushLockExclusive(&IfSet->Lock);

    if (NeedCleanup) {
        XdpIfpCleanupInterfaceOffloadObject(IfSet, OffloadObject);
    }

    XdpIfpDereferenceInterfaceOffloadObject(OffloadObject);

    TraceExitSuccess(TRACE_CORE);
}

NTSTATUS
XdpIfGetInterfaceOffloadCapabilities(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadCapabilities,
    _Inout_ UINT32 *OffloadCapabilitiesSize
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u IfSet=%p OffloadObject=%p",
        IfSet->IfIndex, IfSet, OffloadObject);

    if (!ExAcquireRundownProtection(&IfSet->OffloadRundown)) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    Status =
        IfSet->OffloadDispatch->GetInterfaceOffloadCapabilities(
            OffloadObject->InterfaceOffloadHandle, OffloadType, OffloadCapabilities,
            OffloadCapabilitiesSize);

    ExReleaseRundownProtection(&IfSet->OffloadRundown);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

NTSTATUS
XdpIfGetInterfaceOffload(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _Out_opt_ VOID *OffloadParams,
    _Inout_ UINT32 *OffloadParamsSize
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u IfSet=%p OffloadObject=%p",
        IfSet->IfIndex, IfSet, OffloadObject);

    if (!ExAcquireRundownProtection(&IfSet->OffloadRundown)) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    Status =
        IfSet->OffloadDispatch->GetInterfaceOffload(
            OffloadObject->InterfaceOffloadHandle, OffloadType, OffloadParams, OffloadParamsSize);

    ExReleaseRundownProtection(&IfSet->OffloadRundown);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

NTSTATUS
XdpIfSetInterfaceOffload(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u IfSet=%p OffloadObject=%p",
        IfSet->IfIndex, IfSet, OffloadObject);

    if (!ExAcquireRundownProtection(&IfSet->OffloadRundown)) {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    Status =
        IfSet->OffloadDispatch->SetInterfaceOffload(
            OffloadObject->InterfaceOffloadHandle, OffloadType, OffloadParams, OffloadParamsSize);

    ExReleaseRundownProtection(&IfSet->OffloadRundown);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

NTSTATUS
XdpIfRevertInterfaceOffload(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _In_ XDP_INTERFACE_OFFLOAD_TYPE OffloadType,
    _In_ VOID *OffloadParams,
    _In_ UINT32 OffloadParamsSize
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)IfSetHandle;
    XDP_IF_OFFLOAD_OBJECT *OffloadObject = (XDP_IF_OFFLOAD_OBJECT *)InterfaceOffloadHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u IfSet=%p OffloadObject=%p",
        IfSet->IfIndex, IfSet, OffloadObject);

    //
    // This routine is identical to XdpIfSetInterfaceOffload, except it can be
    // called while the offload interface is being run down. It must not be
    // invoked after the offload interface has been cleaned up. The gratuitous
    // reference counts below assert this routine is invoked before clean up.
    //

    //
    // The RTL reference count routine asserts the reference count did not
    // bounce off zero.
    //
    XdpIncrementReferenceCount(&IfSet->OffloadReferenceCount);

    Status =
        IfSet->OffloadDispatch->SetInterfaceOffload(
            OffloadObject->InterfaceOffloadHandle, OffloadType, OffloadParams, OffloadParamsSize);

    //
    // Verify the caller implicitly held a reference on the offload for the
    // duration of the downcall.
    //
    FRE_ASSERT(!XdpDecrementReferenceCount(&IfSet->OffloadReferenceCount));

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
VOID
XdpIfpStartRundown(
    _In_ XDP_INTERFACE *Interface
    )
{
    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    while (!IsListEmpty(&Interface->Clients)) {
        XDP_BINDING_CLIENT_ENTRY *ClientEntry =
            CONTAINING_RECORD(Interface->Clients.Flink, XDP_BINDING_CLIENT_ENTRY, Link);

        RemoveEntryList(&ClientEntry->Link);
        InitializeListHead(&ClientEntry->Link);

        ClientEntry->Client->BindingDetached(ClientEntry);

        XdpIfpDereferenceInterface(Interface);
    }

    if (Interface->XdpDriverApi.ProviderReference == 0) {
        XdpIfpCloseDriverInterface(Interface);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpIfpRemoveXdpIfInterface(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)Item->BindingHandle;
    XDP_INTERFACE_SET *IfSet;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    ASSERT(!Interface->XdpIfApi.InterfaceRemoving);
    Interface->XdpIfApi.InterfaceRemoving = TRUE;

    XdpIfpStartRundown(Interface);

    RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);
    Interface->IfSet->Interfaces[Interface->Capabilities.Mode] = NULL;
    IfSet = Interface->IfSet;
    Interface->IfSet = NULL;
    RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);

    XdpIfpDereferenceIfSet(IfSet);

    ASSERT(Interface->XdpIfApi.InterfaceContext != NULL);
    Interface->XdpIfApi.RemoveInterfaceComplete(Interface->XdpIfApi.InterfaceContext);
    Interface->XdpIfApi.InterfaceContext = NULL;

    TraceVerbose(TRACE_CORE, "XDPIF interface removal completed");

    //
    // Release the initial interface reference.
    //
    XdpIfpDereferenceInterface(Interface);

    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpIfpInterfaceNmrDelete(
    _In_ XDP_BINDING_WORKITEM *Item
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)Item->BindingHandle;
    XDP_INTERFACE_NMR *Nmr = CONTAINING_RECORD(Item, XDP_INTERFACE_NMR, WorkItem);

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE!",
        Interface->IfIndex, Interface->Capabilities.Mode);

    if (Nmr->NmrHandle != NULL) {
        ASSERT(Interface->NmrDeleting == FALSE);
        Interface->NmrDeleting = TRUE;

        XdpIfpStartRundown(Interface);

        if (Interface->Nmr != NULL) {
            XdpIfpCloseNmrInterface(Interface);
            Interface->NmrDeleting = FALSE;
        }
    }

    XdpIfpDereferenceNmr(Nmr);

    TraceExitSuccess(TRACE_CORE);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfpInterfaceWorker(
    _In_ SINGLE_LIST_ENTRY *WorkQueueHead
    )
{
    while (WorkQueueHead != NULL) {
        XDP_BINDING_WORKITEM *Item;
        XDP_INTERFACE *Interface;
        GROUP_AFFINITY Affinity;
        GROUP_AFFINITY OldAffinity;

        Item = CONTAINING_RECORD(WorkQueueHead, XDP_BINDING_WORKITEM, Link);
        Interface = (XDP_INTERFACE *)Item->BindingHandle;
        WorkQueueHead = WorkQueueHead->Next;

        //
        // Perform work on the original caller's NUMA node. Note that WS2022
        // introduces a multi-affinity-group NUMA concept not implemented here.
        //
        KeQueryNodeActiveAffinity(Item->IdealNode, &Affinity, NULL);
        KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

        Item->WorkRoutine(Item);

        KeRevertToUserGroupAffinityThread(&OldAffinity);

        XdpIfpDereferenceInterface(Interface);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfCreateInterfaceSet(
    _In_ NET_IFINDEX IfIndex,
    _In_ CONST XDP_OFFLOAD_DISPATCH *OffloadDispatch,
    _In_ VOID *InterfaceSetContext,
    _Out_ XDPIF_INTERFACE_SET_HANDLE *InterfaceSetHandle
    )
{
    NTSTATUS Status;
    XDP_INTERFACE_SET *IfSet = NULL;

    //
    // This function is invoked by an interface provider (e.g. NDIS6 via XdpLwf)
    // when a NIC is added.
    //

    TraceEnter(TRACE_CORE, "IfIndex=%u", IfIndex);

    RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);

    //
    // Check for duplicate interface set.
    //
    if (XdpIfpFindIfSet(IfIndex) != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    IfSet = ExAllocatePoolZero(PagedPool, sizeof(*IfSet), XDP_POOLTAG_IFSET);
    if (IfSet == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    IfSet->IfIndex = IfIndex;
    IfSet->OffloadDispatch = OffloadDispatch;
    IfSet->XdpIfInterfaceSetContext = InterfaceSetContext;
    XdpInitializeReferenceCount(&IfSet->ReferenceCount);
    InitializeListHead(&IfSet->Link);
    ExInitializeRundownProtection(&IfSet->OffloadRundown);
    XdpInitializeReferenceCount(&IfSet->OffloadReferenceCount);
    ExInitializePushLock(&IfSet->Lock);
    InitializeListHead(&IfSet->OffloadObjects);
    InsertTailList(&XdpInterfaceSets, &IfSet->Link);

    *InterfaceSetHandle = (XDPIF_INTERFACE_SET_HANDLE)IfSet;
    Status = STATUS_SUCCESS;

    TraceVerbose(
        TRACE_CORE, "IfIndex=%u IfSet=%p XdpIfInterfaceSetContext=%p Created",
        IfSet->IfIndex, IfSet, IfSet->XdpIfInterfaceSetContext);

Exit:

    RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfDeleteInterfaceSet(
    _In_ XDPIF_INTERFACE_SET_HANDLE InterfaceSetHandle
    )
{
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)InterfaceSetHandle;
    KEVENT OffloadDowncallsComplete;

    //
    // This function is invoked by an interface provider (e.g. XDP LWF)
    // when a NIC is deleted.
    //

    RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);

    TraceVerbose(
        TRACE_CORE, "IfIndex=%u XdpIfInterfaceSetContext=%p Deleted",
        IfSet->IfIndex, IfSet->XdpIfInterfaceSetContext);

    RemoveEntryList(&IfSet->Link);
    InitializeListHead(&IfSet->Link);

    RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);

    //
    // 1. Prevent new IF object creation / new IOCTLs.
    // 2. Wait for all oustanding get/set operations to quiesce.
    // 3. Tear down each IF object: revert settings, close IF handle.
    //

    ExWaitForRundownProtectionRelease(&IfSet->OffloadRundown);

    RtlAcquirePushLockExclusive(&IfSet->Lock);

    while (!IsListEmpty(&IfSet->OffloadObjects)) {
        LIST_ENTRY *Entry = RemoveHeadList(&IfSet->OffloadObjects);
        XDP_IF_OFFLOAD_OBJECT *OffloadObject =
            CONTAINING_RECORD(Entry, XDP_IF_OFFLOAD_OBJECT, Entry);

        InitializeListHead(&OffloadObject->Entry);
        XdpIfpReferenceInterfaceOffloadObject(OffloadObject);

        RtlReleasePushLockExclusive(&IfSet->Lock);

        XdpIfpCleanupInterfaceOffloadObject(IfSet, OffloadObject);
        XdpIfpDereferenceInterfaceOffloadObject(OffloadObject);

        RtlAcquirePushLockExclusive(&IfSet->Lock);
    }

    RtlReleasePushLockExclusive(&IfSet->Lock);

    //
    // Release initialize reference to offload interface and wait for all
    // offload downcalls to complete.
    //
    KeInitializeEvent(&OffloadDowncallsComplete, NotificationEvent, FALSE);
    IfSet->OffloadDowncallsComplete = &OffloadDowncallsComplete;
    XdpIfpReleaseOffloadReference(IfSet);
    KeWaitForSingleObject(&OffloadDowncallsComplete, Executive, KernelMode, FALSE, NULL);

    XdpIfpDereferenceIfSet(IfSet);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpIfAddInterfaces(
    _In_ XDPIF_INTERFACE_SET_HANDLE InterfaceSetHandle,
    _Inout_ XDP_ADD_INTERFACE *Interfaces,
    _In_ UINT32 InterfaceCount
    )
{
    NTSTATUS Status;
    XDP_INTERFACE_SET *IfSet = (XDP_INTERFACE_SET *)InterfaceSetHandle;

    //
    // This function is invoked by an interface provider (e.g. NDIS6 via XdpLwf)
    // when a NIC is added.
    //

    TraceEnter(TRACE_CORE, "IfIndex=%u", IfSet->IfIndex);

    RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);

    for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
        XDP_ADD_INTERFACE *AddIf = &Interfaces[Index];
        XDP_INTERFACE *Interface = NULL;

        if (!XdpValidateCapabilitiesEx(
                AddIf->InterfaceCapabilities->CapabilitiesEx,
                AddIf->InterfaceCapabilities->CapabilitiesSize)) {
            TraceError(
                TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Invalid capabilities",
                IfSet->IfIndex, AddIf->InterfaceCapabilities->Mode);
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }

        Interface = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Interface), XDP_POOLTAG_IF);
        if (Interface == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        XdpInitializeReferenceCount(&Interface->ReferenceCount);
        Interface->IfIndex = IfSet->IfIndex;
        Interface->IfSet = IfSet;
        Interface->XdpIfApi.RemoveInterfaceComplete = AddIf->RemoveInterfaceComplete;
        Interface->XdpIfApi.InterfaceContext = AddIf->InterfaceContext;
        Interface->XdpDriverApi.OpenConfig.Dispatch = &XdpOpenDispatch;
        RtlCopyMemory(
            (XDP_CAPABILITIES_INTERNAL *)&Interface->Capabilities,
            AddIf->InterfaceCapabilities,
            sizeof(Interface->Capabilities));
        InitializeListHead(&Interface->Clients);

        Interface->WorkQueue =
            XdpCreateWorkQueue(XdpIfpInterfaceWorker, DISPATCH_LEVEL, XdpDriverObject, NULL);
        if (Interface->WorkQueue == NULL) {
            ExFreePoolWithTag(Interface, XDP_POOLTAG_IF);
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        XdpIfpReferenceIfSet(IfSet);
        ASSERT(IfSet->Interfaces[Interface->Capabilities.Mode] == NULL);
        IfSet->Interfaces[Interface->Capabilities.Mode] = Interface;
        *AddIf->InterfaceHandle = (XDPIF_INTERFACE_HANDLE)Interface;

        TraceVerbose(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! XdpIfInterfaceContext=%p Added",
            Interface->IfIndex, Interface->Capabilities.Mode,
            Interface->XdpIfApi.InterfaceContext);
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
            if (*Interfaces[Index].InterfaceHandle != NULL) {
                XDP_INTERFACE *Interface;
                Interface = (XDP_INTERFACE *)(*Interfaces[Index].InterfaceHandle);
                ASSERT(IfSet);
                IfSet->Interfaces[Interface->Capabilities.Mode] = NULL;
            }
        }
    }

    RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);

    if (!NT_SUCCESS(Status)) {
        for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
            if (*Interfaces[Index].InterfaceHandle != NULL) {
                XDP_INTERFACE *Interface;
                Interface = (XDP_INTERFACE *)(*Interfaces[Index].InterfaceHandle);
                ASSERT(IfSet);
                *Interfaces[Index].InterfaceHandle = NULL;
                XdpIfpDereferenceInterface(Interface);
                XdpIfpDereferenceIfSet(IfSet);
            }
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpIfRemoveInterfaces(
    _In_ XDPIF_INTERFACE_HANDLE *Interfaces,
    _In_ UINT32 InterfaceCount
    )
{
    //
    // This function is invoked by an interface provider (e.g. XDP LWF)
    // when a NIC is deleted.
    //

    for (UINT32 Index = 0; Index < InterfaceCount; Index++) {
        XDP_INTERFACE *Interface = (XDP_INTERFACE *)Interfaces[Index];

        TraceVerbose(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! Removing",
            Interface->IfIndex, Interface->Capabilities.Mode);

        RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);
        //
        // The caller must not remove interfaces after the interface set has
        // been deleted.
        //
        FRE_ASSERT(!IsListEmpty(&Interface->IfSet->Link));
        RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);

        Interface->RemoveWorkItem.BindingHandle = (XDP_BINDING_HANDLE)Interface;
        Interface->RemoveWorkItem.WorkRoutine = XdpIfpRemoveXdpIfInterface;
        XdpIfQueueWorkItem(&Interface->RemoveWorkItem);
    }
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
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    LIST_ENTRY *Entry;

    FRE_ASSERT(Client->ClientId != XDP_BINDING_CLIENT_ID_INVALID);
    FRE_ASSERT(Client->KeySize > 0);
    FRE_ASSERT(Key != NULL);

    if (Interface->XdpIfApi.InterfaceRemoving) {
        TraceInfo(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! client registration failed: interface removing",
            Interface->IfIndex, Interface->Capabilities.Mode);
        return STATUS_DELETE_PENDING;
    }

    //
    // Verify we're not inserting a duplicate client.
    //
    Entry = Interface->Clients.Flink;
    while (Entry != &Interface->Clients) {
        XDP_BINDING_CLIENT_ENTRY *Candidate =
            CONTAINING_RECORD(Entry, XDP_BINDING_CLIENT_ENTRY, Link);
        Entry = Entry->Flink;

        if (!NT_VERIFY(
                (Candidate->Client->ClientId != Client->ClientId) ||
                !RtlEqualMemory(Candidate->Key, Key, Client->KeySize))) {
            TraceInfo(
                TRACE_CORE,
                "IfIndex=%u Mode=%!XDP_MODE! client registration failed: duplicate client",
                Interface->IfIndex, Interface->Capabilities.Mode);
            return STATUS_DUPLICATE_OBJECTID;
        }
    }

    ClientEntry->Client = Client;
    ClientEntry->Key = Key;
    XdpIfpReferenceInterface(Interface);
    InsertTailList(&Interface->Clients, &ClientEntry->Link);

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeregisterClient(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;

    //
    // Invoked by XDP components (e.g. programs, XSKs) to detach from an
    // interface binding.
    //

    if (!IsListEmpty(&ClientEntry->Link)) {
        RemoveEntryList(&ClientEntry->Link);
        InitializeListHead(&ClientEntry->Link);
        XdpIfpDereferenceInterface(Interface);
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
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    LIST_ENTRY *Entry;
    XDP_BINDING_CLIENT_ENTRY *Candidate;

    Entry = Interface->Clients.Flink;
    while (Entry != &Interface->Clients) {
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
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;

    return Interface->IfIndex;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpReferenceProvider(
    _In_ XDP_INTERFACE *Interface
    )
{
    NTSTATUS Status;

    if (Interface->XdpIfApi.InterfaceRemoving || Interface->NmrDeleting) {
        Status = STATUS_DELETE_PENDING;
        TraceInfo(
            TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! reference failed: rundown",
            Interface->IfIndex, Interface->Capabilities.Mode);
        goto Exit;
    }

    if (Interface->XdpDriverApi.InterfaceContext == NULL) {
        ASSERT(Interface->XdpDriverApi.ProviderReference == 0);
        Status = XdpIfpOpenInterface(Interface);
        if (!NT_SUCCESS(Status)) {
            TraceInfo(
                TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! reference failed: open interface",
                Interface->IfIndex, Interface->Capabilities.Mode);
            goto Exit;
        }
    }

    Interface->XdpDriverApi.ProviderReference++;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfpDereferenceProvider(
    _In_ XDP_INTERFACE *Interface
    )
{
    if (--Interface->XdpDriverApi.ProviderReference == 0) {
        XdpIfpCloseInterface(Interface);
    }
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpInvokeDriverCreateRxQueue(
    _In_ XDP_INTERFACE *Interface,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    )
{
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Interface->IfIndex, Interface->Capabilities.Mode,
        XdpRxQueueGetTargetQueueInfo(Config)->QueueId);

    if (XdpFaultInject()) {
        Status = STATUS_NO_MEMORY;
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u CreateRxQueue fault inject",
            Interface->IfIndex, Interface->Capabilities.Mode,
            XdpRxQueueGetTargetQueueInfo(Config)->QueueId);
        goto Exit;
    }

    Status =
        Interface->XdpDriverApi.InterfaceDispatch->CreateRxQueue(
            Interface->XdpDriverApi.InterfaceContext, Config, InterfaceRxQueue,
            InterfaceRxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u CreateRxQueue failed Status=%!STATUS!",
            Interface->IfIndex, Interface->Capabilities.Mode,
            XdpRxQueueGetTargetQueueInfo(Config)->QueueId, Status);
        goto Exit;
    }

Exit:

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
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Interface->IfIndex, Interface->Capabilities.Mode,
        XdpRxQueueGetTargetQueueInfo(Config)->QueueId);

    *InterfaceRxQueue = NULL;
    *InterfaceRxQueueDispatch = NULL;

    Status = XdpIfpReferenceProvider(Interface);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpIfpInvokeDriverCreateRxQueue(
            Interface, Config, InterfaceRxQueue, InterfaceRxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        XdpIfpDereferenceProvider(Interface);
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
NTSTATUS
XdpIfActivateRxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Interface->IfIndex, Interface->Capabilities.Mode, InterfaceRxQueue);

    if (XdpFaultInject()) {
        Status = STATUS_NO_MEMORY;
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p ActivateRxQueue fault inject",
            Interface->IfIndex, Interface->Capabilities.Mode, InterfaceRxQueue);
        goto Exit;
    }

    Status =
        Interface->XdpDriverApi.InterfaceDispatch->ActivateRxQueue(
            InterfaceRxQueue, XdpRxQueue, Config);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p ActivateRxQueue failed Status=%!STATUS!",
            Interface->IfIndex, Interface->Capabilities.Mode, InterfaceRxQueue, Status);
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeleteRxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Interface->IfIndex, Interface->Capabilities.Mode, InterfaceRxQueue);

    Interface->XdpDriverApi.InterfaceDispatch->DeleteRxQueue(InterfaceRxQueue);

    XdpIfpDereferenceProvider(Interface);

    TraceExitSuccess(TRACE_CORE);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpIfpInvokeDriverCreateTxQueue(
    _In_ XDP_INTERFACE *Interface,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    )
{
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Interface->IfIndex, Interface->Capabilities.Mode,
        XdpTxQueueGetTargetQueueInfo(Config)->QueueId);

    if (XdpFaultInject()) {
        Status = STATUS_NO_MEMORY;
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u CreateTxQueue fault inject",
            Interface->IfIndex, Interface->Capabilities.Mode,
            XdpTxQueueGetTargetQueueInfo(Config)->QueueId);
        goto Exit;
    }

    Status =
        Interface->XdpDriverApi.InterfaceDispatch->CreateTxQueue(
            Interface->XdpDriverApi.InterfaceContext, Config, InterfaceTxQueue,
            InterfaceTxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u CreateTxQueue failed Status=%!STATUS!",
            Interface->IfIndex, Interface->Capabilities.Mode,
            XdpTxQueueGetTargetQueueInfo(Config)->QueueId, Status);
        goto Exit;
    }

Exit:

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
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! QueueId=%u",
        Interface->IfIndex, Interface->Capabilities.Mode,
        XdpTxQueueGetTargetQueueInfo(Config)->QueueId);

    *InterfaceTxQueue = NULL;
    *InterfaceTxQueueDispatch = NULL;

    Status = XdpIfpReferenceProvider(Interface);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpIfpInvokeDriverCreateTxQueue(
            Interface, Config, InterfaceTxQueue, InterfaceTxQueueDispatch);
    if (!NT_SUCCESS(Status)) {
        XdpIfpDereferenceProvider(Interface);
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
NTSTATUS
XdpIfActivateTxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Interface->IfIndex, Interface->Capabilities.Mode, InterfaceTxQueue);

    if (XdpFaultInject()) {
        Status = STATUS_NO_MEMORY;
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p ActivateTxQueue fault inject",
            Interface->IfIndex, Interface->Capabilities.Mode, InterfaceTxQueue);
        goto Exit;
    }

    Status =
        Interface->XdpDriverApi.InterfaceDispatch->ActivateTxQueue(
            InterfaceTxQueue, XdpTxQueue, Config);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE,
            "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p ActivateTxQueue failed Status=%!STATUS!",
            Interface->IfIndex, Interface->Capabilities.Mode, InterfaceTxQueue, Status);
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpIfDeleteTxQueue(
    _In_ XDP_BINDING_HANDLE BindingHandle,
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    )
{
    XDP_INTERFACE *Interface = (XDP_INTERFACE *)BindingHandle;

    TraceEnter(
        TRACE_CORE, "IfIndex=%u Mode=%!XDP_MODE! InterfaceQueue=%p",
        Interface->IfIndex, Interface->Capabilities.Mode, InterfaceTxQueue);

    Interface->XdpDriverApi.InterfaceDispatch->DeleteTxQueue(InterfaceTxQueue);

    XdpIfpDereferenceProvider(Interface);

    TraceExitSuccess(TRACE_CORE);
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIfStart(
    VOID
    )
{
    ExInitializePushLock(&XdpInterfaceSetsLock);
    InitializeListHead(&XdpInterfaceSets);
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

    RtlAcquirePushLockExclusive(&XdpInterfaceSetsLock);

    ASSERT(IsListEmpty(&XdpInterfaceSets));

    RtlReleasePushLockExclusive(&XdpInterfaceSetsLock);
}
