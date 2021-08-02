//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdprefcount.h>

#include "rss.h"
#include "send.h"

#define GENERIC_DATAPATH_RESTART_TIMEOUT_MS 1000

typedef struct _XDP_LWF_DATAPATH_BYPASS {
    BOOLEAN Inserted;           // LWF handlers are inserted on the NDIS data path.
    ULONG ReferenceCount;       // Number of data path clients.
    KEVENT ReadyEvent;          // Set when the inserted data path is restarted.
    UINT64 LastDereferenceTimestamp;
} XDP_LWF_DATAPATH_BYPASS;

typedef struct _XDP_LWF_GENERIC {
    NDIS_HANDLE NdisHandle;
    NET_IFINDEX IfIndex;

    XDP_CAPABILITIES Capabilities;
    XDP_CAPABILITIES_INTERNAL InternalCapabilities;

    EX_PUSH_LOCK Lock;
    XDP_REGISTRATION_HANDLE Registration;
    XDP_IF_BINDING_HANDLE BindingHandle;
    KEVENT BindingDeletedEvent;
    XDP_REFERENCE_COUNT ReferenceCount;
    KEVENT CleanupEvent;

    XDP_LWF_GENERIC_RSS Rss;

    struct {
        XDP_LWF_DATAPATH_BYPASS Datapath;
    } Rx;

    struct {
        XDP_LWF_DATAPATH_BYPASS Datapath;
        LIST_ENTRY Queues;
        UINT32 Mtu;
    } Tx;
} XDP_LWF_GENERIC;

XDP_LWF_GENERIC *
XdpGenericFromFilterContext(
    _In_ NDIS_HANDLE FilterModuleContext
    );

VOID
XdpGenericCreateBinding(
    _Inout_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex
    );

VOID
XdpGenericDeleteBinding(
    _In_ XDP_LWF_GENERIC *Generic
    );

VOID
XdpGenericPause(
    _In_ XDP_LWF_GENERIC *Generic
    );

VOID
XdpGenericRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_FILTER_RESTART_PARAMETERS *RestartParameters
    );

NDIS_STATUS
XdpGenericFilterSetOptions(
    _In_ XDP_LWF_GENERIC *Generic
    );

NDIS_STATUS
XdpGenericInspectOidRequest(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NDIS_OID_REQUEST *Request
    );

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpGenericRequestRestart(
    _In_ XDP_LWF_GENERIC *Generic
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_lock_held_(&Generic->Lock)
VOID
XdpGenericReferenceDatapath(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath,
    _Out_ BOOLEAN *NeedRestart
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_lock_held_(&Generic->Lock)
VOID
XdpGenericDereferenceDatapath(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ XDP_LWF_DATAPATH_BYPASS *Datapath,
    _Out_ BOOLEAN *NeedRestart
    );

NTSTATUS
XdpGenericStart(
    VOID
    );

VOID
XdpGenericStop(
    VOID
    );
