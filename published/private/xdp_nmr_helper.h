#pragma once

#include <ntddk.h>
#include <intsafe.h>
#include <ntstrsafe.h>
#include <netioddk.h>
#include <initguid.h>
#include <xdpapi.h>
#include <xdpapi_experimental.h>
// #include "cxplat.h"
// #include "xskbench.h"
// #include "trace.h"
// #include "xskbenchdrvioctl.h"
// #include "driver.tmh"

typedef struct _XBDRV_NMR_CLIENT_BINDING_CONTEXT {
    NPI Npi;
    HANDLE NmrBindingHandle;
} XBDRV_NMR_CLIENT_BINDING_CONTEXT;

XDP_API_PROVIDER_DISPATCH *XdpApi;
XDP_API_PROVIDER_BINDING_CONTEXT *XdpApiProviderBindingContext;

static DEVICE_OBJECT *XskBenchDrvDeviceObject;
static BOOLEAN IsDeviceOpened;
static BOOLEAN IsSessionActive;
static HANDLE MainThread;
static int Argc;
static char **Argv;
static HANDLE NmrRegistrationHandle;
static KEVENT BoundToProvider;

const NPI_MODULEID NPI_XBDRV_MODULEID = {
    sizeof(NPI_MODULEID),
    MIT_GUID,
    { 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_STATUS
XbDrvXskNotifyCallback(
    _In_ VOID* ClientContext,
    _In_ XSK_NOTIFY_RESULT_FLAGS Result
    )
{
    UNREFERENCED_PARAMETER(ClientContext);
    UNREFERENCED_PARAMETER(Result);
    return STATUS_SUCCESS;
}

static const XDP_API_CLIENT_DISPATCH NmrXdpApiClientDispatch = {
    XbDrvXskNotifyCallback
};

//
// Notify provider attach code.
//
NTSTATUS
XbDrvNmrAttachXdpApiProvider(
    HANDLE NmrBindingHandle,
    PVOID ClientContext,
    PNPI_REGISTRATION_INSTANCE ProviderRegistrationInstance
    )
{
    NTSTATUS Status;
    XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext = NULL;

    UNREFERENCED_PARAMETER(ClientContext);

    // TraceEnter(TRACE_CONTROL, "-");

    //
    // Check if this provider interface is suitable.
    //
    if (ProviderRegistrationInstance->Number != XDP_API_VERSION_1) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    // Only support a single provider
    if (XdpApi != NULL) {
        Status = STATUS_NOINTERFACE;
        goto Exit;
    }

    //
    // Allocate memory for this binding.
    //
    BindingContext =
        (XBDRV_NMR_CLIENT_BINDING_CONTEXT*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*BindingContext), 'vDbX');
    if (BindingContext == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    BindingContext->NmrBindingHandle = NmrBindingHandle;

    //
    // Attach to the provider.
    //
    Status =
        NmrClientAttachProvider(
            NmrBindingHandle,
            BindingContext,                 // ClientBindingContext
            &NmrXdpApiClientDispatch,       // ClientDispatch
            &BindingContext->Npi.Handle,    // ProviderBindingContext
            &BindingContext->Npi.Dispatch); // ProviderDispatch
    if (!NT_SUCCESS(Status)) {
        ExFreePool(BindingContext);
        goto Exit;
    }

    //
    // The client can now make calls into the provider.
    //
    XdpApi = (XDP_API_PROVIDER_DISPATCH *)BindingContext->Npi.Dispatch;
    XdpApiProviderBindingContext = (XDP_API_PROVIDER_BINDING_CONTEXT *)BindingContext->Npi.Handle;
    KeSetEvent(&BoundToProvider, 0, FALSE);

Exit:

    // TraceExitStatus(TRACE_CONTROL);

    return Status;
}

//
// Notify provider detach code.
//
NTSTATUS
XbDrvNmrDetachXdpApiProvider(
    PVOID ClientBindingContext
    )
{
    // XBDRV_NMR_CLIENT_BINDING_CONTEXT *BindingContext =
    //     (XBDRV_NMR_CLIENT_BINDING_CONTEXT *) ClientBindingContext;
    UNREFERENCED_PARAMETER(ClientBindingContext);

    // TraceEnter(TRACE_CONTROL, "-");

    //
    // Initiate the closure of all XDPAPI handles.
    //

    // return STATUS_PENDING;

    XdpApiProviderBindingContext = NULL;
    XdpApi = NULL;
    KeResetEvent(&BoundToProvider);

    // TraceExitSuccess(TRACE_CONTROL);

    return STATUS_SUCCESS;
}

// VOID
// XbDrvAllXdpApiHandlesAreClosed(
//     XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext
//     )
// {
//     //
//     // Indicate detach completion.
//     //
//     NmrClientDetachProviderComplete(BindingContext->NmrBindingHandle);
// }

VOID
XbDrvNmrCleanupXdpApiBindingContext(
    PVOID ClientBindingContext
    )
{
    XBDRV_NMR_CLIENT_BINDING_CONTEXT* BindingContext =
        (XBDRV_NMR_CLIENT_BINDING_CONTEXT*)ClientBindingContext;

    // TraceEnter(TRACE_CONTROL, "-");

    //
    // Free memory for this binding.
    //
    ExFreePool(BindingContext);

    // TraceExitSuccess(TRACE_CONTROL);
}

const NPI_CLIENT_CHARACTERISTICS XbDrvNmrXdpApiClientCharacteristics = {
    0, // Version
    sizeof(NPI_CLIENT_CHARACTERISTICS),
    (PNPI_CLIENT_ATTACH_PROVIDER_FN)XbDrvNmrAttachXdpApiProvider,
    (PNPI_CLIENT_DETACH_PROVIDER_FN)XbDrvNmrDetachXdpApiProvider,
    (PNPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN)XbDrvNmrCleanupXdpApiBindingContext,
    {
        0, // Version
        sizeof(NPI_REGISTRATION_INSTANCE),
        &NPI_XDPAPI_INTERFACE_ID,
        &NPI_XBDRV_MODULEID,
        XDP_API_VERSION_1, // Number
        NULL // NpiSpecificCharacteristics
    } // ClientRegistrationInstance
};

XDP_API_PROVIDER_BINDING_CONTEXT *
CxPlatXdpApiGetProviderBindingContext(
    VOID
    )
{
    return XdpApiProviderBindingContext;
}

VOID
CxPlatXdpApiInitialize(
    VOID
    )
{
    NTSTATUS Status;

    // TraceEnter(TRACE_CONTROL, "-");

    Status =
        NmrRegisterClient(
            &XbDrvNmrXdpApiClientCharacteristics,
            NULL,
            &NmrRegistrationHandle);
    if (!NT_SUCCESS(Status)) {
        goto Done;
    }

    KeWaitForSingleObject(&BoundToProvider, Executive, KernelMode, FALSE, NULL);

Done:

    // TraceExitStatus(TRACE_CONTROL);

    return;
}

VOID
CxPlatXdpApiUninitialize(
    VOID
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    // TraceEnter(TRACE_CONTROL, "-");

    if (NmrRegistrationHandle != NULL) {
        Status = NmrDeregisterClient(NmrRegistrationHandle);
        ASSERT(Status == STATUS_PENDING);

        if (Status == STATUS_PENDING) {
            Status = NmrWaitForClientDeregisterComplete(NmrRegistrationHandle);
            ASSERT(Status == STATUS_SUCCESS);
        }

        NmrRegistrationHandle = NULL;
    }

    // TraceExitStatus(TRACE_CONTROL);

    return;
}