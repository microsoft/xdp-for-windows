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

typedef struct _XDP_API_ROUTINE {
    _Null_terminated_ const CHAR *RoutineName;
    VOID *Routine;
} XDP_API_ROUTINE;

static XDPAPI_PROVIDER XdpApiProvider;
static const NPI_MODULEID NPI_XDP_MODULEID = {
    sizeof(NPI_MODULEID),
    MIT_GUID,
    { 0x521421C2, 0x539B, 0x46DF, { 0xA1, 0x43, 0x8D, 0xAC, 0x88, 0xD0, 0xC1, 0x55 } }
};

//
// API routines.
//
static XDP_GET_ROUTINE_FN XdpApiKernelXdpGetRoutine;
static XDP_CREATE_PROGRAM_FN XdpApiKernelXdpCreateProgram;
static XDP_DELETE_PROGRAM_FN XdpApiKernelXdpDeleteProgram;
static XDP_INTERFACE_OPEN_FN XdpApiKernelXdpInterfaceOpen;
static XDP_INTERFACE_CLOSE_FN XdpApiKernelXdpInterfaceClose;
static XSK_CREATE_FN XdpApiKernelXskCreate;
static XSK_DELETE_FN XdpApiKernelXskDelete;
static XSK_BIND_FN XdpApiKernelXskBind;
static XSK_ACTIVATE_FN XdpApiKernelXskActivate;
static XSK_NOTIFY_SOCKET_FN XdpApiKernelXskNotifySocket;
static XSK_NOTIFY_ASYNC2_FN XdpApiKernelXskNotifyAsync2;
static XSK_SET_SOCKOPT_FN XdpApiKernelXskSetSockopt;
static XSK_GET_SOCKOPT_FN XdpApiKernelXskGetSockopt;
static XSK_IOCTL_FN XdpApiKernelXskIoctl;

//
// Experimental APIs, subject to removal in a minor release.
//
static XDP_RSS_GET_CAPABILITIES_FN XdpApiKernelXdpRssGetCapabilities;
static XDP_RSS_SET_FN XdpApiKernelXdpRssSet;
static XDP_RSS_GET_FN XdpApiKernelXdpRssGet;
static XDP_QEO_SET_FN XdpApiKernelXdpQeoSet;

#define DECLARE_XDP_API_ROUTINE(_routine) #_routine, (VOID *)XdpApiKernel##_routine
#define DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(_routine, _name) _name, (VOID *)XdpApiKernel##_routine

static const XDP_API_ROUTINE XdpApiRoutines[] = {
    { DECLARE_XDP_API_ROUTINE(XdpGetRoutine) },
    { DECLARE_XDP_API_ROUTINE(XdpCreateProgram) },
    { DECLARE_XDP_API_ROUTINE(XdpDeleteProgram) },
    { DECLARE_XDP_API_ROUTINE(XdpInterfaceOpen) },
    { DECLARE_XDP_API_ROUTINE(XdpInterfaceClose) },
    { DECLARE_XDP_API_ROUTINE(XskCreate) },
    { DECLARE_XDP_API_ROUTINE(XskDelete) },
    { DECLARE_XDP_API_ROUTINE(XskBind) },
    { DECLARE_XDP_API_ROUTINE(XskActivate) },
    { DECLARE_XDP_API_ROUTINE(XskNotifySocket) },
    { DECLARE_XDP_API_ROUTINE(XskNotifyAsync2) },
    { DECLARE_XDP_API_ROUTINE(XskSetSockopt) },
    { DECLARE_XDP_API_ROUTINE(XskGetSockopt) },
    { DECLARE_XDP_API_ROUTINE(XskIoctl) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssGetCapabilities, XDP_RSS_GET_CAPABILITIES_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssSet, XDP_RSS_SET_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssGet, XDP_RSS_GET_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpQeoSet, XDP_QEO_SET_FN_NAME) },
};

static const XDP_API_PROVIDER_DISPATCH XdpApiProviderDispatchV1 = {
    .XdpGetRoutine = XdpApiKernelXdpGetRoutine,
    .XdpCreateProgram = XdpApiKernelXdpCreateProgram,
    .XdpDeleteProgram = XdpApiKernelXdpDeleteProgram,
    .XdpInterfaceOpen = XdpApiKernelXdpInterfaceOpen,
    .XdpInterfaceClose = XdpApiKernelXdpInterfaceClose,
    .XskCreate = XdpApiKernelXskCreate,
    .XskDelete = XdpApiKernelXskDelete,
    .XskBind = XdpApiKernelXskBind,
    .XskActivate = XdpApiKernelXskActivate,
    .XskNotifySocket = XdpApiKernelXskNotifySocket,
    .XskNotifyAsync2 = XdpApiKernelXskNotifyAsync2,
    .XskSetSockopt = XdpApiKernelXskSetSockopt,
    .XskGetSockopt = XdpApiKernelXskGetSockopt,
    .XskIoctl = XdpApiKernelXskIoctl
};

