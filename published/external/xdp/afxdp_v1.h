//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_AFXDP_V1_H
#define XDP_AFXDP_V1_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Include all necessary Windows headers first.
//
#include <xdp/wincommon.h>

#include <xdp/apiversion.h>
#include <xdp/hookid.h>
#include <xdp/overlapped.h>
#include <afxdp.h>

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef
HRESULT
XSK_CREATE_FN(
    _Out_ HANDLE* Socket
    );

typedef
HRESULT
XSK_BIND_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    );

typedef
HRESULT
XSK_ACTIVATE_FN(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    );

typedef
HRESULT
XSK_NOTIFY_SOCKET_FN(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    );

typedef
HRESULT
XSK_NOTIFY_ASYNC_FN(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ OVERLAPPED *Overlapped
    );

typedef
HRESULT
XSK_GET_NOTIFY_ASYNC_RESULT_FN(
    _In_ OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    );

typedef
HRESULT
XSK_SET_SOCKOPT_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    );

typedef
HRESULT
XSK_GET_SOCKOPT_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_opt_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    );

typedef
HRESULT
XSK_IOCTL_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_opt_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
