//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID *
GET_ROUTINE_ADDRESS(
    _In_ UNICODE_STRING *RoutineName
    );

#pragma warning(push)
#pragma warning(disable:4152) // nonstandard extension, function/data pointer conversion

NDIS_STATUS
MpInitializePoll(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_STATUS Status;
    GET_ROUTINE_ADDRESS *GetRoutineAddress;
    UNICODE_STRING RoutineName;

    switch (Adapter->PollProvider) {
    case PollProviderNdis:
        GetRoutineAddress = NdisGetRoutineAddress;
        break;

    case PollProviderFndis:
        Status = FNdisClientOpen(&Adapter->FndisClient);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        RtlInitUnicodeString(&RoutineName, L"NdisGetRoutineAddress");
        GetRoutineAddress = FNdisClientGetRoutineAddress(&Adapter->FndisClient, &RoutineName);
        if (GetRoutineAddress == NULL) {
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    RtlInitUnicodeString(&RoutineName, L"NdisRegisterPoll");
    Adapter->PollDispatch.RegisterPoll = GetRoutineAddress(&RoutineName);
    if (Adapter->PollDispatch.RegisterPoll == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    RtlInitUnicodeString(&RoutineName, L"NdisDeregisterPoll");
    Adapter->PollDispatch.DeregisterPoll = GetRoutineAddress(&RoutineName);
    if (Adapter->PollDispatch.DeregisterPoll == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    RtlInitUnicodeString(&RoutineName, L"NdisSetPollAffinity");
    Adapter->PollDispatch.SetPollAffinity = GetRoutineAddress(&RoutineName);
    if (Adapter->PollDispatch.SetPollAffinity == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    RtlInitUnicodeString(&RoutineName, L"NdisRequestPoll");
    Adapter->PollDispatch.RequestPoll = GetRoutineAddress(&RoutineName);
    if (Adapter->PollDispatch.RequestPoll == NULL) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

#pragma warning(pop)

VOID
MpCleanupPoll(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    if (Adapter->PollProvider == PollProviderFndis) {
        FNdisClientClose(&Adapter->FndisClient);
    }
}
