//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#include <msxdp.h>

//
// Trigger a bugcheck in xdp.sys. For testing purposes only.
//
HRESULT
XDPAPI
XdpBugCheck(
    VOID
    );

EXTERN_C_END
