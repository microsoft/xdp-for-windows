#pragma once

#include "xdpapi.h"

#define TEST_TIMEOUT_ASYNC_MS 1000
#define POLL_INTERVAL_MS 10

static
VOID
XdpDetach(
    _In_ VOID *ClientContext
    )
{
    UNREFERENCED_PARAMETER(ClientContext);
}

NTSTATUS
InitializeXdpApi(
    _Out_ XDP_API_CLIENT *ApiContext,
    _Out_ const XDP_API_PROVIDER_DISPATCH **ProviderDispatch,
    _Out_ const XDP_API_PROVIDER_BINDING_CONTEXT **ProviderBindingContext,
    _In_ const XDP_API_CLIENT_DISPATCH *ClientDispatch,
    _In_ UINT32 Version
    )
{
    NTSTATUS Status;
    KEVENT Event;
    INT32 TimeoutMs = TEST_TIMEOUT_ASYNC_MS;

    Status = XdpLoadApi(Version, NULL, NULL, &XdpDetach, ClientDispatch, ApiContext);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    do {
        LARGE_INTEGER Timeout100Ns;

        Status =
            XdpOpenApi(
                ApiContext,
                ProviderDispatch,
                ProviderBindingContext);
        if (NT_SUCCESS(Status)) {
            break;
        }

        Timeout100Ns.QuadPart = -1 * Int32x32To64(POLL_INTERVAL_MS, 10000);
        KeResetEvent(&Event);
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout100Ns);
        TimeoutMs = TimeoutMs - POLL_INTERVAL_MS;
    } while (TimeoutMs > 0);

    if (!NT_SUCCESS(Status)) {
        XdpUnloadApi(ApiContext);
    }

    return Status;
}

VOID
UninitializeXdpApi(
    _In_ XDP_API_CLIENT *ApiContext
    )
{
    XdpUnloadApi(ApiContext);
}
