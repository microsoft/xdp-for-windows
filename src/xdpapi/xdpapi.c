//
// Copyright (C) Microsoft Corporation. All rights reserved.
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
XdpRssOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *RssHandle
    )
{
    XDP_RSS_OPEN *RssOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*RssOpen)];

    RssOpen = XdpInitializeEa(XDP_OBJECT_TYPE_RSS, EaBuffer, sizeof(EaBuffer));
    RssOpen->IfIndex = InterfaceIndex;

    *RssHandle = XdpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer));
    if (*RssHandle == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XDPAPI
XdpRssSet(
    _In_ HANDLE RssHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    BOOL Success =
        XdpIoctl(
            RssHandle, IOCTL_RSS_SET, (XDP_RSS_CONFIGURATION *)RssConfiguration,
            RssConfigurationSize, NULL, 0, NULL, NULL);
    if (!Success) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
XDPAPI
XdpRssGet(
    _In_ HANDLE RssHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    BOOL Success =
        XdpIoctl(
            RssHandle, IOCTL_RSS_GET, NULL, 0, RssConfiguration,
            *RssConfigurationSize, (ULONG *)RssConfigurationSize, NULL);
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
