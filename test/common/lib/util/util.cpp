//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <stdlib.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>

#include "util.h"

#define XDP_SERVICE_NAME "xdp"

#define NT_VERIFY(expr) \
    if (!(expr)) { printf("("#expr") failed line %d\n", __LINE__); exit(1); } // TODO: return error values instead of exit

EXTERN_C
CONST CHAR*
GetPowershellPrefix()
{
    return "powershell -noprofile -ExecutionPolicy Bypass";
}

EXTERN_C
_Success_(return == 0)
DWORD
ConvertInterfaceAliasToIndex(
    _In_ CONST WCHAR *Alias,
    _Out_ ULONG *IfIndex
    )
{
    NET_LUID Luid;
    DWORD Error;

    Error = ConvertInterfaceAliasToLuid(Alias, &Luid);
    if (Error == NO_ERROR) {
        Error = ConvertInterfaceLuidToIndex(&Luid, IfIndex);
    }

    return Error;
}

static
SC_HANDLE
OpenServiceHandle(
    _In_z_ CONST CHAR *ServiceName
    )
{
    SC_HANDLE ScmHandle = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    NT_VERIFY(ScmHandle);

    SC_HANDLE SvcHandle = OpenServiceA(ScmHandle, ServiceName, SERVICE_QUERY_STATUS);
    NT_VERIFY(ScmHandle);

    if (!ScmHandle) {
        NT_VERIFY(GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST);
    }

    CloseServiceHandle(ScmHandle);

    return SvcHandle;
}

EXTERN_C
BOOLEAN
IsServiceInstalled(
    _In_z_ CONST CHAR *ServiceName
    )
{
    SC_HANDLE SvcHandle = OpenServiceHandle(ServiceName);
    BOOLEAN Result = SvcHandle != nullptr;
    if (SvcHandle) {
        CloseServiceHandle(SvcHandle);
    }
    return Result;
}

EXTERN_C
BOOLEAN
IsServiceRunning(
    _In_z_ CONST CHAR *ServiceName
    )
{
    SC_HANDLE SvcHandle = OpenServiceHandle(ServiceName);
    SERVICE_STATUS ServiceStatus;

    if (!SvcHandle) {
        return FALSE;
    }

    NT_VERIFY(QueryServiceStatus(SvcHandle, &ServiceStatus));

    CloseServiceHandle(SvcHandle);

    return ServiceStatus.dwCurrentState != SERVICE_STOPPED;
}

static BOOLEAN XdpPreinstalled = TRUE;

EXTERN_C
BOOLEAN
XdpInstall()
{
    CHAR CmdBuff[256];

    XdpPreinstalled = IsServiceInstalled(XDP_SERVICE_NAME);

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s .\\xdp.ps1 -Install %s", GetPowershellPrefix(), XdpPreinstalled ? "-DriverPreinstalled" : "");
    NT_VERIFY(system(CmdBuff) == 0);
    return TRUE;
}

EXTERN_C
BOOLEAN
XdpUninstall()
{
    CHAR CmdBuff[256];

    sprintf_s(CmdBuff, "%s  .\\xdp.ps1 -Uninstall %s", GetPowershellPrefix(), XdpPreinstalled ? "-DriverPreinstalled" : "");
    NT_VERIFY(system(CmdBuff) == 0);
    NT_VERIFY(!IsServiceRunning(XDP_SERVICE_NAME));
    return TRUE;
}
