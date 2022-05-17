//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

//
// XDP extension containing a TX frame's completion context.
//
typedef struct _XDP_TX_FRAME_COMPLETION_CONTEXT {
    //
    // Contains the TX completion context. The XDP interface provides this
    // completion context back to the XDP platform via the TX completion ring
    // if out-of-order TX completion is enabled.
    //
    VOID *Context;
} XDP_TX_FRAME_COMPLETION_CONTEXT;

C_ASSERT(sizeof(XDP_TX_FRAME_COMPLETION_CONTEXT) == sizeof(VOID *));

#pragma warning(pop)

#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME L"ms_tx_frame_completion_context"
#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

//
// Returns the TX completion extension for the given XDP frame.
//
inline
XDP_TX_FRAME_COMPLETION_CONTEXT *
XdpGetFrameTxCompletionContextExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_TX_FRAME_COMPLETION_CONTEXT *)XdpGetExtensionData(Frame, Extension);
}

//
// Returns the TX completion extension for the given TX completion ring element.
//
inline
XDP_TX_FRAME_COMPLETION_CONTEXT *
XdpGetTxCompletionContextExtension(
    _In_ XDP_TX_FRAME_COMPLETION *Completion,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_TX_FRAME_COMPLETION_CONTEXT *)XdpGetExtensionData(Completion, Extension);
}

EXTERN_C_END
