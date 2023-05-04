//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "ebpfextension.tmh"

typedef struct _EBPF_EXTENSION_PROVIDER {
    NPI_PROVIDER_CHARACTERISTICS Characteristics;
    HANDLE NmrProviderHandle;
    EBPF_EXTENSION_ON_CLIENT_ATTACH *AttachCallback;
    EBPF_EXTENSION_ON_CLIENT_DETACH *DetachCallback;
    const VOID *CustomData;
} EBPF_EXTENSION_PROVIDER;

typedef struct _EBPF_EXTENSION_CLIENT {
    HANDLE NmrBindingHandle;
    GUID ClientModuleId;
    const ebpf_extension_dispatch_table_t *ClientDispatch;
    const VOID *ClientBindingContext;
    const ebpf_extension_data_t *ClientData;
    VOID *ProviderData;
    EBPF_EXTENSION_PROVIDER *ProviderContext;
} EBPF_EXTENSION_CLIENT;

VOID
EbpfExtensionClientSetProviderData(
    _In_ const EBPF_EXTENSION_CLIENT *Client,
    _In_opt_ const VOID *ProviderData
    )
{
    ((EBPF_EXTENSION_CLIENT *)Client)->ProviderData = (VOID *)ProviderData;
}

VOID *
EbpfExtensionClientGetProviderData(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    )
{
    return Client->ProviderData;
}

const ebpf_extension_dispatch_table_t *
EbpfExtensionClientGetDispatch(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    )
{
    return Client->ClientDispatch;
}

const ebpf_extension_program_dispatch_table_t *
EbpfExtensionClientGetProgramDispatch(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    )
{
    return (ebpf_extension_program_dispatch_table_t *)Client->ClientDispatch;
}

const VOID *
EbpfExtensionClientGetClientContext(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    )
{
    return Client->ClientBindingContext;
}

const ebpf_extension_data_t *
EbpfExtensionClientGetClientData(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    )
{
    return Client->ClientData;
}

VOID
EbpfExtensionDetachClientCompletion(
    _In_ EBPF_EXTENSION_CLIENT *Client
    )
{
    TraceEnter(TRACE_CORE, "Client=%p", Client);

    ASSERT(Client != NULL);
    _Analysis_assume_(Client != NULL);

    NmrProviderDetachClientComplete(Client->NmrBindingHandle);

    TraceExitSuccess(TRACE_CORE);
}

