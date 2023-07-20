//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_TX_FRAME_COMPLETION_CONTEXT {
    VOID *Context;
} XDP_TX_FRAME_COMPLETION_CONTEXT;

C_ASSERT(sizeof(XDP_TX_FRAME_COMPLETION_CONTEXT) == sizeof(VOID *));

#pragma warning(pop)

#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME L"ms_tx_frame_completion_context"
#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_TX_FRAME_COMPLETION_CONTEXT *
XdpGetFrameTxCompletionContextExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_TX_FRAME_COMPLETION_CONTEXT *)XdpGetExtensionData(Frame, Extension);
}

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
