//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include <assert.h>

HRESULT
XskCreate(
    _Out_ HANDLE* socket
    )
{
    BOOL res;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH];

    XdpInitializeEa(XDP_OBJECT_TYPE_XSK, EaBuffer, sizeof(EaBuffer));

    *socket = XdpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (*socket == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    res =
        SetFileCompletionNotificationModes(
            *socket, FILE_SKIP_SET_EVENT_ON_HANDLE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskBind(
    _In_ HANDLE socket,
    _In_ UINT32 ifIndex,
    _In_ UINT32 queueId,
    _In_ XSK_BIND_FLAGS flags
    )
{
    BOOL res;
    XSK_BIND_IN bind = {0};

    bind.IfIndex = ifIndex;
    bind.QueueId = queueId;
    bind.Flags = flags;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_BIND,
            &bind,
            sizeof(bind),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskActivate(
    _In_ HANDLE socket,
    _In_ XSK_ACTIVATE_FLAGS flags
    )
{
    BOOL res;
    XSK_ACTIVATE_IN activate = {0};

    activate.Flags = flags;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_ACTIVATE,
            &activate,
            sizeof(activate),
            NULL,
            0,
            NULL,
            NULL,
            TRUE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskSetSockopt(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _In_reads_bytes_opt_(optionLength) const VOID *optionValue,
    _In_ UINT32 optionLength
    )
{
    BOOL res;
    XSK_SET_SOCKOPT_IN sockopt = {0};

    sockopt.Option = optionName;
    sockopt.InputBuffer = optionValue;
    sockopt.InputBufferLength = optionLength;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_SET_SOCKOPT,
            &sockopt,
            sizeof(sockopt),
            NULL,
            0,
            NULL,
            NULL,
            FALSE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskGetSockopt(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _Out_writes_bytes_(*optionLength) VOID *optionValue,
    _Inout_ UINT32 *optionLength
    )
{
    BOOL res;
    DWORD bytesReturned;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_GET_SOCKOPT,
            &optionName,
            sizeof(optionName),
            optionValue,
            *optionLength,
            &bytesReturned,
            NULL,
            FALSE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *optionLength = bytesReturned;

    return S_OK;
}

HRESULT
XskIoctl(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _In_reads_bytes_opt_(inputLength) const VOID *inputValue,
    _In_ UINT32 inputLength,
    _Out_writes_bytes_(*outputLength) VOID *outputValue,
    _Inout_ UINT32 *outputLength
    )
{
    UNREFERENCED_PARAMETER(socket);
    UNREFERENCED_PARAMETER(optionName);
    UNREFERENCED_PARAMETER(inputValue);
    UNREFERENCED_PARAMETER(inputLength);
    UNREFERENCED_PARAMETER(outputValue);
    UNREFERENCED_PARAMETER(outputLength);

    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

HRESULT
XskNotifySocket(
    _In_ HANDLE socket,
    _In_ XSK_NOTIFY_FLAGS flags,
    _In_ UINT32 waitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *result
    )
{
    BOOL res;
    DWORD bytesReturned;
    XSK_NOTIFY_IN notify = {0};

    notify.Flags = flags;
    notify.WaitTimeoutMilliseconds = waitTimeoutMilliseconds;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_NOTIFY,
            &notify,
            sizeof(notify),
            NULL,
            0,
            &bytesReturned,
            NULL,
            FALSE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *result = bytesReturned;

    return S_OK;
}

HRESULT
XskNotifyAsync(
    _In_ HANDLE socket,
    _In_ XSK_NOTIFY_FLAGS flags,
    _Inout_ OVERLAPPED *overlapped
    )
{
    BOOL res;
    DWORD bytesReturned;
    XSK_NOTIFY_IN notify = {0};

    notify.Flags = flags;
    notify.WaitTimeoutMilliseconds = INFINITE;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_NOTIFY_ASYNC,
            &notify,
            sizeof(notify),
            NULL,
            0,
            &bytesReturned,
            overlapped,
            TRUE);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XskGetNotifyAsyncResult(
    _In_ OVERLAPPED *overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *result
    )
{
    IO_STATUS_BLOCK *Iosb = (IO_STATUS_BLOCK *)&overlapped->Internal;

    if (!NT_SUCCESS(Iosb->Status)) {
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Iosb->Status));
    }

    *result = (XSK_NOTIFY_RESULT_FLAGS)Iosb->Information;

    return S_OK;
}
