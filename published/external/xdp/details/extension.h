//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_EXTENSION_DETAILS_H
#define XDP_EXTENSION_DETAILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xdp/extension.h>

inline
VOID *
XdpGetExtensionData(
    _In_ VOID *Descriptor,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (VOID *)((UCHAR *)Descriptor + Extension->Reserved);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
