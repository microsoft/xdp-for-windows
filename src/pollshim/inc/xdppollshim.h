//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdppollbackchannel.h>

NDIS_POLL_BACKCHANNEL_CREATE XdpPollCreateBackchannel;
NDIS_POLL_BACKCHANNEL_DELETE XdpPollDeleteBackchannel;
NDIS_POLL_BACKCHANNEL_ACQUIRE_EXCLUSIVE XdpPollAcquireExclusive;
NDIS_POLL_BACKCHANNEL_RELEASE_EXCLUSIVE XdpPollReleaseExclusive;
NDIS_POLL_BACKCHANNEL_INVOKE_POLL XdpPollInvoke;
NDIS_POLL_BACKCHANNEL_SET_NOTIFICATIONS XdpPollSetNotifications;
NDIS_POLL_BACKCHANNEL_ADD_BUSY_REFERENCE XdpPollAddBusyReference;
NDIS_POLL_BACKCHANNEL_RELEASE_BUSY_REFERENCE XdpPollReleaseBusyReference;

NTSTATUS
XdpPollStart(
    VOID
    );

VOID
XdpPollStop(
    VOID
    );
