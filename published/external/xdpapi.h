//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDPAPI_H
#define XDPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xdp/hookid.h>
#include <xdp/objectheader.h>
#include <xdp/program.h>
#include <xdpapi_status.h>

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

#if defined(_KERNEL_MODE)
typedef VOID XDP_API_PROVIDER_BINDING_CONTEXT;
#endif // defined(_KERNEL_MODE)

typedef enum _XDP_CREATE_PROGRAM_FLAGS {
    XDP_CREATE_PROGRAM_FLAG_NONE = 0x0,
    XDP_CREATE_PROGRAM_FLAG_GENERIC = 0x1,
    XDP_CREATE_PROGRAM_FLAG_NATIVE = 0x2,
    XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES = 0x4,
} XDP_CREATE_PROGRAM_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XDP_CREATE_PROGRAM_FLAGS);
C_ASSERT(sizeof(XDP_CREATE_PROGRAM_FLAGS) == sizeof(UINT32));

#if defined(_KERNEL_MODE)

typedef
XDP_STATUS
XDP_CREATE_PROGRAM_FN(
    _In_ XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext,
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );

typedef
XDP_STATUS
XDP_INTERFACE_OPEN_FN(
    _In_ XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext,
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

#else

typedef
XDP_STATUS
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
XDP_STATUS
XDP_INTERFACE_OPEN_FN(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

#endif

#include "afxdp.h"

typedef
VOID *
XDP_GET_ROUTINE_FN(
    _In_z_ const CHAR *RoutineName
    );

//
// The only API version currently supported.
//
#define XDP_API_VERSION_1 1

#if defined(_KERNEL_MODE)

typedef
VOID
XDP_DELETE_PROGRAM_FN(
    _In_ HANDLE Program
    );

typedef
VOID
XDP_INTERFACE_CLOSE_FN(
    _In_ HANDLE InterfaceHandle
    );

DEFINE_GUID(
    NPI_XDPAPI_INTERFACE_ID,
    0x1683b7b0, 0xcf44, 0x4757, 0x91, 0xf6, 0xc6, 0x19, 0x9b, 0x9d, 0x00, 0xfe);

typedef struct _XDP_API_PROVIDER_DISPATCH {
    XDP_GET_ROUTINE_FN *XdpGetRoutine;
    XDP_CREATE_PROGRAM_FN *XdpCreateProgram;
    XDP_DELETE_PROGRAM_FN *XdpDeleteProgram;
    XDP_INTERFACE_OPEN_FN *XdpInterfaceOpen;
    XDP_INTERFACE_CLOSE_FN *XdpInterfaceClose;
    XSK_CREATE_FN *XskCreate;
    XSK_DELETE_FN *XskDelete;
    XSK_BIND_FN *XskBind;
    XSK_ACTIVATE_FN *XskActivate;
    XSK_NOTIFY_SOCKET_FN *XskNotifySocket;
    XSK_NOTIFY_ASYNC2_FN *XskNotifyAsync2;
    XSK_SET_SOCKOPT_FN *XskSetSockopt;
    XSK_GET_SOCKOPT_FN *XskGetSockopt;
    XSK_IOCTL_FN *XskIoctl;
} XDP_API_PROVIDER_DISPATCH;

typedef struct _XDP_API_CLIENT_DISPATCH {
    XSK_NOTIFY_CALLBACK *XskNotifyCallback;
} XDP_API_CLIENT_DISPATCH;

#else

typedef struct _XDP_API_TABLE XDP_API_TABLE;

typedef
HRESULT
XDP_OPEN_API_FN(
    _In_ UINT32 XdpApiVersion,
    _Out_ const XDP_API_TABLE **XdpApiTable
    );

XDPAPI XDP_OPEN_API_FN XdpOpenApi;

typedef
VOID
XDP_CLOSE_API_FN(
    _In_ const XDP_API_TABLE *XdpApiTable
    );

XDPAPI XDP_CLOSE_API_FN XdpCloseApi;


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

inline
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

#endif // defined(_KERNEL_MODE)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
