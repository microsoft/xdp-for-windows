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

#include <xdp/details/apiassert.h>
#include <xdp/details/ioctldef.h>
#include <xdp/overlapped.h>
#include <xdp/status.h>

#ifdef _KERNEL_MODE
#define _XDP_OPEN_KERNEL_OBJ_FLAGS OBJ_KERNEL_HANDLE
#else
#define _XDP_OPEN_KERNEL_OBJ_FLAGS 0
#endif

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
    return
        (Status == STATUS_SUCCESS) ?
            XDP_STATUS_SUCCESS : HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
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
    } else {
        return XDP_STATUS_SUCCESS;
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
XDP_STATUS
_XdpSetFileCompletionModes(
    _In_ HANDLE Handle,
    _In_ UCHAR Flags
    )
{
#ifdef _KERNEL_MODE
    FILE_IO_COMPLETION_NOTIFICATION_INFORMATION IoCompletion = {0};
    IO_STATUS_BLOCK IoStatusBlock;

    IoCompletion.Flags = Flags;

    return
        ZwSetInformationFile(
            Handle, &IoStatusBlock, &IoCompletion, sizeof(IoCompletion),
            FileIoCompletionNotificationInformation);
#else
    if (SetFileCompletionNotificationModes(Handle, Flags)) {
        return XDP_STATUS_SUCCESS;
    } else {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(GetLastError()));
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
    XDP_FILE_FULL_EA_INFORMATION *EaHeader = (XDP_FILE_FULL_EA_INFORMATION *)EaBuffer;
    XDP_OPEN_PACKET *OpenPacket;

    XDPAPI_ASSERT(EaLength >= XDP_OPEN_EA_LENGTH);

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

//
// Open a handle to a specific XDP device by name.
//
inline
XDP_STATUS
_XdpOpenDevice(
    _Out_ HANDLE *Handle,
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength,
    _In_ const WCHAR *DeviceNameStr
    )
{
    UNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    RtlInitUnicodeString(&DeviceName, DeviceNameStr);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE | _XDP_OPEN_KERNEL_OBJ_FLAGS,
        NULL, NULL);

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
_XdpOpen(
    _Out_ HANDLE *Handle,
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength
    )
{
    return _XdpOpenDevice(Handle, Disposition, EaBuffer, EaLength, XDP_DEVICE_NAME);
}

//
// Open a handle to the per-object-type XDP device. If the caller has declared
// minimum version compatibility (XDP_MINIMUM_MAJOR_VER / XDP_MINIMUM_MINOR_VER),
// fall back to the common XDP device when the per-type device is not present
// (e.g. when running against an older XDP driver).
//
inline
XDP_STATUS
_XdpOpenObjectType(
    _Out_ HANDLE *Handle,
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength,
    _In_ XDP_OBJECT_TYPE ObjectType
    )
{
    XDP_STATUS Status;

    Status =
        _XdpOpenDevice(
            Handle, Disposition, EaBuffer, EaLength,
            _XdpObjectTypeDeviceName(ObjectType));

#if defined(XDP_MINIMUM_MAJOR_VER) && defined(XDP_MINIMUM_MINOR_VER)
    //
    // Per-type device objects were introduced after XDP 1.3, which was the
    // last version that required all opens to go through the common XDP
    // device. Only fall back to the common device when the caller's minimum
    // supported version is <= 1.3.
    //
#if (XDP_MINIMUM_MAJOR_VER < 1) || (XDP_MINIMUM_MAJOR_VER == 1 && XDP_MINIMUM_MINOR_VER <= 3)
#ifdef _KERNEL_MODE
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
        Status == STATUS_OBJECT_PATH_NOT_FOUND) {
#else
    if (Status == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
        Status == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
#endif
        //
        // The per-type device does not exist. Fall back to the common XDP
        // device for backward compatibility with older drivers.
        //
        Status = _XdpOpen(Handle, Disposition, EaBuffer, EaLength);
    }
#endif
#endif

    return Status;
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

    XDPAPI_ASSERT(XdpStatus != XDP_STATUS_PENDING || MayPend);

    if (Event == &LocalEvent && XdpStatus == XDP_STATUS_PENDING) {
        XdpStatus = _XdpWaitInfinite(Event);
        if (XDP_FAILED(XdpStatus)) {
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

#undef _XDP_OPEN_KERNEL_OBJ_FLAGS

#ifdef __cplusplus
} // extern "C"
#endif

#endif
