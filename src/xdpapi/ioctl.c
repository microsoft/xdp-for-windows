//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// This file implements common file handle and IOCTL helpers.
//

VOID *
XdpInitializeEa(
    _In_ XDP_OBJECT_TYPE ObjectType,
    _Out_ VOID *EaBuffer,
    _In_ ULONG EaLength
    )
{
    FILE_FULL_EA_INFORMATION *EaHeader = EaBuffer;
    XDP_OPEN_PACKET *OpenPacket;

    if (EaLength < XDP_OPEN_EA_LENGTH) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }

    RtlZeroMemory(EaHeader, sizeof(*EaHeader));
    EaHeader->EaNameLength = sizeof(XDP_OPEN_PACKET_NAME) - 1;
    RtlCopyMemory(EaHeader->EaName, XDP_OPEN_PACKET_NAME, sizeof(XDP_OPEN_PACKET_NAME));
    EaHeader->EaValueLength = (USHORT)(EaLength - sizeof(*EaHeader) - sizeof(XDP_OPEN_PACKET_NAME));

    OpenPacket = (XDP_OPEN_PACKET *)(EaHeader->EaName + sizeof(XDP_OPEN_PACKET_NAME));
    OpenPacket->MajorVersion = 1;
    OpenPacket->MinorVersion = 0;
    OpenPacket->ObjectType = ObjectType;

    return OpenPacket + 1;
}

HANDLE
XdpOpen(
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength
    )
{
    UNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS NtStatus;

    //
    // Open a handle to the XDP device.
    //
    RtlInitUnicodeString(&DeviceName, XDP_DEVICE_NAME);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NtStatus =
        NtCreateFile(
            &Handle,
            GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
            &ObjectAttributes,
            &IoStatusBlock,
            NULL,
            0L,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            Disposition,
            0,
            EaBuffer,
            EaLength);

    if (!NT_SUCCESS(NtStatus)) {
        SetLastError(RtlNtStatusToDosError(NtStatus));
        return NULL;
    }

    return Handle;
}

_Success_(return != FALSE)
BOOL
XdpIoctl(
    _In_ HANDLE XdpHandle,
    _In_ ULONG Operation,
    _In_opt_ VOID *InBuffer,
    _In_ ULONG InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ ULONG *BytesReturned,
    _In_opt_ OVERLAPPED *Overlapped,
    _In_ BOOLEAN MayPend
    )
{
    BOOL Result = FALSE;
    NTSTATUS Status;
    IO_STATUS_BLOCK LocalIoStatusBlock= {0};
    IO_STATUS_BLOCK *IoStatusBlock;
    HANDLE LocalEvent = NULL;
    HANDLE *Event;

    if (Overlapped == NULL) {
        IoStatusBlock = &LocalIoStatusBlock;
        Event = &LocalEvent;
        if (MayPend) {
            LocalEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
            if (LocalEvent == NULL) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                goto Exit;
            }
        }
    } else {
        IoStatusBlock = (IO_STATUS_BLOCK *)&Overlapped->Internal;
        Event = &Overlapped->hEvent;
    }

    IoStatusBlock->Status = STATUS_PENDING;

    Status =
        NtDeviceIoControlFile(
            XdpHandle, *Event, NULL, Overlapped, IoStatusBlock, Operation, InBuffer,
            InBufferSize, OutBuffer, OutputBufferSize);

    ASSERT(Status != STATUS_PENDING || MayPend);

    if (Event == &LocalEvent && Status == STATUS_PENDING) {
        DWORD WaitResult = WaitForSingleObject(*Event, INFINITE);
        if (WaitResult != WAIT_OBJECT_0) {
            if (WaitResult != WAIT_FAILED) {
                SetLastError(ERROR_GEN_FAILURE);
            }
            goto Exit;
        }

        Status = IoStatusBlock->Status;
    }

    if (BytesReturned != NULL) {
        *BytesReturned = (ULONG)IoStatusBlock->Information;
    }

    if (Status == STATUS_SUCCESS) {
        Result = TRUE;
    } else {
        SetLastError(RtlNtStatusToDosError(Status));
    }

Exit:

    if (LocalEvent != NULL) {
        CloseHandle(LocalEvent);
    }

    return Result;
}
