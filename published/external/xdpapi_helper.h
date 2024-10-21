//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <xdpapi.h>

#if defined(_KERNEL_MODE)

#include <wdm.h>
#include <xdp/object.h>

static XDP_API_CLIENT _XdpApiContext = {0};
static const XDP_API_PROVIDER_DISPATCH *_XdpApi;
static const XDP_API_PROVIDER_BINDING_CONTEXT *_ProviderBindingContext;

#define XDP_CREATE_HANDLE_WITH_RUNDOWN(FunctionCall, Object, ...)                               \
    do {                                                                                        \
        XDP_STATUS Status;                                                                      \
        if (!ExAcquireRundownProtectionCacheAware(_XdpApiContext.RundownRef)) {                 \
            return STATUS_DEVICE_NOT_READY;                                                     \
        } else {                                                                                \
            XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext =                          \
                (XDP_API_PROVIDER_BINDING_CONTEXT *)_ProviderBindingContext;                    \
            Status = FunctionCall(ProviderBindingContext, __VA_ARGS__);                         \
            if (NT_SUCCESS(Status)) {                                                           \
                ((XDP_FILE_OBJECT_HEADER *)(Object))->RundownRef = _XdpApiContext.RundownRef;   \
            } else {                                                                            \
                ExReleaseRundownProtectionCacheAware(_XdpApiContext.RundownRef);                \
            }                                                                                   \
            return Status;                                                                      \
        }                                                                                       \
    } while (0)

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
CxPlatXdpOpenApi(
    _In_ UINT32 _XdpApiVersion,
    _In_opt_ VOID *_ClientContext,
    _In_opt_ XDP_API_ATTACH_FN *_ClientAttach,
    _In_opt_ XDP_API_DETACH_FN *_ClientDetach,
    _In_ const XDP_API_CLIENT_DISPATCH *_XdpApiClientDispatch,
    _In_ const INT64 _TimeoutMs
    )
{
    NTSTATUS Status;
    const XDP_API_PROVIDER_DISPATCH *ProviderDispatch;
    const XDP_API_PROVIDER_BINDING_CONTEXT *ProviderContext;

    Status =
        XdpOpenApi(
            _XdpApiVersion,
            _ClientContext,
            _ClientAttach,
            _ClientDetach,
            _XdpApiClientDispatch,
            _TimeoutMs,
            &_XdpApiContext,
            &ProviderDispatch,
            &ProviderContext);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    _XdpApi = (XDP_API_PROVIDER_DISPATCH *)ProviderDispatch;
    _ProviderBindingContext = (XDP_API_PROVIDER_BINDING_CONTEXT *)ProviderContext;

    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatXdpCloseApi(
    VOID
    )
{
    XdpUnloadApi(&_XdpApiContext);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID*
CxPlatXdpGetRoutine(
    _In_z_ const CHAR* RoutineName
    )
{
    if (!ExAcquireRundownProtectionCacheAware(_XdpApiContext.RundownRef)) {
        return NULL;
    }
    return _XdpApi->XdpGetRoutine(RoutineName);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskNotifyAsync2(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_opt_ XSK_COMPLETION_CONTEXT CompletionContext,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return _XdpApi->XskNotifyAsync2(Socket, Flags, CompletionContext, Result);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatXdpCloseHandle(
    _In_ HANDLE Handle
    )
{
    _XdpApi->XdpCloseHandle(Handle);
    ExReleaseRundownProtectionCacheAware(((XDP_FILE_OBJECT_HEADER *)Handle)->RundownRef);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskCreate(
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ HANDLE *Socket
    )
{
    XDP_CREATE_HANDLE_WITH_RUNDOWN(
        _XdpApi->XskCreate,
        Socket,
        OwningProcess,
        OwningThread,
        SecurityDescriptor,
        Socket);
}

#else

static XDP_API_TABLE *_XdpApi;

#define XDP_CREATE_HANDLE_WITH_RUNDOWN(FunctionCall, Object, ...)   \
    do {                                                            \
        return FunctionCall(__VA_ARGS__);                           \
    } while (0)


VOID*
CxPlatXdpGet(
    _In_z_ const CHAR* RoutineName
    )
{
    return _XdpApi->XdpGetRoutine(RoutineName);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskCreate(
    _Out_ HANDLE *Socket
    )
{
    return _XdpApi->XskCreate(Socket);
}

#endif // defined(_KERNEL_MODE)

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_CREATE_HANDLE_WITH_RUNDOWN(
        _XdpApi->XdpCreateProgram,
        Program,
        InterfaceIndex,
        HookId,
        QueueId,
        Flags,
        Rules,
        RuleCount,
        Program);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_CREATE_HANDLE_WITH_RUNDOWN(
        _XdpApi->XdpInterfaceOpen,
        InterfaceHandle,
        InterfaceIndex,
        InterfaceHandle);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskBind(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    )
{
    return _XdpApi->XskBind(Socket, IfIndex, QueueId, Flags);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    return _XdpApi->XskActivate(Socket, Flags);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return _XdpApi->XskNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    return _XdpApi->XskSetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    return _XdpApi->XskGetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskIoctl(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    )
{
    return _XdpApi->XskIoctl(Socket, OptionName, InputValue, InputLength, OutputValue, OutputLength);
}

#ifdef __cplusplus
}
#endif
