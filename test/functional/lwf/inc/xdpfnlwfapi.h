//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#ifndef _KERNEL_MODE
#include <xdpndisuser.h>
#endif

HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

EXTERN_C_END
