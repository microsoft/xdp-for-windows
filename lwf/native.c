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
    _In_ VOID *InterfaceBindingContext
    )
{
    XDP_LWF_NATIVE *Native = InterfaceBindingContext;

    KeSetEvent(Native->BindingDeletedEvent, 0, FALSE);
}

VOID
XdpNativeCreateBinding(
    _Inout_ XDP_LWF_NATIVE *Native,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex
    )
{
    NTSTATUS Status;
    XDP_REGISTER_IF XdpInterface = {0};
    XDP_CAPABILITIES *Capabilities = NULL;
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

    Capabilities = ExAllocatePoolZero(NonPagedPoolNx, BytesReturned, POOLTAG_NATIVE);
    if (Capabilities == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status =
        XdpLwfOidInternalRequest(
            Native->NdisFilterHandle, NdisRequestQueryInformation,
            OID_XDP_QUERY_CAPABILITIES, Capabilities,
            BytesReturned, 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (BytesReturned < RTL_SIZEOF_THROUGH_FIELD(XDP_CAPABILITIES, Size) ||
        BytesReturned != Capabilities->Size) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Native->Capabilities.Mode = XDP_INTERFACE_MODE_NATIVE;
    Native->Capabilities.Hooks = NativeHooks;
    Native->Capabilities.HookCount = RTL_NUMBER_OF(NativeHooks);
    Native->Capabilities.Capabilities = Capabilities;
    Capabilities = NULL;

    XdpInterface.InterfaceCapabilities = &Native->Capabilities;
    XdpInterface.DeleteBindingComplete = XdpNativeDeleteBindingComplete;
    XdpInterface.InterfaceContext = Native;

    Status = XdpIfCreateBinding(IfIndex, &XdpInterface, 1);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Native->BindingHandle = XdpInterface.BindingHandle;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpNativeDeleteBinding(Native);
    }
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
        XdpIfDeleteBinding(&Native->BindingHandle, 1);
        KeWaitForSingleObject(&DeletedEvent, Executive, KernelMode, FALSE, NULL);
        Native->BindingDeletedEvent = NULL;
        Native->BindingHandle = NULL;
    }

    if (Native->Capabilities.Capabilities != NULL) {
        ExFreePoolWithTag((VOID *)Native->Capabilities.Capabilities, POOLTAG_NATIVE);
        Native->Capabilities.Capabilities = NULL;
    }
}
