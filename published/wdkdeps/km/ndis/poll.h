// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/version.h>
#include <ndis/types.h>
#include <ndis/objectheader.h>
#include <ndis/status.h>
#include <ndis/nbl.h>

#if (NDIS_SUPPORT_NDIS685)

EXTERN_C_START

DECLARE_HANDLE(NDIS_POLL_HANDLE);

#define NDIS_ANY_NUMBER_OF_NBLS 0xFFFFFFFF

typedef struct _NDIS_POLL_TRANSMIT_DATA
{
    ULONG MaxNblsToComplete;
    ULONG Reserved1[3];

    NET_BUFFER_LIST *CompletedNblChain;
    ULONG NumberOfCompletedNbls;
    ULONG NumberOfRemainingNbls;
    ULONG SendCompleteFlags;

    ULONG Reserved2;
    void * Reserved3[4];
} NDIS_POLL_TRANSMIT_DATA;

typedef struct _NDIS_POLL_RECEIVE_DATA
{
    ULONG MaxNblsToIndicate;
    ULONG Reserved1[3];

    NET_BUFFER_LIST *IndicatedNblChain;
    ULONG NumberOfIndicatedNbls;
    ULONG NumberOfRemainingNbls;
    ULONG Flags;

    ULONG Reserved2;
    void * Reserved3[4];
} NDIS_POLL_RECEIVE_DATA;

typedef struct _NDIS_POLL_DATA
{
    NDIS_OBJECT_HEADER Header;
    NDIS_POLL_TRANSMIT_DATA Transmit;
    NDIS_POLL_RECEIVE_DATA Receive;
} NDIS_POLL_DATA;

#define NDIS_POLL_DATA_REVISION_1 1
#define NDIS_SIZEOF_NDIS_POLL_DATA_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_POLL_DATA, Receive)

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(NDIS_POLL)
void
(NDIS_POLL)(
    _In_ void * Context,
    _Inout_ NDIS_POLL_DATA * PollData
);

typedef NDIS_POLL * NDIS_POLL_HANDLER;

typedef struct _NDIS_POLL_NOTIFICATION
{
    NDIS_OBJECT_HEADER Header;
    BOOLEAN Enabled;
} NDIS_POLL_NOTIFICATION;

#define NDIS_POLL_NOTIFICATION_REVISION_1 1
#define NDIS_SIZEOF_NDIS_POLL_NOTIFICATION_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_POLL_NOTIFICATION, Enabled)

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(NDIS_SET_POLL_NOTIFICATION)
void
(NDIS_SET_POLL_NOTIFICATION)(
    _In_ void * Context,
    _Inout_ NDIS_POLL_NOTIFICATION * Notification
);

typedef NDIS_SET_POLL_NOTIFICATION * NDIS_SET_POLL_NOTIFICATION_HANDLER;

typedef struct _NDIS_POLL_CHARACTERISTICS
{
    NDIS_OBJECT_HEADER Header;

    NDIS_SET_POLL_NOTIFICATION_HANDLER SetPollNotificationHandler;
    NDIS_POLL_HANDLER PollHandler;
} NDIS_POLL_CHARACTERISTICS;

#define NDIS_POLL_CHARACTERISTICS_REVISION_1 1
#define NDIS_SIZEOF_NDIS_POLL_CHARACTERISTICS_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_POLL_CHARACTERISTICS, PollHandler)

_IRQL_requires_(PASSIVE_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_STATUS
NdisRegisterPoll(
    _In_ NDIS_HANDLE NdisHandle,
    _In_opt_ void * Context,
    _In_ NDIS_POLL_CHARACTERISTICS const * Characteristics,
    _Out_ NDIS_POLL_HANDLE * PollHandle
);

_IRQL_requires_(PASSIVE_LEVEL)
NDIS_EXPORTED_ROUTINE
void
NdisDeregisterPoll(
    _In_ NDIS_POLL_HANDLE PollHandle
);

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
void
NdisSetPollAffinity(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _In_ PROCESSOR_NUMBER const * PollAffinity
);

_IRQL_requires_max_(HIGH_LEVEL)
NDIS_EXPORTED_ROUTINE
void
NdisRequestPoll(
    _In_ NDIS_POLL_HANDLE PollHandle,
    _Reserved_ void * Reserved
);

EXTERN_C_END

#endif // NDIS_SUPPORT_NDIS685
#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#pragma endregion
