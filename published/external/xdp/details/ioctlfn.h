//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_IOCTLFN_H
#define XDP_DETAILS_IOCTLFN_H

#ifdef __cplusplus
extern "C" {
#endif

//
// This header declares the IOCTL interface for the XDP driver.
//

#include <xdp/details/ioctldef.h>
#include <xdp/overlapped.h>
#include <xdp/status.h>

inline
XDP_STATUS
_XdpCloseHandle(
    _In_ HANDLE Handle
    )
{
#ifdef _KERNEL_MODE
    return ZwClose(Handle);
#else
    return CloseHandle(Handle) ?
        XDP_STATUS_SUCCESS : HRESULT_FROM_WIN32(RtlNtStatusToDosError(GetLastError()));
#endif
}

inline
XDP_STATUS
_XdpConvertNtStatusToXdpStatus(
    _In_ NTSTATUS Status
    )
{
#ifdef _KERNEL_MODE
    return Status;
#else
    if (Status != STATUS_SUCCESS) {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
    }
#endif
}

inline
XDP_STATUS
_XdpCreateEvent(
    _Out_ HANDLE *EventHandle
    )
{
#ifdef _KERNEL_MODE
    return ZwCreateEvent(EventHandle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
#else
    *EventHandle = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (*EventHandle == NULL) {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(GetLastError()));
    }
#endif
}

inline
XDP_STATUS
_XdpWaitInfinite(
    _In_ HANDLE *EventHandle
    )
{
#ifdef _KERNEL_MODE
    return ZwWaitForSingleObject(EventHandle, FALSE, NULL);
#else
    DWORD WaitResult = WaitForSingleObject(EventHandle, INFINITE);
    if (WaitResult == WAIT_OBJECT_0) {
        return XDP_STATUS_SUCCESS;
    } else if (WaitResult == WAIT_FAILED) {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(GetLastError()));
    } else {
        return XDP_STATUS_INTERNAL_ERROR;
    }
#endif
}

inline
VOID *
_XdpInitializeEaVersion(
    _In_ XDP_OBJECT_TYPE ObjectType,
    _In_ UINT32 ApiVersion,
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
    OpenPacket->ApiVersion = ApiVersion;
    OpenPacket->ObjectType = ObjectType;

    return OpenPacket + 1;
}

inline
VOID *
_XdpInitializeEa(
    _In_ XDP_OBJECT_TYPE ObjectType,
    _Out_ VOID *EaBuffer,
    _In_ ULONG EaLength
    )
{
    return _XdpInitializeEaVersion(ObjectType, XDP_API_VERSION, EaBuffer, EaLength);
}

inline
XDP_STATUS
_XdpOpen(
    _Out_ HANDLE *Handle,
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength
    )
{
    UNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    //
    // Open a handle to the XDP device.
    //
    RtlInitUnicodeString(&DeviceName, XDP_DEVICE_NAME);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    return
        _XdpConvertNtStatusToXdpStatus(
#ifdef _KERNEL_MODE
            ZwCreateFile
#else
            NtCreateFile
#endif
            (
                Handle,
                GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                &ObjectAttributes,
                &IoStatusBlock,
                NULL,
                0L,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                Disposition,
                0,
                EaBuffer,
                EaLength));
}

inline
XDP_STATUS
_XdpIoctl(
    _In_ HANDLE XdpHandle,
    _In_ ULONG Operation,
    _In_opt_ VOID *InBuffer,
    _In_ ULONG InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ ULONG *BytesReturned,
    _In_opt_ XDP_OVERLAPPED *Overlapped,
    _In_ BOOLEAN MayPend
    )
{
    XDP_STATUS XdpStatus;
    IO_STATUS_BLOCK LocalIoStatusBlock= {0};
    IO_STATUS_BLOCK *IoStatusBlock;
    HANDLE LocalEvent = NULL;
    HANDLE *Event;

    if (Overlapped == NULL) {
        IoStatusBlock = &LocalIoStatusBlock;
        Event = &LocalEvent;
        if (MayPend) {
            XdpStatus = _XdpCreateEvent(&LocalEvent);
            if (XDP_FAILED(XdpStatus)) {
                goto Exit;
            }
        }
    } else {
        IoStatusBlock = (IO_STATUS_BLOCK *)&Overlapped->Internal;
        Event = &Overlapped->hEvent;
    }

    IoStatusBlock->Status = STATUS_PENDING;

    XdpStatus =
        _XdpConvertNtStatusToXdpStatus(
#ifdef _KERNEL_MODE
            ZwDeviceIoControlFile
#else
            NtDeviceIoControlFile
#endif
            (
                XdpHandle, *Event, NULL, Overlapped, IoStatusBlock, Operation, InBuffer,
                InBufferSize, OutBuffer, OutputBufferSize));

    ASSERT(XdpStatus != XDP_STATUS_PENDING || MayPend);

    if (Event == &LocalEvent && XdpStatus == XDP_STATUS_PENDING) {
        XdpStatus = _XdpWaitInfinite(Event);
        if (FAILED(XdpStatus)) {
            goto Exit;
        }

        XdpStatus = _XdpConvertNtStatusToXdpStatus(IoStatusBlock->Status);
    }

    if (BytesReturned != NULL) {
        *BytesReturned = (ULONG)IoStatusBlock->Information;
    }

Exit:

    if (LocalEvent != NULL) {
        _XdpCloseHandle(LocalEvent);
    }

    return XdpStatus;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
