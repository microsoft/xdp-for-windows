//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "pktmon.tmh"

static BOOLEAN XdpDisablePktMon = FALSE;

static const NPI_MODULEID NPI_XDP_GENERIC_PKTMON_CLNT_MODULEID = {
    sizeof(NPI_MODULEID),
    MIT_GUID,
    {
        0x7db86266, 0xb242, 0x416f, {0xb0, 0x4c, 0xb1, 0x0d, 0x4a, 0x50, 0xa8, 0x62}
    }
};

//
// List of all generic bindings. Used for dynamic pktmon registration. Protected by the pktmon
// generic list lock.
//
static LIST_ENTRY XdpPktMonGenericList;
static EX_PUSH_LOCK XdpPktMonGenericListLock;

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonRegisterInterface(
    _Outptr_result_maybenull_ XDP_INTERFACE_PKTMON_CONTEXT **PktMonContext,
    _In_ NET_IFINDEX IfIndex
    )
{

    NTSTATUS Status;
    XDP_INTERFACE_PKTMON_CONTEXT *Context = NULL;
    PKTMON_COMPONENT_PROPERTY Property = {0};

    TraceEnter(TRACE_LWF, "IfIndex=%d", IfIndex);

    *PktMonContext = NULL;

    DECLARE_CONST_UNICODE_STRING(DriverName, L"xdp.sys");
    DECLARE_CONST_UNICODE_STRING(Description, L"XDP GENERIC network inspection");

    Context = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Context), POOLTAG_PKTMON);
    if (Context == NULL) {
        TraceError(TRACE_LWF, "IfIndex=%u PktMon allocation failed", IfIndex);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status =
        PktMonClntComponentRegister(
            &Context->PktMonComp,
            &DriverName,
            &Description,
            PktMonComp_Filter,
            PktMonPayload_Ethernet);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF, "IfIndex=%u PktMonClntComponentRegister failed Status=%!STATUS!",
            IfIndex, Status);
        goto Exit;
    }

    Property.Id = PktMonCompProp_MiniportIfIndex;
    Property.MiniportIfIndex = IfIndex;
    Status =
        PktMonClntSetComponentProperty(
            &Context->PktMonComp,
            &Property);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF, "IfIndex=%u PktMonClntSetComponentProperty failed Status=%!STATUS!",
            IfIndex, Status);
        goto Exit;
    }

    *PktMonContext = Context;
    TraceInfo(TRACE_LWF, "PktMon context created IfIndex=%u, Context=%p", IfIndex, Context);

