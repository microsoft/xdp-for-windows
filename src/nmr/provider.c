//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _XDP_PROVIDER {
    NPI_PROVIDER_CHARACTERISTICS NpiProviderCharacteristics;
    NPI_MODULEID ModuleId;
    HANDLE NmrProviderHandle;
    VOID *ProviderContext;
    XDP_PROVIDER_DETACH_HANDLER *DetachHandler;

    EX_PUSH_LOCK Lock;
    BOOLEAN Closed;
    HANDLE BindingHandle;
    CONST XDP_NPI_CLIENT *NpiClient;
} XDP_PROVIDER;

static
VOID
XdpCleanupProviderRegistration(
    _In_ XDP_PROVIDER *Registration
    )
{
    NTSTATUS Status;

    if (Registration->NmrProviderHandle != NULL) {
        Status = NmrDeregisterProvider(Registration->NmrProviderHandle);
        FRE_ASSERT(Status == STATUS_PENDING);

        Status = NmrWaitForProviderDeregisterComplete(Registration->NmrProviderHandle);
        FRE_ASSERT(Status == STATUS_SUCCESS);
    }

    ExFreePoolWithTag(Registration, POOLTAG_NMR_PROVIDER);
}

static
NTSTATUS
XdpNmrProviderAttachClient(
    _In_ HANDLE NmrBindingHandle,
    _In_ VOID *ProviderContext,
    _In_ CONST NPI_REGISTRATION_INSTANCE *ClientRegistrationInstance,
    _In_ VOID *ClientBindingContext,
    _In_ CONST VOID *ClientDispatch,
    _Out_ VOID **ProviderBindingContext,
    _Out_ CONST VOID **ProviderDispatch
    )
{
    XDP_PROVIDER *Provider = ProviderContext;
    CONST NPI_REGISTRATION_INSTANCE *ProviderRegistrationInstance =
        &Provider->NpiProviderCharacteristics.ProviderRegistrationInstance;
    CONST XDP_NPI_CLIENT *NpiClient = ClientDispatch;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ClientBindingContext);

    //
    // Version the NMR binding protocol separately from the XDP API.
    //
    if (ClientRegistrationInstance->Version != XDP_BINDING_VERSION_1) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    //
    // The interface index of the client must match the provider. This prevents
    // a NIC redirecting XDP onto an unexpected interface.
    //
    if (ClientRegistrationInstance->Number != ProviderRegistrationInstance->Number) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // The NMR client must match the expected registration ID.
    //
    if (!NmrIsEqualNpiModuleId(
            ClientRegistrationInstance->ModuleId, ProviderRegistrationInstance->ModuleId)) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    if (NpiClient->Header.Revision < XDP_NPI_CLIENT_REVISION_1 ||
        NpiClient->Header.Size < XDP_SIZEOF_NPI_CLIENT_REVISION_1) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    //
    // To simplify the caller's state machine, allow at most one binding over
    // the lifetime of the provider.
    //

    RtlAcquirePushLockExclusive(&Provider->Lock);

    if (Provider->BindingHandle != NULL || Provider->Closed) {
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        Provider->BindingHandle = NmrBindingHandle;
        Provider->NpiClient = NpiClient;
        Status = STATUS_SUCCESS;
    }

    RtlReleasePushLockExclusive(&Provider->Lock);

Exit:

    if (NT_SUCCESS(Status)) {
        *ProviderBindingContext = Provider;
        *ProviderDispatch = NULL;
    }

    return Status;
}

static
VOID
XdpNmrDetach(
    _In_ XDP_PROVIDER *Provider
    )
{
    BOOLEAN NeedDetach = FALSE;

    //
    // This routine ensure the provider's detach callback is invoked exactly
    // once.
    //

    RtlAcquirePushLockExclusive(&Provider->Lock);

    NeedDetach = !Provider->Closed;
    Provider->Closed = TRUE;

    RtlReleasePushLockExclusive(&Provider->Lock);

    if (NeedDetach) {
        Provider->DetachHandler(Provider->ProviderContext);
    }
}

static
NTSTATUS
XdpNmrProviderDetachClient(
    _In_ VOID *ProviderBindingContext
    )
{
    XDP_PROVIDER *Provider = ProviderBindingContext;

    XdpNmrDetach(Provider);

    return STATUS_PENDING;
}

VOID
XdpCloseProvider(
    _In_ XDP_PROVIDER_HANDLE ProviderHandle
    )
{
    XDP_PROVIDER *Provider = (XDP_PROVIDER *)ProviderHandle;

    XdpNmrDetach(Provider);
}

VOID
XdpCleanupProvider(
    _In_ XDP_PROVIDER_HANDLE ProviderHandle
    )
{
    XDP_PROVIDER *Provider = (XDP_PROVIDER *)ProviderHandle;

    if (Provider->BindingHandle != NULL) {
        NmrProviderDetachClientComplete(Provider->BindingHandle);
    }

    XdpCleanupProviderRegistration(Provider);
}

NTSTATUS
XdpOpenProvider(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST GUID *ClientGuid,
    _In_ VOID *ProviderContext,
    _In_ XDP_PROVIDER_DETACH_HANDLER *DetachHandler,
    _Out_ VOID **NpiGetInterfaceContext,
    _Out_ XDP_GET_INTERFACE_DISPATCH  **NpiGetInterfaceDispatch,
    _Out_ XDP_PROVIDER_HANDLE *ProviderHandle
    )
{
    NTSTATUS Status;
    XDP_PROVIDER *Provider;
    NPI_PROVIDER_CHARACTERISTICS *NpiCharacteristics;
    NPI_REGISTRATION_INSTANCE *NpiInstance;

    Provider = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Provider), POOLTAG_NMR_PROVIDER);
    if (Provider == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ExInitializePushLock(&Provider->Lock);

    Provider->ProviderContext = ProviderContext;
    Provider->DetachHandler = DetachHandler;

    Provider->ModuleId.Length = sizeof(Provider->ModuleId);
    Provider->ModuleId.Type = MIT_GUID;
    Provider->ModuleId.Guid = *ClientGuid;

    NpiCharacteristics = &Provider->NpiProviderCharacteristics;
    NpiCharacteristics->Length = sizeof(*NpiCharacteristics);
    NpiCharacteristics->ProviderAttachClient = XdpNmrProviderAttachClient;
    NpiCharacteristics->ProviderDetachClient = XdpNmrProviderDetachClient;

    NpiInstance = &NpiCharacteristics->ProviderRegistrationInstance;
    NpiInstance->Size = sizeof(*NpiInstance);
    NpiInstance->Version = XDP_BINDING_VERSION_1;
    NpiInstance->NpiId = &NPI_XDP_ID;
    NpiInstance->ModuleId = &Provider->ModuleId;
    NpiInstance->Number = InterfaceIndex;

    Status =
        NmrRegisterProvider(
            &Provider->NpiProviderCharacteristics, Provider, &Provider->NmrProviderHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&Provider->Lock);

    if (Provider->BindingHandle != NULL) {
        *NpiGetInterfaceContext = Provider->NpiClient->GetInterfaceContext;
        *NpiGetInterfaceDispatch = Provider->NpiClient->GetInterfaceDispatch;
        Status = STATUS_SUCCESS;
    } else {
        Provider->Closed = TRUE;
        Status = STATUS_NOINTERFACE;
    }

    RtlReleasePushLockExclusive(&Provider->Lock);

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *ProviderHandle = (XDP_PROVIDER_HANDLE)Provider;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Provider != NULL) {
            XdpCleanupProviderRegistration(Provider);
        }
    }

    return Status;
}
