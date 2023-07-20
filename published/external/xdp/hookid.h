//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef HOOKID_H
#define HOOKID_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _XDP_HOOK_LAYER {
    XDP_HOOK_L2,
} XDP_HOOK_LAYER;

typedef enum _XDP_HOOK_DATAPATH_DIRECTION {
    XDP_HOOK_RX,
    XDP_HOOK_TX,
} XDP_HOOK_DATAPATH_DIRECTION;

typedef enum _XDP_HOOK_SUBLAYER {
    XDP_HOOK_INSPECT,
    XDP_HOOK_INJECT,
} XDP_HOOK_SUBLAYER;

typedef struct _XDP_HOOK_ID {
    XDP_HOOK_LAYER Layer;
    XDP_HOOK_DATAPATH_DIRECTION Direction;
    XDP_HOOK_SUBLAYER SubLayer;
} XDP_HOOK_ID;

C_ASSERT(
    sizeof(XDP_HOOK_ID) ==
        sizeof(XDP_HOOK_LAYER) +
        sizeof(XDP_HOOK_DATAPATH_DIRECTION) +
        sizeof(XDP_HOOK_SUBLAYER));

#ifdef __cplusplus
} // extern "C"
#endif

#endif
