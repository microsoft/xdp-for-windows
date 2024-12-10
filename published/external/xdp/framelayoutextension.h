//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/framelayout.h>

#define XDP_FRAME_EXTENSION_LAYOUT_NAME L"ms_frame_layout"
#define XDP_FRAME_EXTENSION_LAYOUT_VERSION_1 1U

#include <xdp/extension.h>

inline
XDP_FRAME_LAYOUT *
XdpGetLayoutExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_LAYOUT *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
