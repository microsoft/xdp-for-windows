//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_FRAME_FRAGMENT {
    UINT8 FragmentBufferCount;
} XDP_FRAME_FRAGMENT;

#ifdef __cplusplus
static_assert(sizeof(XDP_FRAME_FRAGMENT) == 1, "XDP_FRAME_FRAGMENT must be exactly 1 byte");
#else
_Static_assert(sizeof(XDP_FRAME_FRAGMENT) == 1, "XDP_FRAME_FRAGMENT must be exactly 1 byte");
#endif

#pragma warning(pop)

#define XDP_FRAME_EXTENSION_FRAGMENT_NAME L"ms_frame_fragment"
#define XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_FRAME_FRAGMENT *
XdpGetFragmentExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_FRAGMENT *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
