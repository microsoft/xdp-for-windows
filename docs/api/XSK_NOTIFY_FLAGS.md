# XSK_NOTIFY_FLAGS enumeration

A set of flags specifying the actions to perform in an AF_XDP notify request.

## Syntax

```C
typedef enum _XSK_NOTIFY_FLAGS {
    XSK_NOTIFY_FLAG_NONE = 0x0,

    //
    // Poke the driver to perform RX. Apps poke RX when entries are produced
    // on the RX fill ring and the RX fill ring is marked as needing a poke.
    //
    XSK_NOTIFY_FLAG_POKE_RX = 0x1,

    //
    // Poke the driver to perform TX. Apps poke TX when entries are produced
    // on the TX ring or consumed from the TX completion ring and the TX
    // ring is marked as needing a poke.
    //
    XSK_NOTIFY_FLAG_POKE_TX = 0x2,

    //
    // Wait until a RX ring entry is available.
    //
    XSK_NOTIFY_FLAG_WAIT_RX = 0x4,

    //
    // Wait until a TX completion ring entry is available.
    //
    XSK_NOTIFY_FLAG_WAIT_TX = 0x8,
} XSK_NOTIFY_FLAGS;
```

## Values

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)  
[`XskNotifySocket`](XskNotifySocket.md)  
[`XskNotifyAsync`](XskNotifyAsync.md)  
