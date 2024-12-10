//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _XDP_CLIENT {
    NPI_CLIENT_CHARACTERISTICS NpiClientCharacteristics;
    NPI_MODULEID ModuleId;
    HANDLE NmrClientHandle;
    const XDP_CAPABILITIES_EX *CapabilitiesEx;
    XDP_NPI_CLIENT NpiClient;

    KSPIN_LOCK SpinLock;
    EX_PUSH_LOCK PushLock;
    HANDLE BindingHandle;
} XDP_CLIENT;

typedef struct _XDP_CLIENT_SINGLE_VERSION {
    XDP_CLIENT Client;
    const XDP_VERSION *ClientVersion;
    VOID *InterfaceContext;
    const XDP_INTERFACE_DISPATCH *InterfaceDispatch;
} XDP_CLIENT_SINGLE_VERSION;

static
VOID
XdpCleanupInterfaceRegistration(
    _In_ XDP_CLIENT *Client
    )
{
    NTSTATUS Status;

    if (Client->NmrClientHandle != NULL) {
        Status = NmrDeregisterClient(Client->NmrClientHandle);
        FRE_ASSERT(Status == STATUS_PENDING);

        Status = NmrWaitForClientDeregisterComplete(Client->NmrClientHandle);
        FRE_ASSERT(Status == STATUS_SUCCESS);
    }
}

static
NTSTATUS
XdpNmrClientAttachProvider(
    _In_ HANDLE NmrBindingHandle,
    _In_ VOID *ClientContext,
    _In_ const NPI_REGISTRATION_INSTANCE *ProviderRegistrationInstance
    )
{
    XDP_CLIENT *Client = ClientContext;
    KIRQL OldIrql;
    NTSTATUS Status;
    VOID *ProviderBindingContext;
    const VOID *ProviderBindingDispatch;

    UNREFERENCED_PARAMETER(ProviderRegistrationInstance);

    //
    // The NMR client allows at most one active binding at a time, but defers
    // all remaining binding logic to the NMR provider.
    //

    RtlAcquirePushLockExclusive(&Client->PushLock);
    KeAcquireSpinLock(&Client->SpinLock, &OldIrql);

    if (Client->BindingHandle != NULL) {
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        KeReleaseSpinLock(&Client->SpinLock, OldIrql);

        Status =
            NmrClientAttachProvider(
                NmrBindingHandle, Client, &Client->NpiClient,
                &ProviderBindingContext, &ProviderBindingDispatch);

        KeAcquireSpinLock(&Client->SpinLock, &OldIrql);

        if (NT_SUCCESS(Status)) {
            ASSERT(Client->BindingHandle == NULL);
            Client->BindingHandle = NmrBindingHandle;
        }
    }

    KeReleaseSpinLock(&Client->SpinLock, OldIrql);
    RtlReleasePushLockExclusive(&Client->PushLock);

    return Status;
}

static
NTSTATUS
XdpNmrClientDetachProvider(
    _In_ VOID *ClientBindingContext
    )
{
    XDP_CLIENT *Client = ClientBindingContext;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Client->SpinLock, &OldIrql);

    ASSERT(Client->BindingHandle != NULL);
    Client->BindingHandle = NULL;

    KeReleaseSpinLock(&Client->SpinLock, OldIrql);

    return STATUS_SUCCESS;
}

static
NTSTATUS
XdpRegisterInterfaceInternal(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_CAPABILITIES_EX *CapabilitiesEx,
    _In_ VOID *GetInterfaceContext,
    _In_ XDP_GET_INTERFACE_DISPATCH *GetInterfaceDispatch,
    _Out_ XDP_CLIENT *Client,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    )
{
    NTSTATUS Status;
    NPI_CLIENT_CHARACTERISTICS *NpiCharacteristics;
    NPI_REGISTRATION_INSTANCE *NpiInstance;

    KeInitializeSpinLock(&Client->SpinLock);
    ExInitializePushLock(&Client->PushLock);

    Client->CapabilitiesEx = CapabilitiesEx;
    Client->NpiClient.Header.Revision = XDP_NPI_CLIENT_REVISION_1;
    Client->NpiClient.Header.Size = XDP_SIZEOF_NPI_CLIENT_REVISION_1;
    Client->NpiClient.GetInterfaceContext = GetInterfaceContext;
    Client->NpiClient.GetInterfaceDispatch = GetInterfaceDispatch;

    Client->ModuleId.Length = sizeof(Client->ModuleId);
    Client->ModuleId.Type = MIT_GUID;
    Client->ModuleId.Guid = CapabilitiesEx->InstanceId;

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
    NpiInstance->NpiSpecificCharacteristics = Client->CapabilitiesEx;

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
            XdpCleanupInterfaceRegistration(Client);
        }
    }

    return Status;
}

static
VOID
XdpDeregisterInterfaceInternal(
    _In_ XDP_CLIENT *Client
    )
{
    XdpCleanupInterfaceRegistration(Client);
}

static
NTSTATUS
XdpNmrClientGetInterfaceDispatch(
    _In_ const XDP_VERSION *Version,
    _In_ VOID *GetInterfaceContext,
    _Out_ VOID **InterfaceContext,
    _Out_ const VOID **InterfaceDispatch
    )
{
    XDP_CLIENT_SINGLE_VERSION *ClientSingleVersion = GetInterfaceContext;

    if (Version->Major != ClientSingleVersion->ClientVersion->Major) {
        return STATUS_NOT_SUPPORTED;
    }

    *InterfaceContext = ClientSingleVersion->InterfaceContext;
    *InterfaceDispatch = ClientSingleVersion->InterfaceDispatch;

    return STATUS_SUCCESS;
}

NTSTATUS
XdpRegisterInterfaceEx(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_CAPABILITIES_EX *CapabilitiesEx,
    _In_ VOID *InterfaceContext,
    _In_ const XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    )
{
    XDP_CLIENT_SINGLE_VERSION *ClientSingleVersion = NULL;
    NTSTATUS Status;

    if (CapabilitiesEx->DriverApiVersionCount != 1) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ClientSingleVersion =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*ClientSingleVersion), POOLTAG_NMR_CLIENT);
    if (ClientSingleVersion == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ClientSingleVersion->ClientVersion =
        RTL_PTR_ADD(CapabilitiesEx, CapabilitiesEx->DriverApiVersionsOffset);
    ClientSingleVersion->InterfaceContext = InterfaceContext;
    ClientSingleVersion->InterfaceDispatch = InterfaceDispatch;

    Status =
        XdpRegisterInterfaceInternal(
            InterfaceIndex, CapabilitiesEx, ClientSingleVersion,
            XdpNmrClientGetInterfaceDispatch, &ClientSingleVersion->Client,
            RegistrationHandle);

Exit:
    if (!NT_SUCCESS(Status)) {
        if (ClientSingleVersion != NULL) {
            ExFreePoolWithTag(ClientSingleVersion, POOLTAG_NMR_CLIENT);
        }
    }

    return Status;
}

VOID
XdpDeregisterInterface(
    _In_ XDP_REGISTRATION_HANDLE RegistrationHandle
    )
{
    XDP_CLIENT_SINGLE_VERSION *ClientSingleVersion =
        (XDP_CLIENT_SINGLE_VERSION *)RegistrationHandle;

    XdpDeregisterInterfaceInternal(&ClientSingleVersion->Client);
    ExFreePoolWithTag(ClientSingleVersion, POOLTAG_NMR_CLIENT);
}
