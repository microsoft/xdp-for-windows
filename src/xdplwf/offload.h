//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "oid.h"

typedef struct _XDP_LWF_OFFLOAD_SETTING_RSS {
    XDP_OFFLOAD_PARAMS_RSS Params;
    XDP_REFERENCE_COUNT ReferenceCount;
    XDP_LIFETIME_ENTRY DeleteEntry;
} XDP_LWF_OFFLOAD_SETTING_RSS;

typedef struct _XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD {
    struct {
        BOOLEAN Enabled : 1;
    } Checksum;
    struct {
        UINT32 MaxOffloadSize;
        UINT32 MinSegments;
    } Lso;
    XDP_LIFETIME_ENTRY DeleteEntry;
} XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD;

//
// Describes a set of interface offload configurations.
//
typedef struct _XDP_LWF_INTERFACE_OFFLOAD_SETTINGS {
    XDP_LWF_OFFLOAD_SETTING_RSS *Rss;
    XDP_LWF_OFFLOAD_SETTING_TASK_OFFLOAD *TaskOffload;
} XDP_LWF_INTERFACE_OFFLOAD_SETTINGS;

//
// Per LWF filter state.
//
typedef struct _XDP_LWF_OFFLOAD {
    //
    // A serialized work queue for handling requests that interact with the
    // regular, NDIS-serialized OID control path. This allows offloads to be
    // serialized with respect to both the OID control path and arbitrary user
    // mode requests.
    //
    XDP_WORK_QUEUE *WorkQueue;

    //
    // Deactivating an interface reverts all XDP-initiated changes to an
    // interface and prevents new changes.
    //
    BOOLEAN Deactivated;

    //
    // Hardware capabilities.
    //
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps;

    //
    // Current settings.
    //
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS UpperEdge;
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS LowerEdge;
} XDP_LWF_OFFLOAD;

typedef enum {
    //
    // Control path below the XDP LWF.
    //
    XdpOffloadEdgeLower,
    //
    // Control path above the XDP LWF.
    //
    XdpOffloadEdgeUpper,
} XDP_LWF_OFFLOAD_EDGE;

//
// Context backing the interface offload handle.
//
typedef struct _XDP_LWF_INTERFACE_OFFLOAD_CONTEXT {
    XDP_LWF_FILTER *Filter;
    XDP_LWF_OFFLOAD_EDGE Edge;
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS Settings;
} XDP_LWF_INTERFACE_OFFLOAD_CONTEXT;

typedef struct _XDP_LWF_OFFLOAD_WORKITEM XDP_LWF_OFFLOAD_WORKITEM;

//
// Annotate routines that must be invoked from the serialized offload work
// queue.
//
#define _Offload_work_routine_
#define _Requires_offload_rundown_ref_

typedef
_Offload_work_routine_
VOID
XDP_LWF_OFFLOAD_WORK_ROUTINE(
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem
    );

typedef struct _XDP_LWF_OFFLOAD_WORKITEM {
    SINGLE_LIST_ENTRY Link;
    XDP_LWF_FILTER *Filter;
    XDP_LWF_OFFLOAD_WORK_ROUTINE *WorkRoutine;
} XDP_LWF_OFFLOAD_WORKITEM;

VOID
XdpLwfOffloadQueueWorkItem(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_OFFLOAD_WORKITEM *WorkItem,
    _In_ XDP_LWF_OFFLOAD_WORK_ROUTINE *WorkRoutine
    );

NDIS_STATUS
XdpLwfOffloadInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_INSPECT_COMPLETE *InspectComplete
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpOffloadFilterStatus(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ const NDIS_STATUS_INDICATION *StatusIndication
    );

VOID
XdpLwfOffloadTransformNbls(
    _In_ XDP_LWF_FILTER *Filter,
    _Inout_ NBL_COUNTED_QUEUE *NblList,
    _In_ UINT32 XdpInspectFlags
    );

VOID
XdpLwfOffloadInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    );

NTSTATUS
XdpLwfOffloadStart(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

extern const XDP_OFFLOAD_DISPATCH XdpLwfOffloadDispatch;
