//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

//
// XDP frame extension for fragmented (multi-buffer) frames.
//
typedef struct _XDP_FRAME_FRAGMENT {
    //
    // The number of additional buffers in the frame. These buffers are stored
    // in a separate fragment ring.
    //
    UINT8 FragmentBufferCount;
} XDP_FRAME_FRAGMENT;

C_ASSERT(sizeof(XDP_FRAME_FRAGMENT) == 1);

#pragma warning(pop)

#define XDP_FRAME_EXTENSION_FRAGMENT_NAME L"ms_frame_fragment"
#define XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

//
// Returns the fragment extension for the given XDP frame.
//
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
