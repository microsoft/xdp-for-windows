//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _ADAPTER_CONTEXT ADAPTER_CONTEXT;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
NDIS_STATUS
REGISTER_POLL(
    _In_ NDIS_HANDLE NdisHandle,
    _In_opt_ VOID *Context,
    _In_ CONST NDIS_POLL_CHARACTERISTICS *Characteristics,
    _Out_ NDIS_POLL_HANDLE *PollHandle
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
DEREGISTER_POLL(
    _In_ NDIS_POLL_HANDLE PollHandle
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
SET_POLL_AFFINITY(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _In_ CONST PROCESSOR_NUMBER *PollAffinity
    );

typedef
_IRQL_requires_max_(HIGH_LEVEL)
VOID
REQUEST_POLL(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _Reserved_ VOID *Reserved
    );

typedef struct _ADAPTER_POLL_DISPATCH {
    REGISTER_POLL *RegisterPoll;
    DEREGISTER_POLL *DeregisterPoll;
    SET_POLL_AFFINITY *SetPollAffinity;
    REQUEST_POLL *RequestPoll;
} ADAPTER_POLL_DISPATCH;

typedef enum _ADAPTER_POLL_PROVIDER {
    PollProviderNdis,
    PollProviderFndis,
    PollProviderMax
} ADAPTER_POLL_PROVIDER;

NDIS_STATUS
MpInitializePoll(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

VOID
MpCleanupPoll(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );
