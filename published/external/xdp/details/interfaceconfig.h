//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#include <xdp/interfaceconfig.h>

typedef struct _XDP_INTERFACE_CONFIG_DISPATCH {
    UINT32 Size;
} XDP_INTERFACE_CONFIG_DISPATCH;

typedef struct _XDP_INTERFACE_CONFIG_DETAILS {
    CONST XDP_INTERFACE_CONFIG_DISPATCH     *Dispatch;
} XDP_INTERFACE_CONFIG_DETAILS;

EXTERN_C_END
