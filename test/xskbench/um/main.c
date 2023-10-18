//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdpapi.h>
#include "xskbench_user.h"
#include "xskbench.h"

CONST XDP_API_TABLE *XdpApi;

VOID
PlatInitializeXdpApi(
    VOID
    )
{
    ASSERT_FRE(SUCCEEDED(XdpOpenApi(XDP_API_VERSION_1, &XdpApi)));
}

VOID
PlatUninitializeXdpApi(
    VOID
    )
{
    XdpCloseApi(XdpApi);
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

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    XskBenchInitialize();

    ASSERT_FRE(SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE));

    return XskBenchStart(argc, argv);
}
