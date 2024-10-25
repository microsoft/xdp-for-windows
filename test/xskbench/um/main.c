//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdpapi.h>
#include <xdpapi_helper.h>
#include "cxplat.h"
#include "xskbench_user.h"
#include "xskbench.h"

VOID
CxPlatXdpApiInitialize(
    _Out_ const XDP_API_TABLE **XdpApi
    )
{
    ASSERT_FRE(SUCCEEDED(XdpHlpOpenApi(XDP_API_VERSION_LATEST, XdpApi)));
}

VOID
CxPlatXdpApiUninitialize(
    _In_ XDP_API_TABLE *XdpApi
    )
{
    XdpHlpCloseApi(XdpApi);
}

BOOL
WINAPI
ConsoleCtrlHandler(
    DWORD CtrlType
    )
{
    UNREFERENCED_PARAMETER(CtrlType);

    XskBenchCtrlHandler();

    return TRUE;
}

XDP_STATUS
CxPlatXskCreateEx(
    _In_ XDP_API_CLIENT *XdpApi,
    _Out_ HANDLE *Socket
    )
{
    return XdpHlpXskCreate(XdpApi, Socket);
}

VOID
CxPlatPrintStats(
    MY_QUEUE *Queue
    )
{
    PrintFinalStats(Queue);
}

VOID
CxPlatQueueCleanup(
    _In_ XDP_API_CLIENT *XdpApi,
    MY_QUEUE *Queue
    )
{
    UNREFERENCED_PARAMETER(XdpApi);
    if (Queue->umemReg.Address != NULL) {
        CXPLAT_VIRTUAL_FREE(Queue->umemReg.Address, 0, MEM_RELEASE, POOLTAG_UMEM);
        Queue->umemReg.Address = NULL;
    }
    if (Queue->freeRingLayout != NULL) {
        CXPLAT_FREE(Queue->freeRingLayout, POOLTAG_FREERING);
        Queue->freeRingLayout = NULL;
    }
}

BOOLEAN
CxPlatEnableLargePages(
    VOID
    )
{
    HANDLE Token = NULL;
    TOKEN_PRIVILEGES TokenPrivileges;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token)) {
        goto Failure;
    }

    TokenPrivileges.PrivilegeCount = 1;
    TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &TokenPrivileges.Privileges[0].Luid)) {
        goto Failure;
    }
    if (!AdjustTokenPrivileges(Token, FALSE, &TokenPrivileges, 0, NULL, 0)) {
        goto Failure;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        goto Failure;
    }

    CloseHandle(Token);

    return TRUE;

Failure:

    printf_error("Failed to acquire large page privileges. See \"Assigning Privileges to an Account\"\n");
    return FALSE;
}

VOID
CxPlatAlignMemory(
    _Inout_ XSK_UMEM_REG *UmemReg
    )
{
    //
    // The memory subsystem requires allocations and mappings be aligned to
    // the large page size. XDP ignores the final chunk, if truncated.
    //
    UmemReg->TotalSize = ALIGN_UP_BY(UmemReg->TotalSize, GetLargePageMinimum());
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    CxPlatInitialize();

    XskBenchInitialize();

    ASSERT_FRE(SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE));

    INT Ret = XskBenchStart(argc, argv);

    CxPlatUninitialize();

    return Ret;
}
