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

#if DBG
FILTER_OID_REQUEST XdpVfLwfOidRequest;
FILTER_OID_REQUEST_COMPLETE XdpVfLwfOidRequestComplete;
#endif

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpLwfOidInternalRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_updates_bytes_to_(InformationBufferLength, *pBytesProcessed)
        VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _In_opt_ ULONG OutputBufferLength,
    _In_ ULONG MethodId,
    _Out_ ULONG *pBytesProcessed
    );
