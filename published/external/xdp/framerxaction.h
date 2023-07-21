//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_FRAME_RX_ACTION {
    UINT8 RxAction;
} XDP_FRAME_RX_ACTION;

C_ASSERT(sizeof(XDP_FRAME_RX_ACTION) == 1);

#pragma warning(pop)

#define XDP_FRAME_EXTENSION_RX_ACTION_NAME L"ms_frame_rx_action"
#define XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_FRAME_RX_ACTION *
XdpGetRxActionExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_RX_ACTION *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
