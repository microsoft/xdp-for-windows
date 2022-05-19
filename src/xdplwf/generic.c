//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "generic.tmh"

#define DELAY_DETACH_DEFAULT_TIMEOUT_SEC (5 * 60)
#define DELAY_DETACH_RX 0x1

static UINT32 GenericDelayDetachTimeoutMs = RTL_SEC_TO_MILLISEC(DELAY_DETACH_DEFAULT_TIMEOUT_SEC);
static XDP_REG_WATCHER_CLIENT_ENTRY GenericRegWatcher = {0};

static
CONST
XDP_HOOK_ID GenericHooks[] = {
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_RX,
        .SubLayer   = XDP_HOOK_INSPECT,
    },
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_TX,
        .SubLayer   = XDP_HOOK_INJECT,
    },
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_RX,
        .SubLayer   = XDP_HOOK_INJECT,
    },
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_TX,
        .SubLayer   = XDP_HOOK_INSPECT,
    },
};

static
VOID
XdpGenericReference(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    XdpIncrementReferenceCount(&Generic->ReferenceCount);
}

static
VOID
XdpGenericDereference(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    if (XdpDecrementReferenceCount(&Generic->ReferenceCount)) {
        KeSetEvent(&Generic->CleanupEvent, 0, FALSE);
    }
}

XDP_LWF_GENERIC *
XdpGenericFromFilterContext(
    _In_ NDIS_HANDLE FilterModuleContext
    )
{
    XDP_LWF_FILTER *Filter = (XDP_LWF_FILTER *)FilterModuleContext;
    return &Filter->Generic;
}

static
VOID
XdpGenericUpdateDelayDetachTimeout(
    _In_ UINT32 NewTimeoutSeconds
    )
{
    UINT32 NewTimeoutMs;

    if (!NT_SUCCESS(RtlUInt32Mult(NewTimeoutSeconds, 1000, &NewTimeoutMs))) {
        NewTimeoutMs = RTL_SEC_TO_MILLISEC(DELAY_DETACH_DEFAULT_TIMEOUT_SEC);
    }

    if (NewTimeoutMs != GenericDelayDetachTimeoutMs) {
        GenericDelayDetachTimeoutMs = NewTimeoutMs;
        TraceInfo(TRACE_GENERIC, "GenericDelayDetachTimeoutMs=%u", GenericDelayDetachTimeoutMs);
    }
}

static
VOID
XdpGenericRegistryUpdate(
    VOID
    )
{
    NTSTATUS Status;
    DWORD Value;

    Status =
        XdpRegQueryDwordValue(
            XDP_LWF_PARAMETERS_KEY, L"GenericDelayDetachTimeoutSec", &Value);
    if (NT_SUCCESS(Status)) {
        XdpGenericUpdateDelayDetachTimeout(Value);
    } else {
        XdpGenericUpdateDelayDetachTimeout(DELAY_DETACH_DEFAULT_TIMEOUT_SEC);
    }
}

VOID
XdpGenericPause(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    TraceVerbose(TRACE_GENERIC, "IfIndex=%u Datapath is pausing", Generic->IfIndex);

    RtlAcquirePushLockExclusive(&Generic->Lock);
    KeClearEvent(&Generic->Tx.Datapath.ReadyEvent);
    KeClearEvent(&Generic->Rx.Datapath.ReadyEvent);

    XdpGenericTxPause(Generic);
    RtlReleasePushLockExclusive(&Generic->Lock);

    TraceVerbose(TRACE_GENERIC, "IfIndex=%u Datapath is paused", Generic->IfIndex);
}

VOID
XdpGenericRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_FILTER_RESTART_PARAMETERS *RestartParameters
    )
{
    NDIS_RESTART_ATTRIBUTES *Entry;
    NDIS_RESTART_GENERAL_ATTRIBUTES *GeneralAttributes = NULL;
    UINT32 NewMtu = 0;

    TraceVerbose(TRACE_GENERIC, "IfIndex=%u Datapath is restarting", Generic->IfIndex);

    for (Entry = RestartParameters->RestartAttributes; Entry != NULL; Entry = Entry->Next) {
        if (Entry->Oid == OID_GEN_MINIPORT_RESTART_ATTRIBUTES &&
            Entry->DataLength >= sizeof(*GeneralAttributes)) {
            GeneralAttributes = (NDIS_RESTART_GENERAL_ATTRIBUTES *)Entry->Data;
            NewMtu = GeneralAttributes->MtuSize + sizeof(ETHERNET_HEADER);
        }
    }

    RtlAcquirePushLockExclusive(&Generic->Lock);
    if (Generic->Tx.Datapath.Inserted) {
        KeSetEvent(&Generic->Tx.Datapath.ReadyEvent, 0, FALSE);
    }
    if (Generic->Rx.Datapath.Inserted) {
        KeSetEvent(&Generic->Rx.Datapath.ReadyEvent, 0, FALSE);
    }

    XdpGenericTxRestart(Generic, NewMtu);
    RtlReleasePushLockExclusive(&Generic->Lock);

    TraceVerbose(TRACE_GENERIC, "IfIndex=%u Datapath is restarted", Generic->IfIndex);
}

