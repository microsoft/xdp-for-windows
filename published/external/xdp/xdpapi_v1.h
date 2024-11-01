//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_XDPAPI_V1_H
#define XDP_XDPAPI_V1_H

//
// This file contains declarations and definitions for the original XDP API.
// It relies on xdpapi.dll to provide an implementation of most routines.
//

#ifdef __cplusplus
extern "C" {
#endif

typedef
HRESULT
XDP_CREATE_PROGRAM_FN(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );

typedef
HRESULT
XDP_INTERFACE_OPEN_FN(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

typedef struct _XDP_API_TABLE XDP_API_TABLE;

#include <afxdp.h>

typedef
HRESULT
XDP_OPEN_API_FN(
    _In_ UINT32 XdpApiVersion,
    _Out_ const XDP_API_TABLE **XdpApiTable
    );

__declspec(deprecated("Use XDP_API_VERSION_3 or greater"))
XDPAPI XDP_OPEN_API_FN XdpOpenApi;

typedef
VOID
XDP_CLOSE_API_FN(
    _In_ const XDP_API_TABLE *XdpApiTable
    );

XDPAPI XDP_CLOSE_API_FN XdpCloseApi;

typedef
VOID *
XDP_GET_ROUTINE_FN(
    _In_z_ const CHAR *RoutineName
    );

typedef struct _XDP_API_TABLE {
    XDP_OPEN_API_FN *XdpOpenApi;
    XDP_CLOSE_API_FN *XdpCloseApi;
    XDP_GET_ROUTINE_FN *XdpGetRoutine;
    XDP_CREATE_PROGRAM_FN *XdpCreateProgram;
    XDP_INTERFACE_OPEN_FN *XdpInterfaceOpen;
    XSK_CREATE_FN *XskCreate;
    XSK_BIND_FN *XskBind;
    XSK_ACTIVATE_FN *XskActivate;
    XSK_NOTIFY_SOCKET_FN *XskNotifySocket;
    XSK_NOTIFY_ASYNC_FN *XskNotifyAsync;
    XSK_GET_NOTIFY_ASYNC_RESULT_FN *XskGetNotifyAsyncResult;
    XSK_SET_SOCKOPT_FN *XskSetSockopt;
    XSK_GET_SOCKOPT_FN *XskGetSockopt;
    XSK_IOCTL_FN *XskIoctl;
} XDP_API_TABLE;

typedef struct _XDP_LOAD_CONTEXT *XDP_LOAD_API_CONTEXT;

#if !defined(_KERNEL_MODE)

inline
__declspec(deprecated("Use XDP_API_VERSION_3 or greater"))
HRESULT
XdpLoadApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ XDP_LOAD_API_CONTEXT *XdpLoadApiContext,
    _Out_ const XDP_API_TABLE **XdpApiTable
    )
{
    HRESULT Result;
    HMODULE XdpHandle;
    XDP_OPEN_API_FN *OpenApi;

    *XdpLoadApiContext = NULL;
    *XdpApiTable = NULL;

    XdpHandle = LoadLibraryA("xdpapi.dll");
    if (XdpHandle == NULL) {
        Result = E_NOINTERFACE;
        goto Exit;
    }

    OpenApi = (XDP_OPEN_API_FN *)GetProcAddress(XdpHandle, "XdpOpenApi");
    if (OpenApi == NULL) {
        Result = E_NOINTERFACE;
        goto Exit;
    }

    Result = OpenApi(XdpApiVersion, XdpApiTable);

Exit:

    if (SUCCEEDED(Result)) {
        *XdpLoadApiContext = (XDP_LOAD_API_CONTEXT)XdpHandle;
    } else {
        if (XdpHandle != NULL) {
            FreeLibrary(XdpHandle);
        }
    }

    return Result;
}

inline
VOID
XdpUnloadApi(
    _In_ XDP_LOAD_API_CONTEXT XdpLoadApiContext,
    _In_ const XDP_API_TABLE *XdpApiTable
    )
{
    HMODULE XdpHandle = (HMODULE)XdpLoadApiContext;

    XdpApiTable->XdpCloseApi(XdpApiTable);

    FreeLibrary(XdpHandle);
}

#endif // !defined(_KERNEL_MODE)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
