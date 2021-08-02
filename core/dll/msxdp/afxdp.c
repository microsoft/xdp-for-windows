//
// Copyright (C) Microsoft Corporation. All rights reserved.
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
    _In_ UINT32 flags,
    _In_opt_ HANDLE sharedUmemSock
    )
{
    BOOL res;
    XSK_BIND_IN bind = { 0 };

    bind.IfIndex = ifIndex;
    bind.QueueId = queueId;
    bind.Flags = flags;
    bind.SharedUmemSock = sharedUmemSock;

    res =
        XdpIoctl(
            socket,
            IOCTL_XSK_BIND,
            &bind,
            sizeof(bind),
            NULL,
            0,
            NULL,
            NULL);
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
    XSK_SET_SOCKOPT_IN sockopt = { 0 };

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
            NULL);
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
            NULL);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *optionLength = bytesReturned;

    return S_OK;
}

HRESULT
XskNotifySocket(
    _In_ HANDLE socket,
    _In_ UINT32 flags,
    _In_ UINT32 waitTimeoutMilliseconds,
    _Out_ UINT32 *result
    )
{
    BOOL res;
    DWORD bytesReturned;
    XSK_NOTIFY_IN notify = { 0 };
    OVERLAPPED overlapped = { 0 };

    //
    // N.B. We know that the notify IOCTL will never pend, so we pass an empty
    // and non-NULL OVERLAPPED parameter to avoid the creation of an unnecessary
    // event object for the sake of performance.
    //

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
            &overlapped);
    if (res == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *result = bytesReturned;

    return S_OK;
}
