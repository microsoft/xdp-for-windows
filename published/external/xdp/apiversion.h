//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef APIVERSION_H
#define APIVERSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XDP_VERSION {
    UINT32 Major;
    UINT32 Minor;
    UINT32 Patch;
} XDP_VERSION;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
