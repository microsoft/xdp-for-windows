//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// API routines.
//
XDP_OPEN_API_FN XdpOpenApi;
XDP_CLOSE_API_FN XdpCloseApi;
XDP_GET_ROUTINE_FN XdpGetRoutine;
XDP_CREATE_PROGRAM_FN XdpCreateProgram;
XDP_INTERFACE_OPEN_FN XdpInterfaceOpen;
XSK_CREATE_FN XskCreate;
XSK_BIND_FN XskBind;
XSK_ACTIVATE_FN XskActivate;
XSK_NOTIFY_SOCKET_FN XskNotifySocket;
XSK_NOTIFY_ASYNC_FN XskNotifyAsync;
XSK_GET_NOTIFY_ASYNC_RESULT_FN XskGetNotifyAsyncResult;
XSK_SET_SOCKOPT_FN XskSetSockopt;
XSK_GET_SOCKOPT_FN XskGetSockopt;
XSK_IOCTL_FN XskIoctl;

//
// Experimental APIs, subject to removal in a minor release.
//
XDP_RSS_GET_CAPABILITIES_FN XdpRssGetCapabilities;
XDP_RSS_SET_FN XdpRssSet;
XDP_RSS_GET_FN XdpRssGet;
XDP_QEO_SET_FN XdpQeoSet;

typedef struct _XDP_API_ROUTINE {
    _Null_terminated_ const CHAR *RoutineName;
    VOID *Routine;
} XDP_API_ROUTINE;

#define DECLARE_XDP_API_ROUTINE(_routine) #_routine, (VOID *)_routine
#define DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(_routine, _name) _name, (VOID *)_routine

static const XDP_API_ROUTINE XdpApiRoutines[] = {
    { DECLARE_XDP_API_ROUTINE(XdpOpenApi) },
    { DECLARE_XDP_API_ROUTINE(XdpCloseApi) },
    { DECLARE_XDP_API_ROUTINE(XdpGetRoutine) },
    { DECLARE_XDP_API_ROUTINE(XdpCreateProgram) },
    { DECLARE_XDP_API_ROUTINE(XdpInterfaceOpen) },
    { DECLARE_XDP_API_ROUTINE(XskCreate) },
    { DECLARE_XDP_API_ROUTINE(XskBind) },
    { DECLARE_XDP_API_ROUTINE(XskActivate) },
    { DECLARE_XDP_API_ROUTINE(XskNotifySocket) },
    { DECLARE_XDP_API_ROUTINE(XskNotifyAsync) },
    { DECLARE_XDP_API_ROUTINE(XskGetNotifyAsyncResult) },
    { DECLARE_XDP_API_ROUTINE(XskSetSockopt) },
    { DECLARE_XDP_API_ROUTINE(XskGetSockopt) },
    { DECLARE_XDP_API_ROUTINE(XskIoctl) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssGetCapabilities, XDP_RSS_GET_CAPABILITIES_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssSet, XDP_RSS_SET_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpRssGet, XDP_RSS_GET_FN_NAME) },
    { DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(XdpQeoSet, XDP_QEO_SET_FN_NAME) },
};

static CONST XDP_API_TABLE XdpApiTableV1 = {
    .XdpOpenApi = XdpOpenApi,
    .XdpCloseApi = XdpCloseApi,
    .XdpGetRoutine = XdpGetRoutine,
    .XdpCreateProgram = XdpCreateProgram,
    .XdpInterfaceOpen = XdpInterfaceOpen,
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

    if (XdpApiVersion != XDP_API_VERSION_1) {
        return E_NOINTERFACE;
    }

    *XdpApiTable = &XdpApiTableV1;

    return S_OK;
}

VOID
XDPAPI
XdpCloseApi(
    _In_ CONST XDP_API_TABLE *XdpApiTable
    )
{
    FRE_ASSERT(XdpApiTable == &XdpApiTableV1);
}

VOID *
XdpGetRoutine(
    _In_z_ const CHAR *RoutineName
    )
{
    for (UINT32 i = 0; i < RTL_NUMBER_OF(XdpApiRoutines); i++) {
        if (strcmp(XdpApiRoutines[i].RoutineName, RoutineName) == 0) {
            return XdpApiRoutines[i].Routine;
        }
    }

    return NULL;
}

HRESULT
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
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

HRESULT
XdpQeoSet(
    _In_ HANDLE InterfaceHandle,
    _Inout_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    )
{
    BOOL Success =
        XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_QEO_SET,
            QuicConnections, QuicConnectionsSize,
            QuicConnections, QuicConnectionsSize,
            (ULONG *)&QuicConnectionsSize, NULL, TRUE);
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
#if DBG
        //
        // Redirect all CRT errors to stderr + the debugger, rather than
        // creating a message dialog box and waiting for user interaction.
        // This is primarily required for ASAN interop.
        //
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
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
