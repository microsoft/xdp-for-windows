//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <stdlib.h>
#include <windows.h>
#include <iphlpapi.h>
#include <pathcch.h>
#include <stdio.h>
#include <xdpassert.h>

#include "util.h"

EXTERN_C
CONST CHAR*
GetPowershellPrefix()
{
    return "powershell -noprofile -ExecutionPolicy Bypass";
}

EXTERN_C
HRESULT
GetCurrentBinaryFileName(
    _Out_ CHAR *Path,
    _In_ UINT32 PathSize
    )
{
    HMODULE Module;

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetCurrentBinaryPath, &Module)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!GetModuleFileNameA(Module, Path, PathSize)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

EXTERN_C
HRESULT
GetCurrentBinaryPath(
    _Out_ CHAR *Path,
    _In_ UINT32 PathSize
    )
{
    HRESULT Result;
    CHAR *FinalBackslash;

    Result = GetCurrentBinaryFileName(Path, PathSize);
    if (FAILED(Result)) {
        return Result;
    }

    FinalBackslash = strrchr(Path, '\\');
    if (FinalBackslash == NULL) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    *FinalBackslash = '\0';
    return S_OK;
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
HRESULT
OpenServiceHandle(
    _Out_ SC_HANDLE *Handle,
    _In_z_ CONST CHAR *ServiceName
    )
{
    HRESULT Result = S_OK;

    SC_HANDLE ScmHandle = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    FRE_ASSERT(ScmHandle);

    *Handle = OpenServiceA(ScmHandle, ServiceName, SERVICE_ALL_ACCESS);

    if (*Handle == nullptr) {
        FRE_ASSERT(GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST);
        Result = HRESULT_FROM_WIN32(GetLastError());
    }

    FRE_ASSERT(CloseServiceHandle(ScmHandle));

    return Result;
}

EXTERN_C
BOOLEAN
IsServiceInstalled(
    _In_z_ CONST CHAR *ServiceName
    )
{
    HRESULT Result;
    SC_HANDLE SvcHandle;

    Result = OpenServiceHandle(&SvcHandle, ServiceName);
    FRE_ASSERT(SUCCEEDED(Result) == (SvcHandle != nullptr));

    if (SUCCEEDED(Result)) {
        FRE_ASSERT(CloseServiceHandle(SvcHandle));
    }

    return SUCCEEDED(Result);
}

EXTERN_C
HRESULT
GetServiceState(
    _Out_ UINT32 *ServiceState,
    _In_z_ CONST CHAR *ServiceName
    )
{
    HRESULT Result;
    SC_HANDLE SvcHandle;
    SERVICE_STATUS ServiceStatus;

    Result = OpenServiceHandle(&SvcHandle, ServiceName);
    if (FAILED(Result)) {
        return Result;
    }

    FRE_ASSERT(QueryServiceStatus(SvcHandle, &ServiceStatus));

    FRE_ASSERT(CloseServiceHandle(SvcHandle));

    *ServiceState = ServiceStatus.dwCurrentState;
    return S_OK;
}

EXTERN_C
HRESULT
StartServiceAsync(
    _In_z_ CONST CHAR *ServiceName
    )
{
    HRESULT Result;
    SC_HANDLE SvcHandle;

    Result = OpenServiceHandle(&SvcHandle, ServiceName);
    if (FAILED(Result)) {
        return Result;
    }

    if (!StartServiceA(SvcHandle, 0, nullptr)) {
        Result = HRESULT_FROM_WIN32(GetLastError());
    }

    FRE_ASSERT(CloseServiceHandle(SvcHandle));

    return Result;
}

EXTERN_C
HRESULT
StopServiceAsync(
    _In_z_ CONST CHAR *ServiceName
    )
{
    HRESULT Result;
    SC_HANDLE SvcHandle = nullptr;
    SERVICE_STATUS ServiceStatus;
    SERVICE_CONTROL_STATUS_REASON_PARAMSA ReasonParameters = {0};

    Result = OpenServiceHandle(&SvcHandle, ServiceName);
    if (FAILED(Result)) {
        goto Exit;
    }

    FRE_ASSERT(QueryServiceStatus(SvcHandle, &ServiceStatus));
    if (ServiceStatus.dwCurrentState == SERVICE_STOPPED) {
        Result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        goto Exit;
    }

    ReasonParameters.dwReason =
        SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_NONE |
            SERVICE_STOP_REASON_MINOR_NONE;

    // Stop service
    FRE_ASSERT(ControlServiceExA(
        SvcHandle, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, &ReasonParameters));

Exit:

    if (SvcHandle != nullptr) {
        FRE_ASSERT(CloseServiceHandle(SvcHandle));
    }

    return Result;
}
