# XDP_TX_FRAME_COMPLETION_CONTEXT structure

An XDP extension containing a TX frame's completion context. This extension is used either on the TX frame ring or the TX completion ring depending on whether the driver supports in-order TX completion.

## Syntax

```C
typedef struct _XDP_TX_FRAME_COMPLETION_CONTEXT {
    //
    // Contains the TX completion context. The XDP interface provides this
    // completion context back to the XDP platform via the TX completion ring
    // if out-of-order TX completion is enabled.
    //
    VOID *Context;
} XDP_TX_FRAME_COMPLETION_CONTEXT;

#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_NAME L"ms_tx_frame_completion_context"
#define XDP_TX_FRAME_COMPLETION_CONTEXT_EXTENSION_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetFrameTxCompletionContextExtension`](XdpGetFrameTxCompletionContextExtension.md)  
[`XdpGetTxCompletionContextExtension`](XdpGetTxCompletionContextExtension.md)  
