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
#include <xdp/status.h>

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
_IRQL_requires_(PASSIVE_LEVEL)
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
_IRQL_requires_(PASSIVE_LEVEL)
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
_IRQL_requires_(PASSIVE_LEVEL)
VOID *
XDP_GET_ROUTINE_FN(
    _In_z_ const CHAR *RoutineName
    );

#define XDP_API_VERSION_1 1

#define XDP_API_VERSION_2 2

//
// This version is always the latest supported version.
//
#define XDP_API_VERSION_LATEST XDP_API_VERSION_2

#if defined(_KERNEL_MODE)

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_DELETE_PROGRAM_FN(
    _In_ HANDLE Program
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
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

typedef struct _XDP_API_CLIENT *XDP_API_CLIENT;

inline
HRESULT
XdpLoadApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ XDP_API_CLIENT *XdpLoadApiContext,
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
        *XdpLoadApiContext = (XDP_API_CLIENT)XdpHandle;
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
    _In_ XDP_API_CLIENT XdpLoadApiContext,
    _In_ const XDP_API_TABLE *XdpApiTable
    )
{
    HMODULE XdpHandle = (HMODULE)XdpLoadApiContext;

    XdpApiTable->XdpCloseApi(XdpApiTable);

    FreeLibrary(XdpHandle);
}

#endif // defined(_KERNEL_MODE)

#if defined(_KERNEL_MODE)

#include <netioddk.h>
#include <xdp/guid.h>
#include "cxplat.h"

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_API_ATTACH_FN(
    _In_ VOID *ClientContext
    );

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_API_DETACH_FN(
    _In_ VOID *ClientContext
    );

