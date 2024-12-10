//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef APIVERSION_H
#define APIVERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define XDP_API_VERSION_1 1
#define XDP_API_VERSION_2 2
#define XDP_API_VERSION_3 3

//
// This version is always the latest supported version.
//
#define XDP_API_VERSION_LATEST XDP_API_VERSION_3

//
// This major.minor.patch version structure is used by native XDP kernel drivers.
//
typedef struct _XDP_VERSION {
    UINT32 Major;
    UINT32 Minor;
    UINT32 Patch;
} XDP_VERSION;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
