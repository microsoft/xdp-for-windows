//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

CONST CHAR*
GetPowershellPrefix();

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

BOOLEAN
IsServiceRunning(
    _In_z_ CONST CHAR *ServiceName
    );

BOOLEAN
XdpInstall();

BOOLEAN
XdpUninstall();

#ifdef __cplusplus
} // extern "C"
#endif
