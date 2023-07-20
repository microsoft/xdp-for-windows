# XSK_RING_FLAGS enumeration

A set of flags containing information about each AF_XDP shared ring.

## Syntax

```C
typedef enum _XSK_RING_FLAGS {
    XSK_RING_FLAG_NONE = 0x0,

    //
    // The ring is in a terminal state and is no longer usable. This flag is set
    // by the driver; detailed information can be retrieved via:
    //     XSK_SOCKOPT_RX_ERROR
    //     XSK_SOCKOPT_RX_FILL_ERROR
    //     XSK_SOCKOPT_TX_ERROR
    //     XSK_SOCKOPT_TX_COMPLETION_ERROR
    //
    XSK_RING_FLAG_ERROR = 0x1,

    //
    // The driver must be poked in order to make IO progress on this ring. This
    // flag is set by the driver when it is stalled due to lack of IO posted or
    // IO completions consumed by the application. Applications should check for
    // this flag on the RX fill and TX rings after producing on them or after
    // consuming from the TX completion ring. The driver must be poked using
    // XskNotifySocket with the appropriate poke flags. See XskNotifySocket for
    // more information.
    //
    XSK_RING_FLAG_NEED_POKE = 0x2,

    //
    // The processor affinity of this ring has changed. Querying the updated
    // ideal processor via XSK_SOCKOPT_RX_PROCESSOR_AFFINITY for RX rings or
    // XSK_SOCKOPT_TX_PROCESSOR_AFFINITY for TX rings will reset this flag.
    //
    XSK_RING_FLAG_AFFINITY_CHANGED = 0x4,
} XSK_RING_FLAGS;
```

## Values

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)
