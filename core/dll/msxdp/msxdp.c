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

#if DBG
HRESULT
XDPAPI
XdpBugCheck(
    VOID
    )
{
    HANDLE Handle;
    BOOL Success;

    Handle = XdpOpen(FILE_CREATE, NULL, 0);
    if (Handle == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    Success = XdpIoctl(Handle, IOCTL_XDP_BUGCHECK, NULL, 0, NULL, 0, NULL, NULL);
    if (!Success) {
        CloseHandle(Handle);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    CloseHandle(Handle);
    return S_OK;
}
#endif

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
        //
        // Disable DLL_THREAD_ATTACH/DETACH notifications.
        //
        DisableThreadLibraryCalls(ModuleHandle);
        break;

    default:
        break;
    }

    return TRUE;
}
