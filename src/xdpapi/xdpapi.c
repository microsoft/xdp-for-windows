//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#pragma warning(disable:4996) // Using deprecated APIs - by design.

//
// API routines.
//
XDP_OPEN_API_FN XdpOpenApi;
XDP_CLOSE_API_FN XdpCloseApi;
XDP_GET_ROUTINE_FN XdpGetRoutine;
XDP_GET_ROUTINE_FN XdpGetRoutineV2;
XSK_CREATE_FN XskCreateV2;

typedef struct _XDP_API_ROUTINE {
    _Null_terminated_ const CHAR *RoutineName;
    VOID *Routine;
} XDP_API_ROUTINE;

#define DECLARE_XDP_API_ROUTINE(_routine) #_routine, (VOID *)_routine
#define DECLARE_XDP_API_ROUTINE_VERSION(_routine, _ver) #_routine, (VOID *)_routine##_ver
#define DECLARE_EXPERIMENTAL_XDP_API_ROUTINE(_routine, _name) _name, (VOID *)_routine

static const XDP_API_ROUTINE XdpApiRoutinesV1[] = {
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

static const XDP_API_TABLE XdpApiTableV1 = {
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

static const XDP_API_ROUTINE XdpApiRoutinesV2[] = {
    { DECLARE_XDP_API_ROUTINE(XdpOpenApi) },
    { DECLARE_XDP_API_ROUTINE(XdpCloseApi) },
    { DECLARE_XDP_API_ROUTINE_VERSION(XdpGetRoutine, V2) },
    { DECLARE_XDP_API_ROUTINE(XdpCreateProgram) },
    { DECLARE_XDP_API_ROUTINE(XdpInterfaceOpen) },
    { DECLARE_XDP_API_ROUTINE_VERSION(XskCreate, V2) },
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

static const XDP_API_TABLE XdpApiTableV2 = {
    .XdpOpenApi = XdpOpenApi,
    .XdpCloseApi = XdpCloseApi,
    .XdpGetRoutine = XdpGetRoutine,
    .XdpCreateProgram = XdpCreateProgram,
    .XdpInterfaceOpen = XdpInterfaceOpen,
    .XskCreate = XskCreateV2,
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
    _Out_ const XDP_API_TABLE **XdpApiTable
    )
{
    switch (XdpApiVersion) {
    case XDP_API_VERSION_1:
        *XdpApiTable = &XdpApiTableV1;
        return S_OK;
    case XDP_API_VERSION_2:
        *XdpApiTable = &XdpApiTableV2;
        return S_OK;
    default:
        *XdpApiTable = NULL;
        return E_NOINTERFACE;
    }
}

VOID
XDPAPI
XdpCloseApi(
    _In_ const XDP_API_TABLE *XdpApiTable
    )
{
    FRE_ASSERT(XdpApiTable == &XdpApiTableV1 || XdpApiTable == &XdpApiTableV2);
}

static
VOID *
XdpGetRoutineFromTable(
    _In_ const XDP_API_ROUTINE *Table,
    _In_ const UINT32 TableSize,
    _In_z_ const CHAR *RoutineName
    )
{
    for (UINT32 i = 0; i < TableSize; i++) {
        if (strcmp(Table[i].RoutineName, RoutineName) == 0) {
            return Table[i].Routine;
        }
    }

    return NULL;
}

VOID *
XdpGetRoutine(
    _In_z_ const CHAR *RoutineName
    )
{
    return XdpGetRoutineFromTable(XdpApiRoutinesV1, RTL_NUMBER_OF(XdpApiRoutinesV1), RoutineName);
}

VOID *
XdpGetRoutineV2(
    _In_z_ const CHAR *RoutineName
    )
{
    return XdpGetRoutineFromTable(XdpApiRoutinesV2, RTL_NUMBER_OF(XdpApiRoutinesV2), RoutineName);
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