Exit:
    if (!NT_SUCCESS(Status)) {
        if (Context != NULL) {
            PktMonClntComponentUnregister(&Context->PktMonComp);
            ExFreePoolWithTag(Context, POOLTAG_PKTMON);
        }
    }

    TraceExitStatus(TRACE_LWF);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonUnregisterInterface(
    _Inout_ XDP_INTERFACE_PKTMON_CONTEXT **PktMonContext
    )
{
    XDP_INTERFACE_PKTMON_CONTEXT *Context = *PktMonContext;

    TraceEnter(TRACE_LWF, "Context=%p", Context);

    if (Context != NULL) {
        PktMonClntComponentUnregister(&Context->PktMonComp);
        ExFreePoolWithTag(Context, POOLTAG_PKTMON);
    }

    *PktMonContext = NULL;

    TraceExitSuccess(TRACE_LWF);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonRegistrationCallback(
    VOID
    )
{
    //
    // PktMon was loaded.
    //
    // Enumerate generic bindings and register them with pktmon.
    //

    TraceEnter(TRACE_LWF, "PktMonRegistrationCallback");

    RtlAcquirePushLockExclusive(&XdpPktMonGenericListLock);

    LIST_ENTRY *Entry = XdpPktMonGenericList.Flink;
    while (Entry != &XdpPktMonGenericList) {
        XDP_LWF_GENERIC *Generic = CONTAINING_RECORD(Entry, XDP_LWF_GENERIC, PktMonLink);
        Entry = Entry->Flink;

        ASSERT(Generic->PktMonContext == NULL);

        XdpPktMonRegisterInterface(&Generic->PktMonContext, Generic->IfIndex);
        if (Generic->PktMonContext != NULL) {
            ExReInitializeRundownProtection(&Generic->PktMonRundownRef);
            TraceInfo(
                TRACE_LWF, "PktMon dynamic registration IfIndex=%u Context=%p",
                Generic->IfIndex, Generic->PktMonContext);
        }
    }

    RtlReleasePushLockExclusive(&XdpPktMonGenericListLock);

    TraceExitSuccess(TRACE_LWF);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonClientCleanupCallback(
    VOID
    )
{
    //
    // PktMon is unloading.
    //
    // Enumerate existing generic bindings and unregister them with pktmon.
    //
    // N.B. By this time, the PktMonClnt library shim has disallowed new calls across the PktMon
    //      NPI, so the unregistration attempts will effectively be no-ops. This is fine because
    //      neither the PktMon subsystem nor the PktMonClnt library shim require explicit
    //      unregistration of edges or components. However, attempting unregistration at this time
    //      frees context memory and provides symmetry and simplicity for this XDP pktmon module.

    TraceEnter(TRACE_LWF, "PktMonClientCleanupCallback");

    RtlAcquirePushLockExclusive(&XdpPktMonGenericListLock);

    LIST_ENTRY *Entry = XdpPktMonGenericList.Flink;
    while (Entry != &XdpPktMonGenericList) {
        XDP_LWF_GENERIC *Generic =
            CONTAINING_RECORD(Entry, XDP_LWF_GENERIC, PktMonLink);
        Entry = Entry->Flink;

        //
        // Wait for any in-progress datapath references to drain, then free
        // the pktmon context. The component is already unregistered by
        // PktMonDetachProvider (which zeroed the component context).
        //
        ExWaitForRundownProtectionRelease(&Generic->PktMonRundownRef);
        XdpPktMonUnregisterInterface(&Generic->PktMonContext);

        TraceVerbose(
            TRACE_LWF, "PktMon cleanup IfIndex=%u", Generic->IfIndex);
    }

    RtlReleasePushLockExclusive(&XdpPktMonGenericListLock);

    TraceExitSuccess(TRACE_LWF);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonClientCompNotifyCallback(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext
    )
{
    UNREFERENCED_PARAMETER(CompContext);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpPktMonLogDrop(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ XDP_PKTMON_DROP_REASON DropReason,
    _In_ XDP_PKTMON_DROP_LOCATION DropLocation
    )
{
    if (XdpDisablePktMon) {
        goto Exit;
    }

    if (!ExAcquireRundownProtection(&Generic->PktMonRundownRef)) {
        goto Exit;
    }

    if (Generic->PktMonContext->PktMonComp.DropEnabled) {
        PktMonClntNblDrop(
            &Generic->PktMonContext->PktMonComp,
            NetBufferLists,
            PktMonPayload_Ethernet,
            NULL, // PacketHeaderInformation
            UseOnlyFirstNbl,
            Direction,
            DropReason,
            DropLocation);
    }

    ExReleaseRundownProtection(&Generic->PktMonRundownRef);

Exit:
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonTrackGeneric(
    _Inout_ XDP_LWF_GENERIC *Generic
    )
{
    TraceEnter(TRACE_LWF, "IfIndex=%u", Generic->IfIndex);

    if (XdpDisablePktMon) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&XdpPktMonGenericListLock);

    InsertTailList(&XdpPktMonGenericList, &Generic->PktMonLink);
    XdpPktMonRegisterInterface(&Generic->PktMonContext, Generic->IfIndex);

    if (Generic->PktMonContext != NULL) {
        ExReInitializeRundownProtection(&Generic->PktMonRundownRef);
    }

    RtlReleasePushLockExclusive(&XdpPktMonGenericListLock);

Exit:

    TraceExitSuccess(TRACE_LWF);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonUntrackGeneric(
    _Inout_ XDP_LWF_GENERIC *Generic
    )
{
    TraceEnter(TRACE_LWF, "IfIndex=%u", Generic->IfIndex);

    if (XdpDisablePktMon) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&XdpPktMonGenericListLock);

    RemoveEntryList(&Generic->PktMonLink);
    InitializeListHead(&Generic->PktMonLink);

    RtlReleasePushLockExclusive(&XdpPktMonGenericListLock);

    //
    // Wait for any in-progress datapath references to drain, then free the
    // pktmon context.
    //
    ExWaitForRundownProtectionRelease(&Generic->PktMonRundownRef);
    XdpPktMonUnregisterInterface(&Generic->PktMonContext);

Exit:

    TraceExitSuccess(TRACE_LWF);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpPktMonStart(
    VOID
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_LWF, "Start");

    InitializeListHead(&XdpPktMonGenericList);
    ExInitializePushLock(&XdpPktMonGenericListLock);

    Status = XdpRegQueryBoolean(XDP_LWF_PARAMETERS_KEY, L"XdpDisablePktMon", &XdpDisablePktMon);
    if (!NT_SUCCESS(Status)) {
        //
        // Non-fatal error.
        //
        TraceError(
            TRACE_LWF, "XdpRegQueryBoolean(XdpDisablePktMon) failed, Status=%!STATUS!", Status);
        Status = STATUS_SUCCESS;
    }

    if (XdpDisablePktMon) {
        ASSERT(NT_SUCCESS(Status));
        goto Exit;
    }

    Status =
        PktMonClntInitialize(
            &NPI_XDP_GENERIC_PKTMON_CLNT_MODULEID,
            XdpPktMonRegistrationCallback,
            XdpPktMonClientCleanupCallback,
            XdpPktMonClientCompNotifyCallback);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_LWF, "PktMonClntInitialize failed, Status=%!STATUS!", Status);
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_LWF);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonStop(
    VOID
    )
{
    TraceEnter(TRACE_LWF, "Stop");

    if (XdpDisablePktMon) {
        goto Exit;
    }

    PktMonClntUninitialize();

Exit:

    TraceExitSuccess(TRACE_LWF);
}