//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_EXTENSION_H
#define XDP_EXTENSION_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_EXTENSION {
    //
    // This field is reserved for XDP platform use.
    //
    UINT16 Reserved;
} XDP_EXTENSION;

#ifdef __cplusplus
static_assert(sizeof(XDP_EXTENSION) == 2, "XDP_EXTENSION must be exactly 2 bytes");
#else
_Static_assert(sizeof(XDP_EXTENSION) == 2, "XDP_EXTENSION must be exactly 2 bytes");
#endif

#pragma warning(pop)

#include <xdp/details/extension.h>

#ifdef __cplusplus
} // extern "C"
#endif

#endif
