//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "apikernel.tmh"

typedef struct _XDPAPI_PROVIDER {
    NPI_PROVIDER_CHARACTERISTICS Characteristics;
    HANDLE NmrProviderHandle;
} XDPAPI_PROVIDER;

typedef struct _XDPAPI_CLIENT {
    HANDLE NmrBindingHandle;
    GUID ClientModuleId;
    const XDP_API_CLIENT_DISPATCH *ClientDispatch;
    const VOID *ClientBindingContext;
} XDPAPI_CLIENT;

static XDPAPI_PROVIDER XdpApiProvider;
static CONST NPI_MODULEID NPI_XDP_MODULEID = {
    sizeof(NPI_MODULEID),
    MIT_GUID,
    { 0x521421C2, 0x539B, 0x46DF, { 0xA1, 0x43, 0x8D, 0xAC, 0x88, 0xD0, 0xC1, 0x55 } }
};

static
XDP_STATUS
XdpApiKernelXdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) CONST XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_PROGRAM_OPEN ProgramOpen;

    ProgramOpen.IfIndex = InterfaceIndex;
    ProgramOpen.HookId = *HookId;
    ProgramOpen.QueueId = QueueId;
    ProgramOpen.Flags = Flags;
    ProgramOpen.RuleCount = RuleCount;
    ProgramOpen.Rules = Rules;

    return XdpProgramCreate((XDP_PROGRAM_OBJECT **)Program, &ProgramOpen, KernelMode);
}

static
VOID
XdpApiKernelXdpDeleteProgram(
    _In_ HANDLE Program
    )
{
    XdpProgramClose((XDP_PROGRAM_OBJECT *)Program);
}

static
XDP_STATUS
XdpApiKernelXdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    UNREFERENCED_PARAMETER(InterfaceIndex);
    UNREFERENCED_PARAMETER(InterfaceHandle);
    return STATUS_NOT_SUPPORTED;
}

static
XDP_STATUS
XdpApiKernelXskCreate(
    _Out_ HANDLE* Socket
    )
{
    return XskCreate((XSK **)Socket);
}

static
VOID
XdpApiKernelXskDelete(
    _In_ HANDLE Socket
    )
{
    XskCleanup((XSK *)Socket);
    XskClose((XSK *)Socket);
}

static
XDP_STATUS
XdpApiKernelXskBind(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    )
{
    XSK_BIND_IN Bind = {0};

    Bind.IfIndex = IfIndex;
    Bind.QueueId = QueueId;
    Bind.Flags = Flags;

    return XskBindSocket((XSK *)Socket, Bind);
}

static
XDP_STATUS
XdpApiKernelXskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    )
{
    XSK_ACTIVATE_IN Activate = {0};

    Activate.Flags = Flags;

    return XskActivateSocket((XSK *)Socket, Activate);
}

static
XDP_STATUS
XdpApiKernelXskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    NTSTATUS Status;
    ULONG_PTR Information;
    XSK_NOTIFY_IN Notify = {0};

    Notify.Flags = Flags;
    Notify.WaitTimeoutMilliseconds = WaitTimeoutMilliseconds;

    Status = XskNotify((XSK *)Socket, &Notify, sizeof(Notify), &Information, NULL, KernelMode);
    *Result = (XSK_NOTIFY_RESULT_FLAGS)Information;

    return Status;
}

static
XDP_STATUS
XdpApiKernelXskNotifyAsync2(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ XSK_COMPLETION_CONTEXT CompletionContext,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Result);
    return STATUS_NOT_SUPPORTED;
}

static
XDP_STATUS
XdpApiKernelXskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    XSK_SET_SOCKOPT_IN Sockopt = {0};

    Sockopt.Option = OptionName;
    Sockopt.InputBuffer = OptionValue;
    Sockopt.InputBufferLength = OptionLength;

    return XskSetSockopt((XSK *)Socket, &Sockopt, KernelMode);
}

static
XDP_STATUS
XdpApiKernelXskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    return XskGetSockopt((XSK *)Socket, OptionName, OptionValue, OptionLength);
}

static
XDP_STATUS
XdpApiKernelXskIoctl(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    )
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(OptionName);
    UNREFERENCED_PARAMETER(InputValue);
    UNREFERENCED_PARAMETER(InputLength);
    UNREFERENCED_PARAMETER(OutputValue);
    UNREFERENCED_PARAMETER(OutputLength);
    return STATUS_NOT_SUPPORTED;
}

static
VOID *
XdpApiKernelXdpGetRoutine(
    _In_z_ const CHAR *RoutineName
    )
{
    UNREFERENCED_PARAMETER(RoutineName);
    return NULL;
}

static CONST XDP_API_PROVIDER_DISPATCH XdpApiProviderDispatch = {
    XdpApiKernelXdpGetRoutine,
    XdpApiKernelXdpCreateProgram,
    XdpApiKernelXdpDeleteProgram,
    XdpApiKernelXdpInterfaceOpen,
    XdpApiKernelXskCreate,
    XdpApiKernelXskDelete,
    XdpApiKernelXskBind,
    XdpApiKernelXskActivate,
    XdpApiKernelXskNotifySocket,
    XdpApiKernelXskNotifyAsync2,
    XdpApiKernelXskSetSockopt,
    XdpApiKernelXskGetSockopt,
    XdpApiKernelXskIoctl
};

