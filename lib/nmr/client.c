//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

typedef struct _XDP_CLIENT {
    NPI_CLIENT_CHARACTERISTICS NpiClientCharacteristics;
    NPI_MODULEID ModuleId;
    HANDLE NmrClientHandle;
    CONST XDP_CAPABILITIES *Capabilities;
    XDP_NPI_CLIENT NpiClient;

    ERESOURCE Resource;
    HANDLE BindingHandle;
} XDP_CLIENT;

static
VOID
XdpCleanupInterfaceRegistration(
    _In_ XDP_CLIENT *Client,
    _In_ BOOLEAN ResourceInitialized
    )
{
    NTSTATUS Status;

    if (Client->NmrClientHandle != NULL) {
        Status = NmrDeregisterClient(Client->NmrClientHandle);
        FRE_ASSERT(Status == STATUS_PENDING);

        Status = NmrWaitForClientDeregisterComplete(Client->NmrClientHandle);
        FRE_ASSERT(Status == STATUS_SUCCESS);
    }

    if (ResourceInitialized) {
        NT_VERIFY(NT_SUCCESS(ExDeleteResourceLite(&Client->Resource)));
    }

    ExFreePoolWithTag(Client, POOLTAG_NMR_CLIENT);
}

static
NTSTATUS
XdpNmrClientAttachProvider(
    _In_ HANDLE NmrBindingHandle,
    _In_ VOID *ClientContext,
    _In_ CONST NPI_REGISTRATION_INSTANCE *ProviderRegistrationInstance
    )
{
    XDP_CLIENT *Client = ClientContext;
    NTSTATUS Status;
    VOID *ProviderContext;
    CONST VOID *ProviderDispatch;

    UNREFERENCED_PARAMETER(ProviderRegistrationInstance);

    //
    // The NMR client allows at most one active binding at a time, but defers
    // all remaining binding logic to the NMR provider.
    //

    ExEnterCriticalRegionAndAcquireResourceExclusive(&Client->Resource);

    if (Client->BindingHandle != NULL) {
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        Status =
            NmrClientAttachProvider(
                NmrBindingHandle, Client, &Client->NpiClient, &ProviderContext, &ProviderDispatch);

        if (NT_SUCCESS(Status)) {
            Client->BindingHandle = NmrBindingHandle;
        }
    }

    ExReleaseResourceAndLeaveCriticalRegion(&Client->Resource);

    return Status;
}

static
NTSTATUS
XdpNmrClientDetachProvider(
    _In_ VOID *ClientBindingContext
    )
{
    XDP_CLIENT *Client = ClientBindingContext;

    ExEnterCriticalRegionAndAcquireResourceExclusive(&Client->Resource);

    Client->BindingHandle = NULL;

    ExReleaseResourceAndLeaveCriticalRegion(&Client->Resource);

    return STATUS_SUCCESS;
}

VOID
XdpDeregisterInterface(
    _In_ XDP_REGISTRATION_HANDLE RegistrationHandle
    )
{
    XDP_CLIENT *Client = (XDP_CLIENT *)RegistrationHandle;

    XdpCleanupInterfaceRegistration(Client, TRUE);
}

NTSTATUS
XdpRegisterInterface(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_CAPABILITIES *Capabilities,
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    )
{
    NTSTATUS Status;
    XDP_CLIENT *Client;
    NPI_CLIENT_CHARACTERISTICS *NpiCharacteristics;
    NPI_REGISTRATION_INSTANCE *NpiInstance;
    BOOLEAN ResourceInitialized = FALSE;

    Client = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Client), POOLTAG_NMR_CLIENT);
    if (Client == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Use resources instead of push locks for downlevel compatibility.
    //
    Status = ExInitializeResourceLite(&Client->Resource);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }
    ResourceInitialized = TRUE;

    Client->Capabilities = Capabilities;
    Client->NpiClient.InterfaceContext = InterfaceContext;
    Client->NpiClient.InterfaceDispatch = InterfaceDispatch;

    Client->ModuleId.Length = sizeof(Client->ModuleId);
    Client->ModuleId.Type = MIT_GUID;
    Client->ModuleId.Guid = Capabilities->InstanceId;

    NpiCharacteristics = &Client->NpiClientCharacteristics;
    NpiCharacteristics->Length = sizeof(*NpiCharacteristics);
    NpiCharacteristics->ClientAttachProvider = XdpNmrClientAttachProvider;
    NpiCharacteristics->ClientDetachProvider = XdpNmrClientDetachProvider;

    NpiInstance = &NpiCharacteristics->ClientRegistrationInstance;
    NpiInstance->Size = sizeof(*NpiInstance);
    NpiInstance->Version = XDP_BINDING_VERSION_1;
    NpiInstance->NpiId = &NPI_XDP_ID;
    NpiInstance->ModuleId = &Client->ModuleId;
    NpiInstance->Number = InterfaceIndex;
    NpiInstance->NpiSpecificCharacteristics = Client->Capabilities;

    Status =
        NmrRegisterClient(
            &Client->NpiClientCharacteristics, Client, &Client->NmrClientHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *RegistrationHandle = (XDP_REGISTRATION_HANDLE)Client;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Client != NULL) {
            XdpCleanupInterfaceRegistration(Client, ResourceInitialized);
        }
    }

    return Status;
}
