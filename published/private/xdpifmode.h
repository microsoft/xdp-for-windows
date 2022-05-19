//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_IF_MODE_H
#define XDP_IF_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _XDP_INTERFACE_MODE {
    //
    // The XDP data path is provided by the XDP platform. This mode does not
    // require changes by network drivers to support XDP.
    //
    XDP_INTERFACE_MODE_GENERIC,

    //
    // XDP data path is provided by a NIC driver natively aware of XDP.
    //
    XDP_INTERFACE_MODE_NATIVE,
} XDP_INTERFACE_MODE;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
