//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_EXPORT_H
#define XDP_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XDPEXPORT
#define XDPEXPORT(X) X
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
