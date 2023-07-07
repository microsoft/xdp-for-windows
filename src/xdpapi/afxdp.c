//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include <assert.h>

HRESULT
XskCreate(
    _Out_ HANDLE* Socket
    )
{
    BOOL Res;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH];

    XdpInitializeEa(XDP_OBJECT_TYPE_XSK, EaBuffer, sizeof(EaBuffer));

    *Socket = XdpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (*Socket == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    Res =
        SetFileCompletionNotificationModes(
            *Socket, FILE_SKIP_SET_EVENT_ON_HANDLE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskBind(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    )
{
    BOOL Res;
    XSK_BIND_IN Bind = {0};

    Bind.IfIndex = IfIndex;
    Bind.QueueId = QueueId;
    Bind.Flags = Flags;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_BIND,
            &Bind,
            sizeof(Bind),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    BOOL Res;
    XSK_ACTIVATE_IN Activate = {0};

    Activate.Flags = Flags;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_ACTIVATE,
            &Activate,
            sizeof(Activate),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    BOOL Res;
    XSK_SET_SOCKOPT_IN Sockopt = {0};

    Sockopt.Option = OptionName;
    Sockopt.InputBuffer = OptionValue;
    Sockopt.InputBufferLength = OptionLength;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_SET_SOCKOPT,
            &Sockopt,
            sizeof(Sockopt),
            NULL,
            0,
            NULL,
            NULL,
            FALSE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    BOOL Res;
    DWORD BytesReturned;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_GET_SOCKOPT,
            &OptionName,
            sizeof(OptionName),
            OptionValue,
            *OptionLength,
            &BytesReturned,
            NULL,
            FALSE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *OptionLength = BytesReturned;

    return S_OK;
}

HRESULT
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

    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

HRESULT
XskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    BOOL Res;
    DWORD BytesReturned;
    XSK_NOTIFY_IN Notify = {0};

    Notify.Flags = Flags;
    Notify.WaitTimeoutMilliseconds = WaitTimeoutMilliseconds;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_NOTIFY,
            &Notify,
            sizeof(Notify),
            NULL,
            0,
            &BytesReturned,
            NULL,
            FALSE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *Result = BytesReturned;

    return S_OK;
}

HRESULT
XskNotifyAsync(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ OVERLAPPED *Overlapped
    )
{
    BOOL Res;
    DWORD BytesReturned;
    XSK_NOTIFY_IN Notify = {0};

    Notify.Flags = Flags;
    Notify.WaitTimeoutMilliseconds = INFINITE;

    Res =
        XdpIoctl(
            Socket,
            IOCTL_XSK_NOTIFY_ASYNC,
            &Notify,
            sizeof(Notify),
            NULL,
            0,
            &BytesReturned,
            Overlapped,
            TRUE);
    if (Res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskGetNotifyAsyncResult(
    _In_ OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    IO_STATUS_BLOCK *Iosb = (IO_STATUS_BLOCK *)&Overlapped->Internal;

    if (!NT_SUCCESS(Iosb->Status)) {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Iosb->Status));
    }

    *Result = (XSK_NOTIFY_RESULT_FLAGS)Iosb->Information;

    return S_OK;
}
