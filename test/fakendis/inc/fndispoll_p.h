//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

// Internal interface to extend NDIS polling API.

#include <fndispoll.h>

#define NDIS_DATAPATH_POLL_ANY_PROCESSOR MAXULONG

typedef struct _NDIS_POLL_QUEUE *PNDIS_POLL_QUEUE;

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NDIS_POLL_NOTIFY(
    _In_ PNDIS_POLL_QUEUE Q
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_INSERT(
    _In_ PNDIS_POLL_QUEUE Q
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_UPDATE(
    _In_ PNDIS_POLL_QUEUE Q
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_CLEANUP(
    _In_ PNDIS_POLL_QUEUE Q
    );

typedef struct _NDIS_POLL_QUEUE {
    NDIS_HANDLE MiniportAdapterHandle;
    PVOID MiniportPollContext;

    NDIS_POLL *Poll;
    NDIS_SET_POLL_NOTIFICATION *Interrupt;
    ULONG IdealProcessor;
    KDPC Dpc;
    LONG ReferenceCount;
    ULONG BusyReferences;
    EX_PUSH_LOCK Lock;
    BOOLEAN Deleted;

    LIST_ENTRY QueueLink;

    // Store the NDIS default EC inline.
    ULONG64 Reserved[16];

    // Dynamic EC contexts.
    PVOID Ec;
    NDIS_POLL_NOTIFY *Notify;
    NDIS_POLL_UPDATE *Update;
    NDIS_POLL_CLEANUP *Cleanup;
} NDIS_POLL_QUEUE, *PNDIS_POLL_QUEUE;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollFindAndReferenceQueueByHandle(
    _In_ NDIS_HANDLE PollHandle,
    _Out_ PNDIS_POLL_QUEUE *FoundQueue
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollDereferenceQueue(
    _In_ PNDIS_POLL_QUEUE Q
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollAcquireQueue(
    _In_ PNDIS_POLL_QUEUE Q,
    _In_ PVOID Ec,
    _In_ NDIS_POLL_INSERT *Insert,
    _In_ NDIS_POLL_UPDATE *Update,
    _In_ NDIS_POLL_CLEANUP *Cleanup
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollReleaseQueue(
    _In_ PNDIS_POLL_QUEUE Q
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollAddBusyReference(
    _In_ PNDIS_POLL_QUEUE Q
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollReleaseBusyReference(
    _In_ PNDIS_POLL_QUEUE Q
    );
