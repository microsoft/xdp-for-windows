//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_APIIOCTL_H
#define XDP_DETAILS_APIIOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

//
// This header declares the IOCTL interface for the XDP driver.
//

#include <afxdp.h>
#include <xdpapi.h>
#include <xdp/overlapped.h>
#include <xdp/program.h>
#include <xdp/status.h>

#ifndef _KERNEL_MODE
#include <windows.h>
#include <ntstatus.h>
#include <winioctl.h>
#include <winternl.h>
#endif

#define XDP_DEVICE_NAME L"\\Device\\xdp"

#define XDP_OPEN_PACKET_NAME "XdpOpenPacket000"

CONST GUID DECLSPEC_SELECTANY XDP_DEVICE_CLASS_GUID = { /* 28f93d3f-4c0a-4a7c-8ff1-96b24e19b856 */
    0x28f93d3f,
    0x4c0a,
    0x4a7c,
    {0x8f, 0xf1, 0x96, 0xb2, 0x4e, 0x19, 0xb8, 0x56}
};

//
// TODO: mangle/prefix all internal typedefs/macros with _
//

//
// Type of XDP object to create or open.
//
typedef enum _XDP_OBJECT_TYPE {
    XDP_OBJECT_TYPE_PROGRAM,
    XDP_OBJECT_TYPE_XSK,
    XDP_OBJECT_TYPE_INTERFACE,
} XDP_OBJECT_TYPE;

//
// XDP open packet, which is our common header for NtCreateFile extended
// attributes.
//
typedef struct _XDP_OPEN_PACKET {
    UINT16 MajorVersion;
    UINT16 MinorVersion;
    UINT32 ApiVersion;
    XDP_OBJECT_TYPE ObjectType;
} XDP_OPEN_PACKET;

//
// Parameters for creating an XDP_OBJECT_TYPE_PROGRAM.
//
typedef struct _XDP_PROGRAM_OPEN {
    UINT32 IfIndex;
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
    XDP_CREATE_PROGRAM_FLAGS Flags;
    UINT32 RuleCount;
    const XDP_RULE *Rules;
} XDP_PROGRAM_OPEN;

//
// Parameters for creating an XDP_OBJECT_TYPE_INTERFACE.
//
typedef struct _XDP_INTERFACE_OPEN {
    UINT32 IfIndex;
} XDP_INTERFACE_OPEN;

//
// IOCTLs supported by an interface file handle.
//
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_QEO_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Define IOCTLs supported by an XSK file handle.
//

#define IOCTL_XSK_BIND \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_ACTIVATE \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_GET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_SET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_NOTIFY \
    CTL_CODE(FILE_DEVICE_NETWORK, 4, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define IOCTL_XSK_NOTIFY_ASYNC \
    CTL_CODE(FILE_DEVICE_NETWORK, 5, METHOD_NEITHER, FILE_WRITE_ACCESS)

//
// Input struct for IOCTL_XSK_BIND
//
typedef struct _XSK_BIND_IN {
    UINT32 IfIndex;
    UINT32 QueueId;
    XSK_BIND_FLAGS Flags;
} XSK_BIND_IN;

//
// Input struct for IOCTL_XSK_ACTIVATE
//
typedef struct _XSK_ACTIVATE_IN {
    XSK_ACTIVATE_FLAGS Flags;
} XSK_ACTIVATE_IN;

//
// Input struct for IOCTL_XSK_SET_SOCKOPT
//
typedef struct _XSK_SET_SOCKOPT_IN {
    UINT32 Option;
    UINT32 InputBufferLength;
    const VOID *InputBuffer;
} XSK_SET_SOCKOPT_IN;

//
// Input struct for IOCTL_XSK_NOTIFY
//
typedef struct _XSK_NOTIFY_IN {
    XSK_NOTIFY_FLAGS Flags;
    UINT32 WaitTimeoutMilliseconds;
} XSK_NOTIFY_IN;

#ifndef _KERNEL_MODE

//
// This struct is defined in public kernel headers, but not user mode headers.
//
typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION;

#endif

#define XDP_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + sizeof(XDP_OPEN_PACKET_NAME) + sizeof(XDP_OPEN_PACKET))

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
