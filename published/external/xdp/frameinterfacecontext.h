//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

typedef VOID XDP_FRAME_INTERFACE_CONTEXT;

#define XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME L"ms_frame_interface_context"
#define XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_FRAME_INTERFACE_CONTEXT *
XdpGetFrameInterfaceContextExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_INTERFACE_CONTEXT *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