static
VOID *
XdpGenericPackContext(
    _In_ CONST XDP_LWF_GENERIC *Generic,
    _In_ CONST XDP_LWF_DATAPATH_BYPASS *Datapath
    )
{
    ASSERT(((ULONG_PTR)Generic & DELAY_DETACH_RX) == 0);
    return
        (VOID *)((ULONG_PTR)Generic |
            ((Datapath == &Generic->Rx.Datapath) ? DELAY_DETACH_RX : 0));
}

static
VOID
XdpGenericUnpackContext(
    _In_ VOID * PackedContext,
    _Out_ CONST XDP_LWF_GENERIC **Generic,
    _Out_ CONST XDP_LWF_DATAPATH_BYPASS **Datapath
    )
{
    *Generic = (XDP_LWF_GENERIC *)((ULONG_PTR)PackedContext & ~DELAY_DETACH_RX);
    *Datapath =
        ((ULONG_PTR)PackedContext & DELAY_DETACH_RX) ?
            &(*Generic)->Rx.Datapath : &(*Generic)->Tx.Datapath;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(WORKER_THREAD_ROUTINE)
VOID
XdpGenericDelayDereferenceDatapath(
   _In_ VOID *PackedContext
   )
{
    XDP_LWF_GENERIC *Generic;
    XDP_LWF_DATAPATH_BYPASS *Datapath;
    BOOLEAN NeedRestart = FALSE;

    XdpGenericUnpackContext(PackedContext, &Generic, &Datapath);

    RtlAcquirePushLockExclusive(&Generic->Lock);

    FRE_ASSERT(Datapath->ReferenceCount > 0);
    if (--Datapath->ReferenceCount == 0) {
        TraceVerbose(
            TRACE_GENERIC, "IfIndex=%u Requesting %s datapath detach",
            Generic->IfIndex, (Datapath == &Generic->Rx.Datapath) ? "RX" : "TX");
        KeClearEvent(&Datapath->ReadyEvent);
        NeedRestart = TRUE;
    }
    RtlReleasePushLockExclusive(&Generic->Lock);

    if (NeedRestart) {
        XdpGenericRequestRestart(Generic);
        XdpGenericDereference(Generic);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_lock_held_(&Generic->Lock)
VOID
XdpGenericReferenceDatapath(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath,
    _Out_ BOOLEAN *NeedRestart
    )
{
    *NeedRestart = FALSE;

    FRE_ASSERT(Datapath->ReferenceCount >= 0);

    //
    // The data path is being referenced, so cancel any delayed detach timer.
    // If we successfully cancelled the timer, then we can simply transfer its
    // data path reference (and implicit generic reference) to this caller.
    //
    if (XdpTimerCancel(Datapath->DelayDetachTimer)) {
        return;
    }

    if (Datapath->ReferenceCount++ == 0) {
        XdpGenericReference(Generic);
        *NeedRestart = TRUE;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_lock_held_(&Generic->Lock)
VOID
XdpGenericDereferenceDatapath(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath,
    _Out_ BOOLEAN *NeedRestart
    )
{
    *NeedRestart = FALSE;

    if (Datapath->ReferenceCount == 1) {
        if (Datapath->DelayDetachTimer != NULL) {
            BOOLEAN StartedTimer;
            BOOLEAN CancelledTimer;

            CancelledTimer =
                XdpTimerStart(
                    Datapath->DelayDetachTimer, GenericDelayDetachTimeoutMs, &StartedTimer);

            FRE_ASSERT(!CancelledTimer);
            FRE_ASSERT(StartedTimer);
            return;
        }

        KeClearEvent(&Datapath->ReadyEvent);
        *NeedRestart = TRUE;
    }

    FRE_ASSERT(Datapath->ReferenceCount > 0);
    --Datapath->ReferenceCount;

    if (*NeedRestart) {
        XdpGenericDereference(Generic);
    }
}

NDIS_STATUS
XdpGenericFilterSetOptions(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_FILTER_PARTIAL_CHARACTERISTICS Handlers = {0};
    BOOLEAN RxInserted = FALSE;
    BOOLEAN TxInserted = FALSE;

    Handlers.Header.Type = NDIS_OBJECT_TYPE_FILTER_PARTIAL_CHARACTERISTICS;
    Handlers.Header.Revision = NDIS_FILTER_PARTIAL_CHARACTERISTICS_REVISION_1;
    Handlers.Header.Size = NDIS_SIZEOF_FILTER_PARTIAL_CHARACTERISTICS_REVISION_1;

    RtlAcquirePushLockExclusive(&Generic->Lock);

    if (Generic->Rx.Datapath.ReferenceCount > 0) {
        Handlers.ReceiveNetBufferListsHandler = XdpGenericReceiveNetBufferLists;
        Handlers.ReturnNetBufferListsHandler = XdpGenericReturnNetBufferLists;
        RxInserted = TRUE;
    }
    if (Generic->Tx.Datapath.ReferenceCount > 0) {
        Handlers.SendNetBufferListsHandler = XdpGenericSendNetBufferLists;
        Handlers.SendNetBufferListsCompleteHandler = XdpGenericSendNetBufferListsComplete;
        TxInserted = TRUE;
    }

    RtlReleasePushLockExclusive(&Generic->Lock);

    Status =
        NdisSetOptionalHandlers(
            Generic->NdisFilterHandle, (NDIS_DRIVER_OPTIONAL_HANDLERS *)&Handlers);

    TraceVerbose(
        TRACE_GENERIC, "IfIndex=%u Set datapath handlers RX=%u TX=%u Status=%!STATUS!",
        Generic->IfIndex, RxInserted, TxInserted, Status);

    if (Status == NDIS_STATUS_SUCCESS) {
        RtlAcquirePushLockExclusive(&Generic->Lock);
        Generic->Rx.Datapath.Inserted = RxInserted;
        Generic->Tx.Datapath.Inserted = TxInserted;
        RtlReleasePushLockExclusive(&Generic->Lock);
    }

    return Status;
}

NDIS_STATUS
XdpGenericInspectOidRequest(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_OID_REQUEST *Request
    )
{
    NDIS_STATUS Status;

    Status = XdpGenericRssInspectOidRequest(Generic, Request);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    return Status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpGenericRequestRestart(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    NDIS_STATUS Status;

    TraceVerbose(TRACE_GENERIC, "IfIndex=%u Requesting datapath restart", Generic->IfIndex);
    Status = NdisFRestartFilter(Generic->NdisFilterHandle);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NT_VERIFY(NT_SUCCESS(XdpConvertNdisStatusToNtStatus(Status)));
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericOpenInterface(
    _In_ VOID *InterfaceContext,
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    )
{
    XDP_LWF_GENERIC *Generic = InterfaceContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(InterfaceConfig);

    Status = XdpGenericRssInitialize(Generic);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    return Status;
}

static
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpGenericCloseInterface(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext
    )
{
    XDP_LWF_GENERIC *Generic = (XDP_LWF_GENERIC *)InterfaceContext;

    XdpGenericRssCleanup(Generic);
}

static CONST XDP_INTERFACE_DISPATCH XdpGenericDispatch = {
    .Header             = {
        .Revision       = XDP_INTERFACE_DISPATCH_REVISION_1,
        .Size           = XDP_SIZEOF_INTERFACE_DISPATCH_REVISION_1
    },
    .OpenInterface      = XdpGenericOpenInterface,
    .CloseInterface     = XdpGenericCloseInterface,
    .CreateRxQueue      = XdpGenericRxCreateQueue,
    .ActivateRxQueue    = XdpGenericRxActivateQueue,
    .DeleteRxQueue      = XdpGenericRxDeleteQueue,
    .CreateTxQueue      = XdpGenericTxCreateQueue,
    .ActivateTxQueue    = XdpGenericTxActivateQueue,
    .DeleteTxQueue      = XdpGenericTxDeleteQueue,
};

static
VOID
XdpGenericCleanupDatapath(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath
    )
{
    if (Datapath->DelayDetachTimer != NULL) {
        XDP_TIMER *DelayDetachTimer;

        RtlAcquirePushLockExclusive(&Generic->Lock);

        DelayDetachTimer = Datapath->DelayDetachTimer;
        Datapath->DelayDetachTimer = NULL;

        if (XdpTimerShutdown(DelayDetachTimer, TRUE, FALSE)) {
            BOOLEAN NeedRestart;

            //
            // The delay detach timer was active and was cancelled, so release
            // the delayed reference immediately. Since we are in the teardown
            // path, there's no need to request the NDIS data path restart: it
            // will be rejected by NDIS anyways.
            //
            XdpGenericDereferenceDatapath(Generic, Datapath, &NeedRestart);
        }

        RtlReleasePushLockExclusive(&Generic->Lock);
    }
}

static
VOID
XdpGenericCleanupInterface(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    XdpGenericCleanupDatapath(Generic, &Generic->Tx.Datapath);
    XdpGenericCleanupDatapath(Generic, &Generic->Rx.Datapath);

    if (Generic->Registration != NULL) {
        XdpDeregisterInterface(Generic->Registration);
        Generic->Registration = NULL;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericRemoveInterfaceComplete(
    _In_ VOID *InterfaceContext
    )
{
    XDP_LWF_GENERIC *Generic = InterfaceContext;

    KeSetEvent(&Generic->InterfaceRemovedEvent, 0, FALSE);
}

NTSTATUS
XdpGenericAttachInterface(
    _Inout_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex,
    _Out_ XDP_ADD_INTERFACE *AddIf
    )
{
    NTSTATUS Status;
    CONST XDP_VERSION DriverApiVersion = {
        .Major = XDP_DRIVER_API_MAJOR_VER,
        .Minor = XDP_DRIVER_API_MINOR_VER,
        .Patch = XDP_DRIVER_API_PATCH_VER
    };

    //
    // This function supplies its caller with XDPIF interface addition info and
    // the caller will add the XDPIF interface.
    //

    ExInitializePushLock(&Generic->Lock);
    InitializeListHead(&Generic->Tx.Queues);
    KeInitializeEvent(&Generic->InterfaceRemovedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Generic->CleanupEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Generic->Tx.Datapath.ReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Generic->Rx.Datapath.ReadyEvent, NotificationEvent, FALSE);
    XdpInitializeReferenceCount(&Generic->ReferenceCount);
    Generic->Filter = Filter;
    Generic->NdisFilterHandle = NdisFilterHandle;
    Generic->IfIndex = IfIndex;
    Generic->InternalCapabilities.Mode = XDP_INTERFACE_MODE_GENERIC;
    Generic->InternalCapabilities.Hooks = GenericHooks;
    Generic->InternalCapabilities.HookCount = RTL_NUMBER_OF(GenericHooks);
    Generic->InternalCapabilities.CapabilitiesEx = &Generic->Capabilities.CapabilitiesEx;
    Generic->InternalCapabilities.CapabilitiesSize = sizeof(Generic->Capabilities);

    Generic->Rx.Datapath.DelayDetachTimer =
        XdpTimerCreate(
            XdpGenericDelayDereferenceDatapath,
            XdpGenericPackContext(Generic, &Generic->Rx.Datapath), XdpLwfDriverObject, NULL);
    if (Generic->Rx.Datapath.DelayDetachTimer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Generic->Tx.Datapath.DelayDetachTimer =
        XdpTimerCreate(
            XdpGenericDelayDereferenceDatapath,
            XdpGenericPackContext(Generic, &Generic->Tx.Datapath), XdpLwfDriverObject, NULL);
    if (Generic->Tx.Datapath.DelayDetachTimer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status =
        XdpInitializeCapabilities(
            &Generic->Capabilities, &DriverApiVersion);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpRegisterInterface(
            IfIndex, &Generic->Capabilities, Generic,
            &XdpGenericDispatch, &Generic->Registration);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RtlZeroMemory(AddIf, sizeof(*AddIf));
    AddIf->InterfaceCapabilities = &Generic->InternalCapabilities;
    AddIf->RemoveInterfaceComplete = XdpGenericRemoveInterfaceComplete;
    AddIf->InterfaceContext = Generic;
    AddIf->InterfaceHandle = &Generic->XdpIfInterfaceHandle;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpGenericCleanupInterface(Generic);
    }

    return Status;
}

VOID
XdpGenericDetachInterface(
    _In_ XDP_LWF_GENERIC *Generic
    )
{
    //
    // The caller of the attach routine added the XDPIF interface, but this
    // function removes the XDPIF interface.
    //

    if (Generic->XdpIfInterfaceHandle != NULL) {
        //
        // Initiate core XDP cleanup and wait for completion.
        //
        XdpIfRemoveInterfaces(&Generic->XdpIfInterfaceHandle, 1);
        KeWaitForSingleObject(
            &Generic->InterfaceRemovedEvent, Executive, KernelMode, FALSE, NULL);
        Generic->XdpIfInterfaceHandle = NULL;
    }

    XdpGenericCleanupInterface(Generic);
    XdpGenericDereference(Generic);
    KeWaitForSingleObject(&Generic->CleanupEvent, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
XdpGenericStart(
    VOID
    )
{
    XdpRegWatcherAddClient(XdpLwfRegWatcher, XdpGenericRegistryUpdate, &GenericRegWatcher);

    return STATUS_SUCCESS;
}

VOID
XdpGenericStop(
    VOID
    )
{
    XdpRegWatcherRemoveClient(XdpLwfRegWatcher, &GenericRegWatcher);
}
