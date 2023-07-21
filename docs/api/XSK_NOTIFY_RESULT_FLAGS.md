# XSK_NOTIFY_FLAGS enumeration

A set of flags representing the result of a AF_XDP notify request.

## Syntax

```C
typedef enum _XSK_NOTIFY_RESULT_FLAGS {
    XSK_NOTIFY_RESULT_FLAG_NONE = 0x0,

    //
    // RX ring entry is available.
    //
    XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE = 0x1,

    //
    // TX completion ring entry is available.
    //
    XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE = 0x2,
} XSK_NOTIFY_RESULT_FLAGS;
```

## Values

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)  
[`XskNotifySocket``](XskNotifySocket.md)  
[`XskGetNotifyAsyncResult`](XskGetNotifyAsyncResult.md)  
