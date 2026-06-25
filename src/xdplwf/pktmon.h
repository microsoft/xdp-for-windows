//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <pktmonclntk.h>

//
// Must be in range [0x80000000 - 0xFFFFFFFF] per PktMon guidance.
//
typedef enum _XDP_PKTMON_DROP_REASON {
    DropProgramInspection = 0x80000000,
    DropForwardingRscInvalidHeaders,
    DropForwardingAllocation,
    DropForwardingAllocationLimit,
} XDP_PKTMON_DROP_REASON;

//
// Must be in range [0 - 0x7FFFFFFF] per PktMon guidance.
//
typedef enum _XDP_PKTMON_DROP_LOCATION {
    PktMonDropLoc1 = 1,
    PktMonDropLoc2,
    PktMonDropLoc3,
    PktMonDropLoc4,
    PktMonDropLoc5,
    PktMonDropLoc6,
    PktMonDropLoc7,
    PktMonDropLoc8,
    PktMonDropLoc9,
    PktMonDropLoc10,
    PktMonDropLoc11,
    PktMonDropLoc12,
    PktMonDropLoc13,
    PktMonDropLoc14,
    PktMonDropLoc15,
} XDP_PKTMON_DROP_LOCATION;

typedef struct _XDP_INTERFACE_PKTMON_CONTEXT {
    PKTMON_COMPONENT_CONTEXT PktMonComp;
} XDP_INTERFACE_PKTMON_CONTEXT;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpPktMonLogDrop(
    _In_ XDP_INTERFACE_PKTMON_CONTEXT *PktMonContext,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ XDP_PKTMON_DROP_REASON DropReason,
    _In_ XDP_PKTMON_DROP_LOCATION DropLocation
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonRegisterInterface(
    _Outptr_result_maybenull_ XDP_INTERFACE_PKTMON_CONTEXT **PktMonContext,
    _In_ NET_IFINDEX IfIndex
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonUnregisterInterface(
    _Inout_ XDP_INTERFACE_PKTMON_CONTEXT **PktMonContext
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpPktMonStart(
    VOID
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPktMonStop(
    VOID
    );
