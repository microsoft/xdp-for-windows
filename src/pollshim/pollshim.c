//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static CONST NDIS_POLL_BACKCHANNEL_DISPATCH *XdpPollDispatch;
static FNDIS_NPI_CLIENT FndisClient;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpPollCreateBackchannel(
    _In_ NDIS_HANDLE InterfacePollHandle,
    _Out_ NDIS_POLL_BACKCHANNEL **Backchannel
    )
{
    if (XdpPollDispatch == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    return XdpPollDispatch->CreateBackchannel(InterfacePollHandle, Backchannel);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPollDeleteBackchannel(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    XdpPollDispatch->DeleteBackchannel(Backchannel);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpPollAcquireExclusive(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_opt_ NDIS_POLL_BACKCHANNEL_NOTIFY *Notify,
    _In_opt_ VOID *NotifyContext
    )
{
    return XdpPollDispatch->AcquireExclusive(Backchannel, Notify, NotifyContext);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPollReleaseExclusive(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    XdpPollDispatch->ReleaseExclusive(Backchannel);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
XdpPollInvoke(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ UINT32 RxQuota,
    _In_ UINT32 TxQuota
    )
{
    return XdpPollDispatch->InvokePoll(Backchannel, RxQuota, TxQuota);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpPollSetNotifications(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel,
    _In_ BOOLEAN EnableNotifications
    )
{
    XdpPollDispatch->SetNotifications(Backchannel, EnableNotifications);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpPollAddBusyReference(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    return XdpPollDispatch->AddBusyReference(Backchannel);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpPollReleaseBusyReference(
    _In_ NDIS_POLL_BACKCHANNEL *Backchannel
    )
{
    XdpPollDispatch->ReleaseBusyReference(Backchannel);
}

NTSTATUS
XdpPollStart(
    VOID
    )
{
#ifndef XDP_OFFICIAL_BUILD

    NTSTATUS Status;

    XdpPollDispatch = NULL;

    //
    // Since an NDIS polling backchannel is not yet implemented, shim the fake
    // NDIS polling backchannel, which in turn exports the backchannel dispatch
    // table via an IOCTL.
    //

    Status = FNdisClientOpen(&FndisClient);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = FNdisClientGetPollBackchannel(&FndisClient, (VOID *)&XdpPollDispatch);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

#endif

    return STATUS_SUCCESS;
}

VOID
XdpPollStop(
    VOID
    )
{
    FNdisClientClose(&FndisClient);
}
