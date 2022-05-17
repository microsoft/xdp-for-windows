//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _NDIS_POLL_BACKCHANNEL {
    PNDIS_POLL_QUEUE Q;
    NDIS_POLL_BACKCHANNEL_NOTIFY *Notify;
    VOID *NotifyContext;
} NDIS_POLL_BACKCHANNEL;

//
// NDIS control path callbacks.
//

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
BackchannelNotify(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    NDIS_POLL_BACKCHANNEL *Backchannel = Q->Ec;
    ASSERT(Backchannel->Notify != NULL);

    Backchannel->Notify(Backchannel->NotifyContext);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
BackchannelInsert(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    NDIS_POLL_BACKCHANNEL *Backchannel = Q->Ec;

    ASSERT(Backchannel->Q == Q);
    ASSERT(Q->Notify == NULL);

    if (Backchannel->Notify != NULL) {
        Q->Notify = BackchannelNotify;
    }
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
BackchannelUpdate(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    UNREFERENCED_PARAMETER(Q);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
BackchannelCleanup(
    _In_ PNDIS_POLL_QUEUE Q
    )
{
    UNREFERENCED_PARAMETER(Q);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollBackchannelCreate(
    _In_ NDIS_HANDLE PollHandle,
    _Out_ NDIS_POLL_BACKCHANNEL **PollBackchannel
    )
{
    NDIS_POLL_BACKCHANNEL *Backchannel = NULL;
    NTSTATUS Status;

    Backchannel = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Backchannel), POOLTAG_BACKCHANNEL);
    if (Backchannel == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = NdisPollFindAndReferenceQueueByHandle(PollHandle, &Backchannel->Q);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *PollBackchannel = Backchannel;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Backchannel != NULL) {
            ExFreePoolWithTag(Backchannel, POOLTAG_BACKCHANNEL);
        }
    }

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollBackchannelDelete(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    NdisPollDereferenceQueue(Backchannel->Q);
    ExFreePoolWithTag(Backchannel, POOLTAG_BACKCHANNEL);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollBackchannelAcquireExclusive(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_opt_ NDIS_POLL_BACKCHANNEL_NOTIFY *Notify,
    _In_opt_ VOID *NotifyContext
    )
{
    NTSTATUS Status;

    Backchannel->Notify = Notify;
    Backchannel->NotifyContext = NotifyContext;

    Status =
        NdisPollAcquireQueue(
            Backchannel->Q, Backchannel, BackchannelInsert, BackchannelUpdate, BackchannelCleanup);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        Backchannel->Notify = NULL;
        Backchannel->NotifyContext = NULL;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollBackchannelReleaseExclusive(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    NdisPollReleaseQueue(Backchannel->Q);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
NdisPollBackchannelInvokePoll(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ UINT32 RxQuota,
    _In_ UINT32 TxQuota
    )
{
    return NdisPollInvokePoll(Backchannel->Q, RxQuota, TxQuota);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NdisPollBackchannelSetNotifications(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ BOOLEAN EnableNotifications
    )
{
    if (EnableNotifications) {
        NdisPollEnableInterrupt(Backchannel->Q);
    } else {
        //
        // Interrupts are implicitly disabled by NICs requesting a poll.
        //
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NdisPollBackchannelAddBusyReference(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    return NdisPollAddBusyReference(Backchannel->Q);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NdisPollBackchannelReleaseBusyReference(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    NdisPollReleaseBusyReference(Backchannel->Q);
}

static CONST NDIS_POLL_BACKCHANNEL_DISPATCH BackchannelDispatch = {
    .CreateBackchannel      = NdisPollBackchannelCreate,
    .DeleteBackchannel      = NdisPollBackchannelDelete,
    .AcquireExclusive       = NdisPollBackchannelAcquireExclusive,
    .ReleaseExclusive       = NdisPollBackchannelReleaseExclusive,
    .InvokePoll             = NdisPollBackchannelInvokePoll,
    .SetNotifications       = NdisPollBackchannelSetNotifications,
    .AddBusyReference       = NdisPollBackchannelAddBusyReference,
    .ReleaseBusyReference   = NdisPollBackchannelReleaseBusyReference,
};

NTSTATUS
NdisPollGetBackchannel(
    _Inout_ IRP *Irp,
    _Out_ CONST NDIS_POLL_BACKCHANNEL_DISPATCH **Dispatch
    )
{
    ASSERT(Irp->RequestorMode == KernelMode);

    *Dispatch = &BackchannelDispatch;
    Irp->IoStatus.Information = sizeof(*Dispatch);

    return STATUS_SUCCESS;
}
