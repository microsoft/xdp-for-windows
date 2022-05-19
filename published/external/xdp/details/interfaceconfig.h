//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/control.h>
#include <xdp/interfaceconfig.h>
#include <xdp/details/export.h>
#include <xdp/objectheader.h>

typedef
CONST XDP_VERSION *
XDP_GET_DRIVER_API_VERSION(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    );

typedef struct _XDP_INTERFACE_CONFIG_DISPATCH {
    XDP_OBJECT_HEADER           Header;
    XDP_GET_DRIVER_API_VERSION  *GetDriverApiVersion;
} XDP_INTERFACE_CONFIG_DISPATCH;

#define XDP_INTERFACE_CONFIG_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_INTERFACE_CONFIG_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_INTERFACE_CONFIG_DISPATCH, GetDriverApiVersion)

typedef struct _XDP_INTERFACE_CONFIG_DETAILS {
    CONST XDP_INTERFACE_CONFIG_DISPATCH     *Dispatch;
} XDP_INTERFACE_CONFIG_DETAILS;

inline
CONST XDP_VERSION *
XDPEXPORT(XdpGetDriverApiVersion)(
    _In_ XDP_INTERFACE_CONFIG InterfaceConfig
    )
{
    XDP_INTERFACE_CONFIG_DETAILS *Details =
        (XDP_INTERFACE_CONFIG_DETAILS *)InterfaceConfig;
    return Details->Dispatch->GetDriverApiVersion(InterfaceConfig);
}

EXTERN_C_END
