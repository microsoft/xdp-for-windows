//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include <stdlib.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wil/resource.h>

#include "util.h"

#define XDP_SERVICE_NAME "xdp"

#define NT_VERIFY(expr) \
    if (!(expr)) { printf("("#expr") failed line %d\n", __LINE__); exit(1); } // TODO: return error values instead of exit

CONST CHAR*
GetPowershellPrefix()
{
    return (system("pwsh -v 2> NUL") == 0) ?
        "pwsh -ExecutionPolicy Bypass" : "powershell -noprofile -ExecutionPolicy Bypass";
}

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
wil::unique_schandle
OpenServiceHandle(
    _In_z_ CONST CHAR *ServiceName
    )
{
    wil::unique_schandle ScmHandle{ OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS) };
    NT_VERIFY(ScmHandle.is_valid());

    wil::unique_schandle SvcHandle{ OpenService(ScmHandle.get(), ServiceName, SERVICE_QUERY_STATUS) };

    if (!SvcHandle.is_valid()) {
        NT_VERIFY(GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST);
    }

    return SvcHandle;
}

BOOLEAN
IsServiceInstalled(
    _In_z_ CONST CHAR *ServiceName
    )
{
    return OpenServiceHandle(ServiceName).is_valid();
}

BOOLEAN
IsServiceRunning(
    _In_z_ CONST CHAR *ServiceName
    )
{
    wil::unique_schandle SvcHandle = OpenServiceHandle(ServiceName);
    SERVICE_STATUS ServiceStatus;

    if (!SvcHandle.is_valid()) {
        return FALSE;
    }

    NT_VERIFY(QueryServiceStatus(SvcHandle.get(), &ServiceStatus));

    return ServiceStatus.dwCurrentState != SERVICE_STOPPED;
}

static BOOLEAN XdpPreinstalled = TRUE;

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

BOOLEAN
XdpUninstall()
{
    CHAR CmdBuff[256];

    sprintf(CmdBuff, "%s  .\\xdp.ps1 -Uninstall %s", GetPowershellPrefix(), XdpPreinstalled ? "-DriverPreinstalled" : "");
    NT_VERIFY(system(CmdBuff) == 0);
    NT_VERIFY(!IsServiceRunning(XDP_SERVICE_NAME));
    return TRUE;
}