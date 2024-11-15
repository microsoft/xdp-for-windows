//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_AFXDP_H
#define XDP_DETAILS_AFXDP_H

#include <afxdp.h>
#include <xdp/status.h>
#include <xdp/details/ioctldef.h>
#include <xdp/details/ioctlfn.h>

#ifdef __cplusplus
extern "C" {
#endif

inline
XDP_STATUS
_XskCreateVersion(
    _Out_ HANDLE *Socket,
    _In_ UINT32 ApiVersion
    )
{
    XDP_STATUS Res;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH];

    _XdpInitializeEaVersion(XDP_OBJECT_TYPE_XSK, ApiVersion, EaBuffer, sizeof(EaBuffer));

    Res = _XdpOpen(Socket, FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (XDP_FAILED(Res)) {
        return Res;
    }

    //
    // Performance optimization: skip setting the file handle upon each IO completion.
    //
    Res = _XdpSetFileCompletionModes(*Socket, FILE_SKIP_SET_EVENT_ON_HANDLE);
    if (XDP_FAILED(Res)) {
        _XdpCloseHandle(*Socket);
    }

    return Res;
}

inline
XDP_STATUS
XskCreate(
    _Out_ HANDLE *Socket
    )
{
    return _XskCreateVersion(Socket, XDP_API_VERSION);
}

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

inline
XDP_STATUS
XskBind(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    )
{
    XSK_BIND_IN Bind = {};

    Bind.IfIndex = IfIndex;
    Bind.QueueId = QueueId;
    Bind.Flags = Flags;

    return
        _XdpIoctl(
            Socket,
            IOCTL_XSK_BIND,
            &Bind,
            sizeof(Bind),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
}

//
// Input struct for IOCTL_XSK_ACTIVATE
//
typedef struct _XSK_ACTIVATE_IN {
    XSK_ACTIVATE_FLAGS Flags;
} XSK_ACTIVATE_IN;

inline
XDP_STATUS
XskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    XSK_ACTIVATE_IN Activate = {};

    Activate.Flags = Flags;

    return
        _XdpIoctl(
            Socket,
            IOCTL_XSK_ACTIVATE,
            &Activate,
            sizeof(Activate),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
}


//
// Input struct for IOCTL_XSK_SET_SOCKOPT
//
typedef struct _XSK_SET_SOCKOPT_IN {
    UINT32 Option;
    UINT32 InputBufferLength;
    const VOID *InputBuffer;
} XSK_SET_SOCKOPT_IN;

inline
XDP_STATUS
XskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    XSK_SET_SOCKOPT_IN Sockopt = {};

    Sockopt.Option = OptionName;
    Sockopt.InputBuffer = OptionValue;
    Sockopt.InputBufferLength = OptionLength;

    return
        _XdpIoctl(
            Socket,
            IOCTL_XSK_SET_SOCKOPT,
            &Sockopt,
            sizeof(Sockopt),
            NULL,
            0,
            NULL,
            NULL,
            FALSE);
}

inline
XDP_STATUS
XskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    XDP_STATUS Res;
    DWORD BytesReturned;

    Res =
        _XdpIoctl(
            Socket,
            IOCTL_XSK_GET_SOCKOPT,
            &OptionName,
            sizeof(OptionName),
            OptionValue,
            *OptionLength,
            &BytesReturned,
            NULL,
            FALSE);
    if (XDP_FAILED(Res)) {
        return Res;
    }

    *OptionLength = BytesReturned;

    return Res;
}

inline
XDP_STATUS
XskIoctl(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    )
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(OptionName);
    UNREFERENCED_PARAMETER(InputValue);
    UNREFERENCED_PARAMETER(InputLength);
    UNREFERENCED_PARAMETER(OutputValue);
    UNREFERENCED_PARAMETER(OutputLength);

    return XDP_STATUS_NOT_SUPPORTED;
}

//
// Input struct for IOCTL_XSK_NOTIFY
//
typedef struct _XSK_NOTIFY_IN {
    XSK_NOTIFY_FLAGS Flags;
    UINT32 WaitTimeoutMilliseconds;
} XSK_NOTIFY_IN;

inline
XDP_STATUS
XskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    XDP_STATUS Res;
    DWORD BytesReturned;
    XSK_NOTIFY_IN Notify = {};

    Notify.Flags = Flags;
    Notify.WaitTimeoutMilliseconds = WaitTimeoutMilliseconds;

    Res =
        _XdpIoctl(
            Socket,
            IOCTL_XSK_NOTIFY,
            &Notify,
            sizeof(Notify),
            NULL,
            0,
            &BytesReturned,
            NULL,
            FALSE);
    if (XDP_FAILED(Res)) {
        return Res;
    }

    *Result = (XSK_NOTIFY_RESULT_FLAGS)BytesReturned;

    return Res;
}

inline
XDP_STATUS
XskNotifyAsync(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ XDP_OVERLAPPED *Overlapped
    )
{
    DWORD BytesReturned;
    XSK_NOTIFY_IN Notify = {};

    Notify.Flags = Flags;
    Notify.WaitTimeoutMilliseconds = XDP_INFINITE;

    return
        _XdpIoctl(
            Socket,
            IOCTL_XSK_NOTIFY_ASYNC,
            &Notify,
            sizeof(Notify),
            NULL,
            0,
            &BytesReturned,
            Overlapped,
            TRUE);
}

inline
XDP_STATUS
XskGetNotifyAsyncResult(
    _In_ XDP_OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    IO_STATUS_BLOCK *Iosb = (IO_STATUS_BLOCK *)&Overlapped->Internal;

    if (!NT_SUCCESS(Iosb->Status)) {
        XDP_STATUS Status;
        Status = _XdpConvertNtStatusToXdpStatus(Iosb->Status);
        XDPAPI_ASSERT(XDP_FAILED(Status));
        _Analysis_assume_(XDP_FAILED(Status));
        return Status;
    }

    *Result = (XSK_NOTIFY_RESULT_FLAGS)Iosb->Information;

    return XDP_STATUS_SUCCESS;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