static
XDP_STATUS
XdpApiKernelXdpCreateProgram(
    _In_ XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext,
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_PROGRAM_OPEN ProgramOpen;

    UNREFERENCED_PARAMETER(ProviderBindingContext);

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
    _In_ XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext,
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_INTERFACE_OPEN InterfaceOpen;

    UNREFERENCED_PARAMETER(ProviderBindingContext);

    InterfaceOpen.IfIndex = InterfaceIndex;

    return XdpInterfaceCreate((XDP_INTERFACE_OBJECT **)InterfaceHandle, &InterfaceOpen);
}

static
VOID
XdpApiKernelXdpInterfaceClose(
    _In_ HANDLE InterfaceHandle
    )
{
    XdpInterfaceDelete((XDP_INTERFACE_OBJECT *)InterfaceHandle);
}

static
XDP_STATUS
XdpApiKernelXskCreate(
    _In_ XDP_API_PROVIDER_BINDING_CONTEXT *ProviderBindingContext,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ HANDLE *Socket
    )
{
    XDPAPI_CLIENT *Client = ProviderBindingContext;

    return
        XskCreate(
            (XSK **)Socket, Client->ClientDispatch->XskNotifyCallback,
            OwningProcess, OwningThread, SecurityDescriptor);
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

    Status = XskNotify((XSK *)Socket, &Notify, sizeof(Notify), &Information, NULL, KernelMode, FALSE);
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
    NTSTATUS Status;
    ULONG_PTR Information;
    XSK_NOTIFY_IN Notify = {0};

    Notify.Flags = Flags;
    Notify.CompletionContext = CompletionContext;

    Status = XskNotify((XSK *)Socket, &Notify, sizeof(Notify), &Information, NULL, KernelMode, TRUE);
    *Result = (XSK_NOTIFY_RESULT_FLAGS)Information;

    return Status;
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
    for (UINT32 i = 0; i < RTL_NUMBER_OF(XdpApiRoutines); i++) {
        if (strcmp(XdpApiRoutines[i].RoutineName, RoutineName) == 0) {
            return XdpApiRoutines[i].Routine;
        }
    }

    return NULL;
}

static
XDP_STATUS
XdpApiKernelXdpRssGetCapabilities(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssCapabilitiesSize) XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    )
{
    ASSERT(!((RssCapabilities != NULL) ^ (*RssCapabilitiesSize > 0)));
    return XdpInterfaceOffloadRssGetCapabilities((XDP_INTERFACE_OBJECT *)InterfaceHandle, RssCapabilities, *RssCapabilitiesSize, RssCapabilitiesSize);
}

static
XDP_STATUS
XdpApiKernelXdpRssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ const XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    return XdpInterfaceOffloadRssSet((XDP_INTERFACE_OBJECT *)InterfaceHandle, RssConfiguration, RssConfigurationSize);
}

static
XDP_STATUS
XdpApiKernelXdpRssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssConfigurationSize) XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    ASSERT(!((RssConfiguration != NULL) ^ (*RssConfigurationSize > 0)));
    return XdpInterfaceOffloadRssGet((XDP_INTERFACE_OBJECT *)InterfaceHandle, RssConfiguration, *RssConfigurationSize, RssConfigurationSize);
}

static
XDP_STATUS
XdpApiKernelXdpQeoSet(
    _In_ HANDLE InterfaceHandle,
    _Inout_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    )
{
    UINT32 OutputBufferLength = QuicConnectionsSize;

    return XdpInterfaceOffloadQeoSet((XDP_INTERFACE_OBJECT *)InterfaceHandle, QuicConnections, QuicConnectionsSize, &OutputBufferLength);
}

static
NTSTATUS
XdpApiKernelProviderAttachClient(
    _In_ HANDLE NmrBindingHandle,
    _In_ const VOID *ProviderContext,
    _In_ const NPI_REGISTRATION_INSTANCE *ClientRegistrationInstance,
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
        *ProviderDispatch = &XdpApiProviderDispatchV1;
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
    DWORD EnableKmXdpApi;

    TraceEnter(TRACE_CORE, "-");

    //
    // Kernel-mode XDPAPI is disabled by default while considered experimental.
    //

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"EnableKmXdpApi", &EnableKmXdpApi);
    if (NT_SUCCESS(Status) && EnableKmXdpApi) {
        NPI_PROVIDER_CHARACTERISTICS *Characteristics;

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
    }

    Status = STATUS_SUCCESS;

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