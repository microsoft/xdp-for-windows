//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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
XdpNativeRemoveInterfaceComplete(
    _In_ VOID *InterfaceContext
    )
{
    XDP_LWF_NATIVE *Native = InterfaceContext;

    KeSetEvent(&Native->InterfaceRemovedEvent, 0, FALSE);
}

NTSTATUS
XdpNativeAttachInterface(
    _Inout_ XDP_LWF_NATIVE *Native,
    _In_ XDP_LWF_FILTER *Filter,
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NET_IFINDEX IfIndex,
    _Out_ XDP_ADD_INTERFACE *AddIf
    )
{
    NTSTATUS Status;
    XDP_CAPABILITIES_EX *CapabilitiesEx = NULL;
    ULONG BytesReturned = 0;

    //
    // This function supplies its caller with XDPIF interface addition info and
    // the caller will add the XDPIF interface.
    //

    Native->Filter = Filter;
    Native->NdisFilterHandle = NdisFilterHandle;
    Native->IfIndex = IfIndex;
    KeInitializeEvent(&Native->InterfaceRemovedEvent, NotificationEvent, FALSE);

    Status =
        XdpLwfOidInternalRequest(
            Native->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_XDP_QUERY_CAPABILITIES, NULL, 0, 0, 0, &BytesReturned);

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
            Native->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_REGULAR,
            NdisRequestQueryInformation, OID_XDP_QUERY_CAPABILITIES, CapabilitiesEx,
            BytesReturned, 0, 0, &BytesReturned);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (BytesReturned < RTL_SIZEOF_THROUGH_FIELD(XDP_CAPABILITIES_EX, Header)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Native->Capabilities.CapabilitiesSize = BytesReturned;

    RtlZeroMemory(AddIf, sizeof(*AddIf));
    AddIf->InterfaceCapabilities = &Native->Capabilities;
    AddIf->RemoveInterfaceComplete = XdpNativeRemoveInterfaceComplete;
    AddIf->InterfaceContext = Native;
    AddIf->InterfaceHandle = &Native->XdpIfInterfaceHandle;

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpNativeDetachInterface(Native);
    }

    return Status;
}

VOID
XdpNativeDetachInterface(
    _In_ XDP_LWF_NATIVE *Native
    )
{
    //
    // The caller of the attach routine added the XDPIF interface, but this
    // function removes the XDPIF interface.
    //

    if (Native->XdpIfInterfaceHandle != NULL) {
        //
        // Initiate core XDP cleanup and wait for completion.
        //
        XdpIfRemoveInterfaces(&Native->XdpIfInterfaceHandle, 1);
    }
}

VOID
XdpNativeWaitForDetachInterfaceComplete(
    _In_ XDP_LWF_NATIVE *Native
    )
{
    if (Native->XdpIfInterfaceHandle != NULL) {
        KeWaitForSingleObject(
            &Native->InterfaceRemovedEvent, Executive, KernelMode, FALSE, NULL);
        Native->XdpIfInterfaceHandle = NULL;
    }

    if (Native->Capabilities.CapabilitiesEx != NULL) {
        ExFreePoolWithTag((VOID *)Native->Capabilities.CapabilitiesEx, POOLTAG_NATIVE);
        Native->Capabilities.CapabilitiesEx = NULL;
        Native->Capabilities.CapabilitiesSize = 0;
    }
}