static
NTSTATUS
EbpfExtensionProviderAttachClient(
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
    EBPF_EXTENSION_PROVIDER *Provider = (EBPF_EXTENSION_PROVIDER *)ProviderContext;
    EBPF_EXTENSION_CLIENT *Client = NULL;
    const ebpf_extension_dispatch_table_t *ClientDispatch = ClientNpiDispatch;

    TraceEnter(TRACE_CORE, "ProviderContext=%p", ProviderContext);

    if ((ProviderBindingContext == NULL) || (ProviderDispatch == NULL) || (Provider == NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Client = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Client), XDP_POOLTAG_EBPF_NMR);
    if (Client == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Client->NmrBindingHandle = NmrBindingHandle;
    Client->ClientModuleId = ClientRegistrationInstance->ModuleId->Guid;
    Client->ClientBindingContext = ClientBindingContext;
    Client->ClientData = ClientRegistrationInstance->NpiSpecificCharacteristics;

    Client->ClientDispatch = ClientDispatch;
    Client->ProviderContext = Provider;

    if (Provider->AttachCallback != NULL) {
        Status = Provider->AttachCallback(Client, Provider);
    }

    if (NT_SUCCESS(Status)) {
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_NOINTERFACE;
    }

Exit:

    if (NT_SUCCESS(Status)) {
        *ProviderBindingContext = Client;
        Client = NULL;
        *ProviderDispatch = NULL;
    } else {
        if (Client != NULL) {
            ExFreePoolWithTag(Client, XDP_POOLTAG_EBPF_NMR);
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
EbpfExtensionProviderDetachClient(
    _In_ const VOID *ProviderBindingContext
    )
{
    EBPF_EXTENSION_CLIENT *Client = (EBPF_EXTENSION_CLIENT *)ProviderBindingContext;
    EBPF_EXTENSION_PROVIDER *Provider = Client->ProviderContext;
    NTSTATUS Status = STATUS_SUCCESS;

    TraceEnter(TRACE_CORE, "ProviderBindingContext=%p", ProviderBindingContext);

    if (!NT_VERIFY(Client != NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Provider->DetachCallback != NULL) {
        Status = Provider->DetachCallback(Client);
        ASSERT(NT_SUCCESS(Status));
    }

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

static
VOID
EbpfExtensionProviderCleanup(
    _Frees_ptr_ VOID *ProviderBindingContext
    )
{
    TraceEnter(TRACE_CORE, "ProviderBindingContext=%p", ProviderBindingContext);

    ExFreePoolWithTag(ProviderBindingContext, XDP_POOLTAG_EBPF_NMR);

    TraceExitSuccess(TRACE_CORE);
}

VOID
EbpfExtensionProviderUnregister(
    _Frees_ptr_opt_ EBPF_EXTENSION_PROVIDER *ProviderContext
    )
{
    TraceEnter(TRACE_CORE, "ProviderContext=%p", ProviderContext);

    if (ProviderContext != NULL) {
        NTSTATUS Status = NmrDeregisterProvider(ProviderContext->NmrProviderHandle);

        if (Status == STATUS_PENDING) {
            NmrWaitForProviderDeregisterComplete(ProviderContext->NmrProviderHandle);
        }

        ExFreePoolWithTag(ProviderContext, XDP_POOLTAG_EBPF_NMR);
    }

    TraceExitSuccess(TRACE_CORE);
}

NTSTATUS
EbpfExtensionProviderRegister(
    _In_ const NPIID *NpiId,
    _In_ const EBPF_EXTENSION_PROVIDER_PARAMETERS *Parameters,
    _In_opt_ EBPF_EXTENSION_ON_CLIENT_ATTACH *AttachCallback,
    _In_opt_ EBPF_EXTENSION_ON_CLIENT_DETACH *DetachCallback,
    _In_opt_ const VOID *CustomData,
    _Outptr_ EBPF_EXTENSION_PROVIDER **ProviderContext
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    EBPF_EXTENSION_PROVIDER *Provider = NULL;
    NPI_PROVIDER_CHARACTERISTICS *Characteristics;

    TraceEnter(TRACE_CORE, "Parameters=%p", Parameters);

    Provider = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Provider), XDP_POOLTAG_EBPF_NMR);
    if (Provider == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Characteristics = &Provider->Characteristics;
    Characteristics->Length = sizeof(NPI_PROVIDER_CHARACTERISTICS);
    Characteristics->ProviderAttachClient = EbpfExtensionProviderAttachClient;
    Characteristics->ProviderDetachClient = EbpfExtensionProviderDetachClient;
    Characteristics->ProviderCleanupBindingContext = EbpfExtensionProviderCleanup;
    Characteristics->ProviderRegistrationInstance.Size = sizeof(NPI_REGISTRATION_INSTANCE);
    Characteristics->ProviderRegistrationInstance.NpiId = NpiId;
    Characteristics->ProviderRegistrationInstance.NpiSpecificCharacteristics =
        Parameters->ProviderData;
    Characteristics->ProviderRegistrationInstance.ModuleId = Parameters->ProviderModuleId;

    Provider->AttachCallback = AttachCallback;
    Provider->DetachCallback = DetachCallback;
    Provider->CustomData = CustomData;

    Status = NmrRegisterProvider(Characteristics, Provider, &Provider->NmrProviderHandle);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CORE, "NmrRegisterProvider failed Status=%!STATUS!", Status);
        goto Exit;
    }

    *ProviderContext = Provider;
    Provider = NULL;

Exit:
    if (!NT_SUCCESS(Status)) {
        EbpfExtensionProviderUnregister(Provider);
    }

    if (NT_SUCCESS(Status)) {
        TraceVerbose(
            TRACE_CORE, "ModuleId=%!GUID! ProviderContext=%p",
            &Parameters->ProviderModuleId->Guid, *ProviderContext);
    }

    TraceExitStatus(TRACE_CORE);
    return Status;
}
