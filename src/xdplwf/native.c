//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "native.tmh"

static
CONST
XDP_HOOK_ID NativeHooks[] = {
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_RX,
        .SubLayer   = XDP_HOOK_INSPECT,
    },
    {
        .Layer      = XDP_HOOK_L2,
        .Direction  = XDP_HOOK_TX,
        .SubLayer   = XDP_HOOK_INJECT,
    },
};

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpNativeDeleteBindingComplete(
    _In_ VOID *BindingContext
    )
{
    XDP_LWF_NATIVE *Native = BindingContext;

    KeSetEvent(Native->BindingDeletedEvent, 0, FALSE);
}

NTSTATUS
XdpNativeCreateBinding(
    _Inout_ XDP_LWF_NATIVE *Native,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex,
    _Out_ XDP_REGISTER_IF *RegisterIf
    )
{
    NTSTATUS Status;
    XDP_CAPABILITIES_EX *CapabilitiesEx = NULL;
    ULONG BytesReturned = 0;

    Native->NdisFilterHandle = NdisFilterHandle;
    Native->IfIndex = IfIndex;

    Status =
        XdpLwfOidInternalRequest(
            Native->NdisFilterHandle, NdisRequestQueryInformation,
            OID_XDP_QUERY_CAPABILITIES, NULL, 0, 0, 0, &BytesReturned);

    if (Status != STATUS_BUFFER_TOO_SMALL) {
        if (!NT_VERIFY(!NT_SUCCESS(Status))) {
            //
            // The NIC is acting strange, so declare it doesn't support XDP.
            //
            Status = STATUS_NOT_SUPPORTED;
        }
        goto Exit;
    }

    if (!NT_VERIFY(BytesReturned > 0)) {
        //
        // The NIC is acting strange, so declare it doesn't support XDP.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    CapabilitiesEx = ExAllocatePoolZero(NonPagedPoolNx, BytesReturned, POOLTAG_NATIVE);
    if (CapabilitiesEx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Native->Capabilities.Mode = XDP_INTERFACE_MODE_NATIVE;
    Native->Capabilities.Hooks = NativeHooks;
    Native->Capabilities.HookCount = RTL_NUMBER_OF(NativeHooks);
    Native->Capabilities.CapabilitiesEx = CapabilitiesEx;

    Status =
        XdpLwfOidInternalRequest(
            Native->NdisFilterHandle, NdisRequestQueryInformation,
            OID_XDP_QUERY_CAPABILITIES, CapabilitiesEx,
            BytesReturned, 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (BytesReturned < RTL_SIZEOF_THROUGH_FIELD(XDP_CAPABILITIES_EX, Header)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Native->Capabilities.CapabilitiesSize = BytesReturned;

    RtlZeroMemory(RegisterIf, sizeof(*RegisterIf));
    RegisterIf->InterfaceCapabilities = &Native->Capabilities;
    RegisterIf->DeleteBindingComplete = XdpNativeDeleteBindingComplete;
    RegisterIf->BindingContext = Native;
    RegisterIf->BindingHandle = &Native->BindingHandle;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpNativeDeleteBinding(Native);
    }

    return Status;
}

VOID
XdpNativeDeleteBinding(
    _In_ XDP_LWF_NATIVE *Native
    )
{
    KEVENT DeletedEvent;

    if (Native->BindingHandle != NULL) {
        KeInitializeEvent(&DeletedEvent, NotificationEvent, FALSE);
        Native->BindingDeletedEvent = &DeletedEvent;

        // Initiate core XDP cleanup and wait for completion.
        XdpIfDeleteBindings(&Native->BindingHandle, 1);
        KeWaitForSingleObject(&DeletedEvent, Executive, KernelMode, FALSE, NULL);
        Native->BindingDeletedEvent = NULL;
        Native->BindingHandle = NULL;
    }

    if (Native->Capabilities.CapabilitiesEx != NULL) {
        ExFreePoolWithTag((VOID *)Native->Capabilities.CapabilitiesEx, POOLTAG_NATIVE);
        Native->Capabilities.CapabilitiesEx = NULL;
        Native->Capabilities.CapabilitiesSize = 0;
    }
}
