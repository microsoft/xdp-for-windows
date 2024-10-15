
#pragma once

#include "xdpapi.h"

#if defined(_KERNEL_MODE)

extern XDP_API_PROVIDER_DISPATCH *XdpApiTest;
extern XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext;

#define XDP_CALL_WITH_RETURN(ReturnType, FunctionCall, ...) \
    do { \
        ((XDP_API_CLIENT *)(ProviderBindingContext))->InProgressCallCount++; \
        ReturnType result = XdpApiTest->FunctionCall(__VA_ARGS__); \
        ((XDP_API_CLIENT *)(ProviderBindingContext))->InProgressCallCount--; \
        return result; \
    } while (0)

#define XDP_CALL_VOID(FunctionCall, ...) \
    do { \
        ((XDP_API_CLIENT *)(ProviderBindingContext))->InProgressCallCount++; \
        XdpApiTest->FunctionCall(__VA_ARGS__); \
        ((XDP_API_CLIENT *)(ProviderBindingContext))->InProgressCallCount--; \
    } while (0)

// _IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_CALL_WITH_RETURN(XDP_STATUS, XdpCreateProgram, ProviderBindingContext, InterfaceIndex, HookId, QueueId, Flags, Rules, RuleCount, Program);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatXdpDeleteProgram(
    _In_ HANDLE Program
    )
{
    XDP_CALL_VOID(XdpDeleteProgram, Program);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_CALL_WITH_RETURN(XDP_STATUS, XdpInterfaceOpen, ProviderBindingContext, InterfaceIndex, InterfaceHandle);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatInterfaceClose(
    _In_ HANDLE InterfaceHandle
    )
{
    XDP_CALL_VOID(XdpInterfaceClose, InterfaceHandle);
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
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskCreate, ProviderBindingContext, OwningProcess, OwningThread, SecurityDescriptor, Socket);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatXskDelete(
    _In_ HANDLE Socket
    )
{
    XDP_CALL_VOID(XskDelete, Socket);
}

XDP_STATUS
CxPlatXskNotifyAsync2(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_opt_ XSK_COMPLETION_CONTEXT CompletionContext,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskNotifyAsync2, Socket, Flags, CompletionContext, Result);
}


#else

// TODO: change name
extern XDP_API_TABLE *XdpApiTest;

#define XDP_CALL_WITH_RETURN(ReturnType, FunctionCall, ...) \
    do { \
        return XdpApiTest->FunctionCall(__VA_ARGS__); \
    } while (0)

#define XDP_CALL_VOID(FunctionCall, ...) \
    do { \
        XdpApiTest->FunctionCall(__VA_ARGS__); \
    } while (0)


XDP_STATUS
CxPlatCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    return XdpApiTest->XdpCreateProgram(InterfaceIndex, HookId, QueueId, Flags, Rules, RuleCount, Program);
}

XDP_STATUS
CxPlatXdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    return XdpApiTest->XdpInterfaceOpen(InterfaceIndex, InterfaceHandle);
}

XDP_STATUS
CxPlatXskCreate(
    _Out_ HANDLE *Socket
    )
{
    return XdpApiTest->XskCreate(Socket);
}

#endif // defined(_KERNEL_MODE)

VOID*
CxPlatXdpGet(
    _In_z_ const CHAR* RoutineName
    )
{
    XDP_CALL_WITH_RETURN(VOID*, XdpGetRoutine, RoutineName);
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
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskBind, Socket, IfIndex, QueueId, Flags);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskActivate, Socket, Flags);
}

XDP_STATUS
_IRQL_requires_(PASSIVE_LEVEL)
CxPlatXskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskNotifySocket, Socket, Flags, WaitTimeoutMilliseconds, Result);
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
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskSetSockopt, Socket, OptionName, OptionValue, OptionLength);
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
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskGetSockopt, Socket, OptionName, OptionValue, OptionLength);
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
    XDP_CALL_WITH_RETURN(XDP_STATUS, XskIoctl, Socket, OptionName, InputValue, InputLength, OutputValue, OutputLength);
}