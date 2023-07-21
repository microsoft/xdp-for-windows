# XDP_RULE_ACTION structure

Specifies an XDP inspection program rule action.

## Syntax

```C
typedef enum _XDP_RULE_ACTION {
    //
    // Frame must be dropped.
    //
    XDP_PROGRAM_ACTION_DROP,
    //
    // Frame must be allowed to continue.
    //
    XDP_PROGRAM_ACTION_PASS,
    //
    // Frame must be redirected to the target specified in XDP_REDIRECT_PARAMS.
    //
    XDP_PROGRAM_ACTION_REDIRECT,
    //
    // The frame's ethernet source and destination addresses are swapped and the
    // frame is directed onto the return path. For native XDP drivers, this
    // results in an XDP_RX_ACTION_TX.
    //
    XDP_PROGRAM_ACTION_L2FWD,
    //
    // Reserved for internal use: the action is determined by the specified
    // eBPF program.
    //
    XDP_PROGRAM_ACTION_EBPF,
} XDP_RULE_ACTION;

//
// Target types for a redirect action.
//
typedef enum _XDP_REDIRECT_TARGET_TYPE {
    //
    // Redirect frames to an XDP socket.
    //
    XDP_REDIRECT_TARGET_TYPE_XSK,
} XDP_REDIRECT_TARGET_TYPE;

typedef struct _XDP_REDIRECT_PARAMS {
    XDP_REDIRECT_TARGET_TYPE TargetType;
    HANDLE Target;
} XDP_REDIRECT_PARAMS;

//
// Reserved.
//
typedef struct _XDP_EBPF_PARAMS {
    HANDLE Target;
} XDP_EBPF_PARAMS;
```

## Members

TODO

## Remarks

TODO
