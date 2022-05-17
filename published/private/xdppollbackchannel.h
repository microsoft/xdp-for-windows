//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define XDP_POLL_BACKCHANNEL

#include <ndis/types.h>

typedef struct _NDIS_POLL_BACKCHANNEL NDIS_POLL_BACKCHANNEL;

//
// Callback invoked by the backchannel when a poll is requested.
//
typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NDIS_POLL_BACKCHANNEL_NOTIFY(
    _In_ VOID *NotifyContext
    );

//
// Create an NDIS backchannel for the given NDIS polling handle.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NDIS_POLL_BACKCHANNEL_CREATE(
    _In_ NDIS_HANDLE PollHandle,
    _Out_ NDIS_POLL_BACKCHANNEL **Backchannel
    );

//
// Delete an NDIS backchannel.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_BACKCHANNEL_DELETE(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    );

//
// Acquire exclusive ownership of an NDIS poll context. The owner is responsible
// for serializing the poll routine and handling poll requests.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NDIS_POLL_BACKCHANNEL_ACQUIRE_EXCLUSIVE(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_opt_ NDIS_POLL_BACKCHANNEL_NOTIFY *Notify,
    _In_opt_ VOID *NotifyContext
    );

//
// Return exclusive ownership of an NDIS poll context.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_BACKCHANNEL_RELEASE_EXCLUSIVE(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    );

//
// Invoke the NDIS poll routine.
//
typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
NDIS_POLL_BACKCHANNEL_INVOKE_POLL(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ UINT32 RxQuota,
    _In_ UINT32 TxQuota
    );

//
// Set the notification state of a polling context.
//
typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NDIS_POLL_BACKCHANNEL_SET_NOTIFICATIONS(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ BOOLEAN EnableNotifications
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NDIS_POLL_BACKCHANNEL_ADD_BUSY_REFERENCE(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NDIS_POLL_BACKCHANNEL_RELEASE_BUSY_REFERENCE(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    );

typedef struct _NDIS_POLL_BACKCHANNEL_DISPATCH {
    NDIS_POLL_BACKCHANNEL_CREATE *CreateBackchannel;
    NDIS_POLL_BACKCHANNEL_DELETE *DeleteBackchannel;
    NDIS_POLL_BACKCHANNEL_ACQUIRE_EXCLUSIVE *AcquireExclusive;
    NDIS_POLL_BACKCHANNEL_RELEASE_EXCLUSIVE *ReleaseExclusive;
    NDIS_POLL_BACKCHANNEL_INVOKE_POLL *InvokePoll;
    NDIS_POLL_BACKCHANNEL_SET_NOTIFICATIONS *SetNotifications;
    NDIS_POLL_BACKCHANNEL_ADD_BUSY_REFERENCE *AddBusyReference;
    NDIS_POLL_BACKCHANNEL_RELEASE_BUSY_REFERENCE *ReleaseBusyReference;
} NDIS_POLL_BACKCHANNEL_DISPATCH;
