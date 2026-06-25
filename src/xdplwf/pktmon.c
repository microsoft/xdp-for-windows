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

static
VOID
XdpPktMonRegistrationCallback(
    VOID
    )
{
    //
    // We don't yet support pktmon tracing for existing XDP interfaces in dynamic
    // pktmon load scenarios (pktmon loads after XDP interface is established).
    //
}

static
VOID
XdpPktMonClientCleanupCallback(
    VOID
    )
{
}

static
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
    _In_ XDP_INTERFACE_PKTMON_CONTEXT *PktMonContext,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ XDP_PKTMON_DROP_REASON DropReason,
    _In_ XDP_PKTMON_DROP_LOCATION DropLocation
    )
{
    //
    // If PktMonContext is non-NULL, PktMon must be enabled.
    //
    ASSERT(!XdpDisablePktMon || (PktMonContext == NULL));

    //
    // Drop reason must be in range [0x80000000 - 0xFFFFFFFF] per PktMon guidance.
    // Drop location must be in range [0 - 0x7FFFFFFF] per PktMon guidance.
    //

    if ((PktMonContext != NULL) && PktMonContext->PktMonComp.DropEnabled) {
        PktMonClntNblDrop(
            &PktMonContext->PktMonComp,
            NetBufferLists,
            PktMonPayload_Ethernet,
            NULL, // PacketHeaderInformation
            UseOnlyFirstNbl,
            Direction,
            DropReason,
            DropLocation);
    }
}

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

    if (XdpDisablePktMon) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    DECLARE_CONST_UNICODE_STRING(DriverName, L"xdp.sys");
    DECLARE_CONST_UNICODE_STRING(Description, L"XDP GENERIC network inspection");

    Context = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Context), POOLTAG_PKTMON);
    if (Context == NULL) {
        TraceError(
            TRACE_LWF, "IfIndex=%u PktMon allocation failed",
            IfIndex);
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

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonUnregisterInterface(
    _Inout_ XDP_INTERFACE_PKTMON_CONTEXT **PktMonContext
    )
{
    XDP_INTERFACE_PKTMON_CONTEXT *Context = *PktMonContext;

    TraceEnter(TRACE_LWF, "Context=%p", Context);

    if (XdpDisablePktMon) {
        goto Exit;
    }

    if (Context != NULL) {
        PktMonClntComponentUnregister(&Context->PktMonComp);
        ExFreePoolWithTag(Context, POOLTAG_PKTMON);
    }

    *PktMonContext = NULL;

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

    return;
}