static
NTSTATUS
XdpApiKernelProviderAttachClient(
    _In_ HANDLE NmrBindingHandle,
    _In_ const VOID *ProviderContext,
    _In_ const NPI_REGISTRATION_INSTANCE* ClientRegistrationInstance,
    _In_ const VOID *ClientBindingContext,
    _In_ const VOID *ClientNpiDispatch,
    _Outptr_ VOID **ProviderBindingContext,
    _Outptr_result_maybenull_ const VOID **ProviderDispatch
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    XDPAPI_PROVIDER *Provider = (XDPAPI_PROVIDER *)ProviderContext;
    XDPAPI_CLIENT *Client = NULL;
    const XDP_API_CLIENT_DISPATCH *ClientDispatch = ClientNpiDispatch;

    TraceEnter(TRACE_CORE, "ProviderContext=%p", ProviderContext);

    if ((ProviderBindingContext == NULL) || (ProviderDispatch == NULL) || (Provider == NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (ClientRegistrationInstance->Number != XDP_API_VERSION_1) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    Client = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Client), XDP_POOLTAG_KERNEL_API_NMR);
    if (Client == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Client->NmrBindingHandle = NmrBindingHandle;
    Client->ClientModuleId = ClientRegistrationInstance->ModuleId->Guid;
    Client->ClientBindingContext = ClientBindingContext;
    Client->ClientDispatch = ClientDispatch;

Exit:

    if (NT_SUCCESS(Status)) {
        *ProviderBindingContext = Client;
        Client = NULL;
        *ProviderDispatch = &XdpApiProviderDispatch;
    } else {
        if (Client != NULL) {
            ExFreePoolWithTag(Client, XDP_POOLTAG_KERNEL_API_NMR);
        }
    }

    TraceVerbose(
        TRACE_CORE, "ProviderContext=%p ProviderBindingContext=%p",
        ProviderContext, NT_SUCCESS(Status) ? *ProviderBindingContext : NULL);
    TraceExitStatus(TRACE_CORE);
    return Status;
}

static
NTSTATUS
XdpApiKernelProviderDetachClient(
    _In_ const VOID *ProviderBindingContext
    )
{
    XDPAPI_CLIENT *Client = (XDPAPI_CLIENT *)ProviderBindingContext;
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(TRACE_CORE, "ProviderBindingContext=%p", ProviderBindingContext);

    if (!NT_VERIFY(Client != NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

static
VOID
XdpApiKernelProviderCleanup(
    _Frees_ptr_ VOID *ProviderBindingContext
    )
{
    TraceEnter(TRACE_CORE, "ProviderBindingContext=%p", ProviderBindingContext);

    ExFreePoolWithTag(ProviderBindingContext, XDP_POOLTAG_KERNEL_API_NMR);

    TraceExitSuccess(TRACE_CORE);
}

NTSTATUS
XdpApiKernelStart(
    VOID
    )
{
    NTSTATUS Status;
    NPI_PROVIDER_CHARACTERISTICS *Characteristics;

    TraceEnter(TRACE_CORE, "-");

    Characteristics = &XdpApiProvider.Characteristics;
    Characteristics->Length = sizeof(NPI_PROVIDER_CHARACTERISTICS);
    Characteristics->ProviderAttachClient = XdpApiKernelProviderAttachClient;
    Characteristics->ProviderDetachClient = XdpApiKernelProviderDetachClient;
    Characteristics->ProviderCleanupBindingContext = XdpApiKernelProviderCleanup;
    Characteristics->ProviderRegistrationInstance.Size = sizeof(NPI_REGISTRATION_INSTANCE);
    Characteristics->ProviderRegistrationInstance.NpiId = &NPI_XDPAPI_INTERFACE_ID;
    Characteristics->ProviderRegistrationInstance.ModuleId = &NPI_XDP_MODULEID;
    Characteristics->ProviderRegistrationInstance.Number = XDP_API_VERSION_1;

    Status = NmrRegisterProvider(Characteristics, &XdpApiProvider, &XdpApiProvider.NmrProviderHandle);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CORE, "NmrRegisterProvider failed Status=%!STATUS!", Status);
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpApiKernelStop(
    VOID
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "-");

    if (XdpApiProvider.NmrProviderHandle != NULL) {
        Status = NmrDeregisterProvider(XdpApiProvider.NmrProviderHandle);
        ASSERT(Status == STATUS_PENDING);

        if (Status == STATUS_PENDING) {
            Status = NmrWaitForProviderDeregisterComplete(XdpApiProvider.NmrProviderHandle);
            ASSERT(Status == STATUS_SUCCESS);
        }

        XdpApiProvider.NmrProviderHandle = NULL;
    }

    TraceExitSuccess(TRACE_CORE);
}
