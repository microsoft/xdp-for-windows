//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

HRESULT
XDPAPI
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
XDPAPI
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
XDPAPI
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
XDPAPI
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
XDPAPI
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
