//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

XDP_OPEN_API_FN XdpOpenApi;
XDP_CLOSE_API_FN XdpCloseApi;
XDP_CREATE_PROGRAM_FN XdpCreateProgram;
XDP_INTERFACE_OPEN_FN XdpInterfaceOpen;
XDP_RSS_GET_CAPABILITIES_FN XdpRssGetCapabilities;
XDP_RSS_SET_FN XdpRssSet;
XDP_RSS_GET_FN XdpRssGet;
XSK_CREATE_FN XskCreate;
XSK_BIND_FN XskBind;
XSK_ACTIVATE_FN XskActivate;
XSK_NOTIFY_SOCKET_FN XskNotifySocket;
XSK_NOTIFY_ASYNC_FN XskNotifyAsync;
XSK_GET_NOTIFY_ASYNC_RESULT_FN XskGetNotifyAsyncResult;
XSK_SET_SOCKOPT_FN XskSetSockopt;
XSK_GET_SOCKOPT_FN XskGetSockopt;
XSK_IOCTL_FN XskIoctl;

static CONST XDP_API_TABLE XdpApiTablePrerelease = {
    .XdpOpenApi = XdpOpenApi,
    .XdpCloseApi = XdpCloseApi,
    .XdpCreateProgram = XdpCreateProgram,
    .XdpInterfaceOpen = XdpInterfaceOpen,
    .XdpRssGetCapabilities = XdpRssGetCapabilities,
    .XdpRssSet = XdpRssSet,
    .XdpRssGet = XdpRssGet,
    .XskCreate = XskCreate,
    .XskBind = XskBind,
    .XskActivate = XskActivate,
    .XskNotifySocket = XskNotifySocket,
    .XskNotifyAsync = XskNotifyAsync,
    .XskGetNotifyAsyncResult = XskGetNotifyAsyncResult,
    .XskSetSockopt = XskSetSockopt,
    .XskGetSockopt = XskGetSockopt,
    .XskIoctl = XskIoctl,
};

HRESULT
XDPAPI
XdpOpenApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ CONST XDP_API_TABLE **XdpApiTable
    )
{
    *XdpApiTable = NULL;

    if (XdpApiVersion != XDP_VERSION_PRERELEASE) {
        return E_NOINTERFACE;
    }

    *XdpApiTable = &XdpApiTablePrerelease;

    return S_OK;
}

VOID
XDPAPI
XdpCloseApi(
    _In_ CONST XDP_API_TABLE *XdpApiTable
    )
{
    FRE_ASSERT(XdpApiTable == &XdpApiTablePrerelease);
}

HRESULT
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ UINT32 Flags,
    _In_reads_(RuleCount) CONST XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_PROGRAM_OPEN *ProgramOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*ProgramOpen)];

    ProgramOpen = XdpInitializeEa(XDP_OBJECT_TYPE_PROGRAM, EaBuffer, sizeof(EaBuffer));
    ProgramOpen->IfIndex = InterfaceIndex;
    ProgramOpen->HookId = *HookId;
    ProgramOpen->QueueId = QueueId;
    ProgramOpen->Flags = Flags;
    ProgramOpen->RuleCount = RuleCount;
    ProgramOpen->Rules = Rules;

    *Program = XdpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (*Program == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_INTERFACE_OPEN *InterfaceOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*InterfaceOpen)];

    InterfaceOpen =
        XdpInitializeEa(XDP_OBJECT_TYPE_INTERFACE, EaBuffer, sizeof(EaBuffer));
    InterfaceOpen->IfIndex = InterfaceIndex;

    *InterfaceHandle = XdpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (*InterfaceHandle == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XdpRssGetCapabilities(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    )
{
    BOOL Success =
        XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES,
            NULL, 0, RssCapabilities, *RssCapabilitiesSize,
            (ULONG *)RssCapabilitiesSize, NULL, TRUE);
    if (!Success) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XdpRssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    BOOL Success =
        XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_SET,
            (XDP_RSS_CONFIGURATION *)RssConfiguration, RssConfigurationSize,
            NULL, 0, NULL, NULL, TRUE);
    if (!Success) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XdpRssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    BOOL Success =
        XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_GET, NULL, 0, RssConfiguration,
            *RssConfigurationSize, (ULONG *)RssConfigurationSize, NULL, TRUE);
    if (!Success) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE ModuleHandle,
    _In_ DWORD Reason,
    _In_ VOID *Reserved
    )
{
    UNREFERENCED_PARAMETER(Reserved);

    switch (Reason) {
    case DLL_PROCESS_ATTACH:
#ifndef _MT
        //
        // Disable DLL_THREAD_ATTACH/DETACH notifications if using shared CRT.
        // This must not be called if using static CRT.
        //
        DisableThreadLibraryCalls(ModuleHandle);
#else
        UNREFERENCED_PARAMETER(ModuleHandle);
#endif
        break;

    default:
        break;
    }

    return TRUE;
}
