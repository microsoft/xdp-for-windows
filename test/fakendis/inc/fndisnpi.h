//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fndisioctl.h>

typedef struct _FNDIS_NPI_CLIENT {
    HANDLE Handle;
} FNDIS_NPI_CLIENT;

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FNdisClientOpen(
    _Out_ FNDIS_NPI_CLIENT *FndisClient
    )
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Oa;
    IO_STATUS_BLOCK Iosb;

    RtlZeroMemory(FndisClient, sizeof(*FndisClient));
    RtlInitUnicodeString(&Name, FNDIS_DEVICE_NAME);
    InitializeObjectAttributes(&Oa, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status =
        ZwCreateFile(
            &FndisClient->Handle, GENERIC_READ, &Oa, &Iosb, NULL, 0L, 0, FILE_OPEN_IF, 0, NULL, 0);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    return STATUS_SUCCESS;
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID
FNdisClientClose(
    _In_ FNDIS_NPI_CLIENT *FndisClient
    )
{
    if (FndisClient->Handle != NULL) {
        ZwClose(FndisClient->Handle);
        FndisClient->Handle = NULL;
    }
}

#ifdef XDP_POLL_BACKCHANNEL

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FNdisClientGetPollBackchannel(
    _In_ FNDIS_NPI_CLIENT *FndisClient,
    _Out_ NDIS_POLL_BACKCHANNEL_DISPATCH **Dispatch
    )
{
    IO_STATUS_BLOCK Iosb;

    return
        ZwDeviceIoControlFile(
            FndisClient->Handle, NULL, NULL, NULL, &Iosb, IOCTL_FNDIS_POLL_GET_BACKCHANNEL, NULL, 0,
            (VOID *)Dispatch, sizeof(*Dispatch));
}

#endif

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID *
FNdisClientGetRoutineAddress(
    _In_ FNDIS_NPI_CLIENT *FndisClient,
    _In_ UNICODE_STRING *RoutineName
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;
    FNDIS_POLL_GET_ROUTINE_ADDRESS_IN In = {0};
    FNDIS_POLL_GET_ROUTINE_ADDRESS_OUT Out;

    In.RoutineName = RoutineName;

    Status =
        ZwDeviceIoControlFile(
            FndisClient->Handle, NULL, NULL, NULL, &Iosb, IOCTL_FNDIS_POLL_GET_ROUTINE_ADDRESS,
            &In, sizeof(In), &Out, sizeof(Out));
    if (!NT_SUCCESS(Status)) {
        Out.Routine = NULL;
    }

    return Out.Routine;
}
