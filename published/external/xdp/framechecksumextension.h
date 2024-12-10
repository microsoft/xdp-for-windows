//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/framechecksum.h>

#define XDP_FRAME_EXTENSION_CHECKSUM_NAME L"ms_frame_checksum"
#define XDP_FRAME_EXTENSION_CHECKSUM_VERSION_1 1U

#include <xdp/extension.h>

inline
XDP_FRAME_CHECKSUM *
XdpGetChecksumExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_CHECKSUM *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
