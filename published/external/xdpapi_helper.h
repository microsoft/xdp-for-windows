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

extern XDP_API_PROVIDER_DISPATCH *XdpApiTest;
extern XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext;

#define XDP_CREATE_HANDLE_WITH_RUNDOWN(FunctionCall, Object, ...) \
    do { \
        XDP_STATUS Status; \
        XDP_API_CLIENT *Client = (XDP_API_CLIENT *)(ProviderBindingContext); \
        if (!ExAcquireRundownProtectionCacheAware(Client->RundownRef)) { \
            return STATUS_DEVICE_NOT_READY; \
        } else { \
            Status = FunctionCall(ProviderBindingContext, __VA_ARGS__); \
            if (NT_SUCCESS(Status)) { \
                ((XDP_FILE_OBJECT_HEADER *)(Object))->RundownRef = Client->RundownRef; \
            } else { \
                ExReleaseRundownProtectionCacheAware(Client->RundownRef); \
            } \
            return Status; \
        } \
    } while (0)

_IRQL_requires_(PASSIVE_LEVEL)
VOID*
CxPlatXdpGet(
    _In_z_ const CHAR* RoutineName
    )
{
    XDP_API_CLIENT *Client = (XDP_API_CLIENT *)(ProviderBindingContext);
    if (!ExAcquireRundownProtectionCacheAware(Client->RundownRef)) {
        return NULL;
    }
    return XdpApiTest->XdpGetRoutine(RoutineName);
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
    return XdpApiTest->XskNotifyAsync2(Socket, Flags, CompletionContext, Result);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
CxPlatXdpCloseHandle(
    _In_ HANDLE Handle
    )
{
    XdpApiTest->XdpCloseHandle(Handle);
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
        XdpApiTest->XskCreate,
        Socket,
        OwningProcess,
        OwningThread,
        SecurityDescriptor,
        Socket);
}

#else

// TODO: change name
extern XDP_API_TABLE *XdpApiTest;

#define XDP_CREATE_HANDLE_WITH_RUNDOWN(FunctionCall, Object, ...) \
    do { \
        return FunctionCall(__VA_ARGS__); \
    } while (0)


VOID*
CxPlatXdpGet(
    _In_z_ const CHAR* RoutineName
    )
{
    return XdpApiTest->XdpGetRoutine(RoutineName);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskCreate(
    _Out_ HANDLE *Socket
    )
{
    return XdpApiTest->XskCreate(Socket);
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
        XdpApiTest->XdpCreateProgram,
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
        XdpApiTest->XdpInterfaceOpen,
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
    return XdpApiTest->XskBind(Socket, IfIndex, QueueId, Flags);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatXskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    return XdpApiTest->XskActivate(Socket, Flags);
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
    return XdpApiTest->XskNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result);
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
    return XdpApiTest->XskSetSockopt(Socket, OptionName, OptionValue, OptionLength);
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
    return XdpApiTest->XskGetSockopt(Socket, OptionName, OptionValue, OptionLength);
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
    return XdpApiTest->XskIoctl(Socket, OptionName, InputValue, InputLength, OutputValue, OutputLength);
}

#ifdef __cplusplus
}
#endif
