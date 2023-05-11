//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef enum _XDP_OID_ACTION {
    XdpOidActionPass,
    XdpOidActionComplete,
} XDP_OID_ACTION;

FILTER_OID_REQUEST XdpLwfOidRequest;
FILTER_OID_REQUEST_COMPLETE XdpLwfOidRequestComplete;
FILTER_DIRECT_OID_REQUEST XdpLwfDirectOidRequest;
FILTER_DIRECT_OID_REQUEST_COMPLETE XdpLwfDirectOidRequestComplete;

#if DBG
FILTER_OID_REQUEST XdpVfLwfOidRequest;
FILTER_OID_REQUEST_COMPLETE XdpVfLwfOidRequestComplete;
FILTER_DIRECT_OID_REQUEST XdpVfLwfDirectOidRequest;
FILTER_DIRECT_OID_REQUEST_COMPLETE XdpVfLwfDirectOidRequestComplete;
#endif

typedef enum _XDP_OID_REQUEST_INTERFACE {
    XDP_OID_REQUEST_INTERFACE_REGULAR,
    XDP_OID_REQUEST_INTERFACE_DIRECT,
} XDP_OID_REQUEST_INTERFACE;

typedef
VOID
XDP_OID_INSPECT_COMPLETE(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ XDP_OID_ACTION Action,
    _In_ NDIS_STATUS Status
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpLwfOidInternalRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ XDP_OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_updates_bytes_to_(InformationBufferLength, *pBytesProcessed)
        VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _In_opt_ ULONG OutputBufferLength,
    _In_ ULONG MethodId,
    _Out_ ULONG *pBytesProcessed
    );
