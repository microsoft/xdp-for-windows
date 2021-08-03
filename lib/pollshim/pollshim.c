//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

static CONST NDIS_POLL_BACKCHANNEL_DISPATCH *XdpPollDispatch;
static HANDLE BackchannelHandle;

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
#if FNDIS

    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Oa;
    IO_STATUS_BLOCK Iosb;

    BackchannelHandle = NULL;
    XdpPollDispatch = NULL;

    //
    // Since an NDIS polling backchannel is not yet implemented, shim the fake
    // NDIS polling backchannel, which in turn exports the backchannel dispatch
    // table via an IOCTL.
    //

    RtlInitUnicodeString(&Name, FNDIS_DEVICE_NAME);
    InitializeObjectAttributes(&Oa, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status =
        ZwCreateFile(
            &BackchannelHandle, GENERIC_READ, &Oa, &Iosb, NULL, 0L, 0, FILE_OPEN_IF, 0, NULL, 0);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        ZwDeviceIoControlFile(
            BackchannelHandle, NULL, NULL, NULL, &Iosb, IOCTL_FNDIS_POLL_GET_BACKCHANNEL, NULL, 0,
            (VOID *)&XdpPollDispatch, sizeof(XdpPollDispatch));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

#endif // FNDIS

    return STATUS_SUCCESS;
}

VOID
XdpPollStop(
    VOID
    )
{
    if (BackchannelHandle != NULL) {
        ZwClose(BackchannelHandle);
        BackchannelHandle = NULL;
    }
}