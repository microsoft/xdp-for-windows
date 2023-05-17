//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "offload.h"

NTSTATUS
XdpLwfOffloadRssGetCapabilities(
    _In_ XDP_LWF_FILTER *Filter,
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesLength
    );

NTSTATUS
XdpLwfOffloadRssGet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _Out_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _Inout_ UINT32 *RssParamsLength
    );

NTSTATUS
XdpLwfOffloadRssSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ XDP_OFFLOAD_PARAMS_RSS *RssParams,
    _In_ UINT32 RssParamsLength
    );

VOID
XdpLwfOffloadRssInitialize(
    _In_ XDP_LWF_FILTER *Filter
    );

_Offload_work_routine_
_Requires_offload_rundown_ref_
VOID
XdpLwfOffloadRssDeactivate(
    _In_ XDP_LWF_FILTER *Filter
    );

VOID
XdpLwfOffloadRssCloseInterfaceOffloadHandle(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext
    );

NTSTATUS
XdpLwfOffloadRssInspectOidRequest(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _Out_ XDP_OID_ACTION *Action,
    _Out_ NDIS_STATUS *CompletionStatus
    );

VOID
XdpLwfOffloadRssNblTransform(
    _In_ XDP_LWF_FILTER *Filter,
    _Inout_ NET_BUFFER_LIST *Nbl
    );
