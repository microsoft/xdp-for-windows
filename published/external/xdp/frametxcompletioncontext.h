//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

//
// XDP frame extension containing a TX completion context.
//
typedef struct _XDP_FRAME_TX_COMPLETION_CONTEXT {
    //
    // Contains the TX completion context. The XDP interface provides this
    // completion context back to the XDP platform via the TX completion ring.
    //
    VOID *Context;
} XDP_FRAME_TX_COMPLETION_CONTEXT;

C_ASSERT(sizeof(XDP_FRAME_TX_COMPLETION_CONTEXT) == sizeof(VOID *));

#pragma warning(pop)

#define XDP_FRAME_EXTENSION_TX_COMPLETION_CONTEXT_NAME L"ms_frame_tx_completion_context"
#define XDP_FRAME_EXTENSION_TX_COMPLETION_CONTEXT_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

//
// Returns the TX completion extension for the given XDP frame.
//
inline
XDP_FRAME_TX_COMPLETION_CONTEXT *
XdpGetFrameTxCompletionContextExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_TX_COMPLETION_CONTEXT *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
