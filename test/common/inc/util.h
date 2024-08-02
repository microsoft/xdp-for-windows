//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define XDP_SERVICE_NAME "xdp"

CONST CHAR*
GetPowershellPrefix();

HRESULT
GetCurrentBinaryFileName(
    _Out_ CHAR *Path,
    _In_ UINT32 PathSize
    );

HRESULT
GetCurrentBinaryPath(
    _Out_ CHAR *Path,
    _In_ UINT32 PathSize
    );

_Success_(return==0)
DWORD
ConvertInterfaceAliasToIndex(
    _In_ CONST WCHAR *Alias,
    _Out_ ULONG *IfIndex
    );

BOOLEAN
IsServiceInstalled(
    _In_z_ CONST CHAR *ServiceName
    );

HRESULT
GetServiceState(
    _Out_ UINT32 *ServiceState,
    _In_z_ CONST CHAR *ServiceName
    );

HRESULT
StartServiceAsync(
    _In_z_ CONST CHAR *ServiceName
    );

HRESULT
StopServiceAsync(
    _In_z_ CONST CHAR *ServiceName
    );

BOOLEAN
XdpInstall();

BOOLEAN
XdpUninstall();

#ifdef __cplusplus
} // extern "C"
#endif
