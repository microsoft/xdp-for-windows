//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    XDPFNLWF_OPEN_DEFAULT *OpenDefault;
    CHAR EaBuffer[XDPFNLWF_OPEN_EA_LENGTH + sizeof(*OpenDefault)];

    OpenDefault = FnLwfInitializeEa(XDPFNLWF_FILE_TYPE_DEFAULT, EaBuffer, sizeof(EaBuffer));
    OpenDefault->IfIndex = IfIndex;

    return FnLwfOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer), Handle);
}
