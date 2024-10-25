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

#define XDPHLP_API_TABLE XdpApi->XdpApiProviderDispatch
#define API_ABSTRACTION(FunctionCall, ...)  \
        XDPHLP_API_TABLE->FunctionCall((XDP_API_CLIENT *)XdpApi, __VA_ARGS__)

inline
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpHlpOpenApi(
    _In_ UINT32 _XdpApiVersion,
    _In_opt_ VOID *_ClientContext,
    _In_opt_ XDP_API_ATTACH_FN *_ClientAttach,
    _In_opt_ XDP_API_DETACH_FN *_ClientDetach,
    _In_ const XDP_API_CLIENT_DISPATCH *_XdpApiClientDispatch,
    _In_ const INT64 *_TimeoutMs,
    _Out_ XDP_API_CLIENT *XdpApi
    )
{
    static const XDP_API_PROVIDER_DISPATCH *_XdpApiDispatch; // dummy
    static const XDP_API_PROVIDER_BINDING_CONTEXT *_ProviderBindingContext; // dummy
    return
        XdpOpenApi(
            _XdpApiVersion,
            _ClientContext,
            _ClientAttach,
            _ClientDetach,
            _XdpApiClientDispatch,
            _TimeoutMs,
            (XDP_API_CLIENT *)XdpApi,
            (XDP_API_PROVIDER_DISPATCH **)&_XdpApiDispatch,
            (XDP_API_PROVIDER_BINDING_CONTEXT **)&_ProviderBindingContext);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpHlpCloseApi(
    _In_ const XDP_API_CLIENT *XdpApi
    )
{
    XdpUnloadApi((XDP_API_CLIENT *)XdpApi);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID*
XdpHlpGetRoutine(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_z_ const CHAR* RoutineName
    )
{
    if (!ExAcquireRundownProtection(XdpApi->RundownRef)) {
        return NULL;
    }
    VOID* Routine = XDPHLP_API_TABLE->XdpGetRoutine(RoutineName);
    ExReleaseRundownProtection(XdpApi->RundownRef);
    return Routine;
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskNotifyAsync2(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_opt_ XSK_COMPLETION_CONTEXT CompletionContext,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return XDPHLP_API_TABLE->XskNotifyAsync2(Socket, Flags, CompletionContext, Result);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpHlpCloseHandle(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Handle
    )
{
    XDPHLP_API_TABLE->XdpCloseHandle(Handle, (XDP_API_CLIENT *)XdpApi);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskCreate(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ HANDLE *Socket
    )
{
    return
        API_ABSTRACTION(
            XskCreate,
            OwningProcess,
            OwningThread,
            SecurityDescriptor,
            Socket);
}

#else

#define XDP_API_CLIENT XDP_API_TABLE
#define XDPHLP_API_TABLE XdpApi
#define API_ABSTRACTION(FunctionCall, ...) XDPHLP_API_TABLE->FunctionCall(__VA_ARGS__)

inline
HRESULT
XdpHlpOpenApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ const XDP_API_TABLE **XdpApi
    )
{
    return XdpOpenApi(XdpApiVersion, XdpApi);
}

inline
VOID
XdpHlpCloseApi(
    _In_ const XDP_API_TABLE *XdpApi
    )
{
    XdpCloseApi(XdpApi);
}

inline
VOID*
XdpHlpGetRoutine(
    _In_ const XDP_API_TABLE *XdpApi,
    _In_z_ const CHAR* RoutineName
    )
{
    return XDPHLP_API_TABLE->XdpGetRoutine(RoutineName);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskCreate(
    _In_ const XDP_API_TABLE *XdpApi,
    _Out_ HANDLE *Socket
    )
{
    return XDPHLP_API_TABLE->XskCreate(Socket);
}

#endif // defined(_KERNEL_MODE)

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpCreateProgram(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    return
        API_ABSTRACTION(
            XdpCreateProgram,
            InterfaceIndex,
            HookId,
            QueueId,
            Flags,
            Rules,
            RuleCount,
            Program);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpInterfaceOpen(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    return
        API_ABSTRACTION(
            XdpInterfaceOpen,
            InterfaceIndex,
            InterfaceHandle);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskBind(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    )
{
    return XDPHLP_API_TABLE->XskBind(Socket, IfIndex, QueueId, Flags);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskActivate(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    return XDPHLP_API_TABLE->XskActivate(Socket, Flags);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskNotifySocket(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return XDPHLP_API_TABLE->XskNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskSetSockopt(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    return XDPHLP_API_TABLE->XskSetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskGetSockopt(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    return XDPHLP_API_TABLE->XskGetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

inline
_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
XdpHlpXskIoctl(
    _In_ const XDP_API_CLIENT *XdpApi,
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    )
{
    return
        XDPHLP_API_TABLE->XskIoctl(
            Socket,
            OptionName,
            InputValue,
            InputLength,
            OutputValue,
            OutputLength);
}

#ifdef __cplusplus
}
#endif
