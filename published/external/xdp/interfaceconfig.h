//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/apiversion.h>
#include <xdp/driverapi.h>

DECLARE_HANDLE(XDP_INTERFACE_CONFIG);

CONST XDP_VERSION *
XdpGetDriverApiVersion(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    );

#include <xdp/details/interfaceconfig.h>

EXTERN_C_END
