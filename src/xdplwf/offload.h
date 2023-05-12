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

//
// Describes a set of interface offload configurations.
//
typedef struct _XDP_LWF_INTERFACE_OFFLOAD_SETTINGS {
    XDP_LWF_OFFLOAD_SETTING_RSS *Rss;
    // ...
} XDP_LWF_INTERFACE_OFFLOAD_SETTINGS;

//
// Per LWF filter state.
//
typedef struct _XDP_LWF_OFFLOAD {
    EX_PUSH_LOCK Lock;

    //
    // Indicates offload handles have been swept and no new handles may be
    // opened.
    //
    BOOLEAN Deactivated;

    //
    // This rundown prevents the filter from detaching from the NDIS stack. A
    // rundown reference must be held while issuing OID requests. Once the
    // rundown is complete, no new requests should be made to the filter.
    //
    EX_RUNDOWN_REF FilterRundown;

    //
    // A serialized work queue for handling requests that interact with the
    // regular, NDIS-serialized OID control path. This allows offloads to be
    // serialized with respect to both the OID control path and arbitrary user
    // mode requests.
    //
    XDP_WORK_QUEUE *WorkQueue;

    //
    // Hardware capabilities.
    //
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps;

    //
    // Current settings.
    //
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS UpperEdge;
    XDP_LWF_INTERFACE_OFFLOAD_SETTINGS LowerEdge;

    //
    // Offload handles.
    //
    LIST_ENTRY InterfaceOffloadHandleListHead;
} XDP_LWF_OFFLOAD;

inline
BOOLEAN
XdpLwfOffloadIsNdisRssEnabled(
    _In_ CONST NDIS_RECEIVE_SCALE_PARAMETERS *NdisRssParams
    )
{
    return
        NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(NdisRssParams->HashInformation) != 0 &&
        (NdisRssParams->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) == 0;
}

NDIS_STATUS
XdpLwfOffloadInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_INSPECT_COMPLETE *InspectComplete
    );

VOID
XdpLwfOffloadTransformNbls(
    _In_ XDP_LWF_FILTER *Filter,
    _Inout_ NBL_COUNTED_QUEUE *NblList,
    _In_ UINT32 XdpInspectFlags
    );

VOID
XdpLwfOffloadDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadRssInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

NTSTATUS
XdpLwfOffloadStart(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadUnInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

extern CONST XDP_OFFLOAD_DISPATCH XdpLwfOffloadDispatch;