typedef struct _XDP_API_CLIENT {
    CXPLAT_LOCK Lock;

    //
    // NMR client registration.
    //
    NPI_CLIENT_CHARACTERISTICS NpiClientCharacteristics;
    NPI_MODULEID ModuleId;
    HANDLE NmrClientHandle;

    //
    // XDPAPI client.
    //
    VOID *Context;
    XDP_API_ATTACH_FN *Attach;
    XDP_API_DETACH_FN *Detach;
    const XDP_API_CLIENT_DISPATCH *XdpApiClientDispatch;

    //
    // XDPAPI provider.
    //
    HANDLE BindingHandle;
    XDP_API_PROVIDER_DISPATCH *XdpApiProviderDispatch;
    XDP_API_PROVIDER_BINDING_CONTEXT *XdpApiProviderContext;
} XDP_API_CLIENT;

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpNmrClientAttachProvider(
    _In_ HANDLE _NmrBindingHandle,
    _In_ VOID *_ClientContext,
    _In_ const NPI_REGISTRATION_INSTANCE *_ProviderRegistrationInstance
    )
{
    XDP_API_CLIENT *_Client = (XDP_API_CLIENT *)_ClientContext;
    NTSTATUS _Status;
    VOID *_ProviderBindingContext;
    const VOID *_ProviderBindingDispatch;

    //
    // The NMR client allows at most one active binding at a time, but defers
    // all remaining binding logic to the NMR provider.
    //

    CxPlatLockAcquire(&_Client->Lock);

    if (_Client->BindingHandle != NULL) {
        _Status = STATUS_DEVICE_NOT_READY;
    } else if (_ProviderRegistrationInstance->Number != XDP_API_VERSION_1) {
        _Status = STATUS_NOINTERFACE;
    } else {
        _Status =
            NmrClientAttachProvider(
                _NmrBindingHandle, _Client, &_Client->XdpApiClientDispatch,
                &_ProviderBindingContext, &_ProviderBindingDispatch);

        if (NT_SUCCESS(_Status)) {
            _Client->BindingHandle = _NmrBindingHandle;
            _Client->XdpApiProviderDispatch = (XDP_API_PROVIDER_DISPATCH *)_ProviderBindingDispatch;
            _Client->XdpApiProviderContext = _ProviderBindingContext;
        }
    }

    CxPlatLockRelease(&_Client->Lock);

    if (NT_SUCCESS(_Status) && _Client->Attach != NULL) {
        _Client->Attach(_Client->Context);
    }

    return _Status;
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpNmrClientDetachProvider(
    _In_ VOID *_ClientBindingContext
    )
{
    XDP_API_CLIENT *_Client = (XDP_API_CLIENT *)_ClientBindingContext;

    if (_Client->Detach != NULL) {
        _Client->Detach(_Client->Context);
    }

    CxPlatLockAcquire(&_Client->Lock);

    _Client->BindingHandle = NULL;

    CxPlatLockRelease(&_Client->Lock);

    return STATUS_SUCCESS;
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpCleanupClientRegistration(
    _In_ XDP_API_CLIENT *_Client
    )
{
    NTSTATUS _Status;

    if (_Client->NmrClientHandle != NULL) {
        _Status = NmrDeregisterClient(_Client->NmrClientHandle);
        if (!NT_VERIFY(_Status == STATUS_PENDING)) {
            RtlFailFast(FAST_FAIL_INVALID_ARG);
        }

        _Status = NmrWaitForClientDeregisterComplete(_Client->NmrClientHandle);
        if (!NT_VERIFY(_Status == STATUS_SUCCESS)) {
            RtlFailFast(FAST_FAIL_INVALID_ARG);
        }
    }

    CxPlatLockUninitialize(&_Client->Lock);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpRegister(
    _In_ UINT32 _XdpApiVersion,
    _In_opt_ VOID *_ClientContext,
    _In_opt_ XDP_API_ATTACH_FN *_ClientAttach,
    _In_opt_ XDP_API_DETACH_FN *_ClientDetach,
    _In_ const XDP_API_CLIENT_DISPATCH *_XdpApiClientDispatch,
    _Out_ XDP_API_CLIENT *_Client
    )
{
    NTSTATUS _Status;
    NPI_CLIENT_CHARACTERISTICS *_NpiCharacteristics;
    NPI_REGISTRATION_INSTANCE *_NpiInstance;

    if (_XdpApiVersion != XDP_API_VERSION_1 ||
        _Client == NULL ||
        _ClientDetach == NULL) {
        _Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    CxPlatLockInitialize(&_Client->Lock);

    _Client->ModuleId.Length = sizeof(_Client->ModuleId);
    _Client->ModuleId.Type = MIT_GUID;
    XdpGuidCreate(&_Client->ModuleId.Guid);

    _Client->Context = _ClientContext;
    _Client->Attach = _ClientAttach;
    _Client->Detach = _ClientDetach;
    _Client->XdpApiClientDispatch = _XdpApiClientDispatch;

    _NpiCharacteristics = &_Client->NpiClientCharacteristics;
    _NpiCharacteristics->Length = sizeof(*_NpiCharacteristics);
    _NpiCharacteristics->ClientAttachProvider = XdpNmrClientAttachProvider;
    _NpiCharacteristics->ClientDetachProvider = XdpNmrClientDetachProvider;

    _NpiInstance = &_NpiCharacteristics->ClientRegistrationInstance;
    _NpiInstance->Size = sizeof(*_NpiInstance);
    _NpiInstance->Version = 0;
    _NpiInstance->NpiId = &NPI_XDPAPI_INTERFACE_ID;
    _NpiInstance->ModuleId = &_Client->ModuleId;
    _NpiInstance->Number = XDP_API_VERSION_1;

    _Status =
        NmrRegisterClient(
            &_Client->NpiClientCharacteristics, _Client, &_Client->NmrClientHandle);
    if (!NT_SUCCESS(_Status)) {
        goto Error;
    }

    _Status = STATUS_SUCCESS;

Error:

    if (!NT_SUCCESS(_Status)) {
#pragma push
#pragma warning(suppress:6001)
        XdpCleanupClientRegistration(_Client);
#pragma pop
    }

Exit:

    return _Status;
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpUnloadApi(
    _In_ XDP_API_CLIENT *_Client
    )
{
    XdpCleanupClientRegistration(_Client);
}

inline
_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
XdpGetProviderContexts(
    _In_ XDP_API_CLIENT *_Client,
    _Out_ const XDP_API_PROVIDER_DISPATCH **_XdpApiProviderDispatch,
    _Out_ const XDP_API_PROVIDER_BINDING_CONTEXT **_XdpApiProviderContext
    )
{
    NTSTATUS _Status;

    CxPlatLockAcquire(&_Client->Lock);

    if (_Client->BindingHandle == NULL) {
        _Status = STATUS_DEVICE_NOT_READY;
    } else {
        *_XdpApiProviderDispatch = _Client->XdpApiProviderDispatch;
        *_XdpApiProviderContext = _Client->XdpApiProviderContext;
        _Status = STATUS_SUCCESS;
    }

    CxPlatLockRelease(&_Client->Lock);

    return _Status;
}

#define _POLL_INTERVAL_MS 10

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpOpenApi(
    _In_ UINT32 _XdpApiVersion,
    _In_opt_ VOID *_ClientContext,
    _In_opt_ XDP_API_ATTACH_FN *_ClientAttach,
    _In_opt_ XDP_API_DETACH_FN *_ClientDetach,
    _In_ const XDP_API_CLIENT_DISPATCH *_XdpApiClientDispatch,
    _In_ const INT64 _TimeoutMs,
    _Out_ XDP_API_CLIENT *_ApiContext,
    _Out_ const XDP_API_PROVIDER_DISPATCH **_XdpApiProviderDispatch,
    _Out_ const XDP_API_PROVIDER_BINDING_CONTEXT **_XdpApiProviderContext
    )
{
    NTSTATUS Status;
    KEVENT Event;
    INT64 TimeoutMs = INT64_MAX; // forever
    if (_TimeoutMs > 0) {
        TimeoutMs = _TimeoutMs;
    }

    Status = XdpRegister(_XdpApiVersion, _ClientContext, _ClientAttach, _ClientDetach, _XdpApiClientDispatch, _ApiContext);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    do {
        LARGE_INTEGER Timeout100Ns;

        Status =
            XdpGetProviderContexts(
                _ApiContext,
                _XdpApiProviderDispatch,
                _XdpApiProviderContext);
        if (NT_SUCCESS(Status)) {
            break;
        }

        Timeout100Ns.QuadPart = -1 * Int32x32To64(_POLL_INTERVAL_MS, 10000);
        KeResetEvent(&Event);
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout100Ns);
        TimeoutMs = TimeoutMs - _POLL_INTERVAL_MS;
    } while (TimeoutMs > 0);

    if (!NT_SUCCESS(Status)) {
        XdpUnloadApi(_ApiContext);
    }

    return Status;
}

#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
