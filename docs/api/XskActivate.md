# XskActivate function

Activate the data path of an AF_XDP socket.

## Syntax

```C
typedef enum _XSK_ACTIVATE_FLAGS {
    XSK_ACTIVATE_FLAG_NONE = 0x0,
} XSK_ACTIVATE_FLAGS;

typedef
HRESULT
XSK_ACTIVATE_FN(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    );
```

## Parameters

TODO

## Remarks

Activate the data path of an AF_XDP socket. An AF_XDP socket cannot send or
receive data until it is successfully activated. An AF_XDP socket can only be
activated after it has been successfully bound.

Before calling XskActivate:

- The socket object must have at least TX + TX completion and RX + RX fill
rings configured if bound to the TX and RX data paths, respectively.
- The socket object must have registered or shared a UMEM.

## See Also

[AF_XDP](../afxdp.md)
