//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "generic.h"
#include "native.h"
#include "offload.h"

typedef enum _XDP_LWF_FILTER_STATE {
    FilterStateUnspecified,
    FilterPaused,
    FilterPausing,
    FilterRunning,
} XDP_LWF_FILTER_STATE;

typedef struct _XDP_LWF_FILTER {
    NDIS_HANDLE NdisFilterHandle;
    NET_IFINDEX MiniportIfIndex;
    XDP_LWF_FILTER_STATE NdisState;
    XDPIF_INTERFACE_SET_HANDLE XdpIfInterfaceSetHandle;

    XDP_LWF_OFFLOAD Offload;

    XDP_LWF_GENERIC Generic;
    XDP_LWF_NATIVE Native;
} XDP_LWF_FILTER;

#define POOLTAG_BUFFER              'BfdX'      // XdfB
#define POOLTAG_FILTER              'FfdX'      // XdfF
#define POOLTAG_NATIVE              'NfdX'      // XdfN
#define POOLTAG_OID                 'OfdX'      // XdfO
#define POOLTAG_OFFLOAD             'ofdX'      // Xdfo
#define POOLTAG_RECV                'rfdX'      // Xdfr
#define POOLTAG_RSS                 'RfdX'      // XdfR
#define POOLTAG_SEND                'SfdX'      // XdfS
