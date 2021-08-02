//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

CONST CHAR*
GetPowershellPrefix();

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

EXTERN_C_END
