//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

typedef VOID XDP_BUFFER_INTERFACE_CONTEXT;

#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME L"ms_buffer_interface_context"
#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_BUFFER_INTERFACE_CONTEXT *
XdpGetBufferInterfaceContextExtension(
    _In_ XDP_BUFFER *Buffer,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_BUFFER_INTERFACE_CONTEXT *)XdpGetExtensionData(Buffer, Extension);
}

EXTERN_C_END
