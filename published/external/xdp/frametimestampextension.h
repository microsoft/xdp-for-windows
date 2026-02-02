//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/frametimestamp.h>

#define XDP_FRAME_EXTENSION_TIMESTAMP_NAME L"ms_frame_timestamp"
#define XDP_FRAME_EXTENSION_TIMESTAMP_VERSION_1 1U

#include <xdp/extension.h>
#include <xdp/datapath.h>

inline
XDP_FRAME_TIMESTAMP *
XdpGetTimestampExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_TIMESTAMP *)XdpGetExtensionData(Frame, Extension);
}

inline
XDP_FRAME_TIMESTAMP *
XdpGetTxCompletionTimestampExtension(
    _In_ XDP_TX_FRAME_COMPLETION *Completion,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_TIMESTAMP *)XdpGetExtensionData(Completion, Extension);
}

EXTERN_C_END
