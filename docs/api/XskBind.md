# XskBind function

Binds an AF_XDP socket to a network interface queue.

## Syntax

```C
typedef enum _XSK_BIND_FLAGS {
    XSK_BIND_FLAG_NONE = 0x0,

    //
    // The AF_XDP socket is bound to the RX data path.
    //
    XSK_BIND_FLAG_RX = 0x1,

    //
    // The AF_XDP socket is bound to the TX data path.
    //
    XSK_BIND_FLAG_TX = 0x2,

    //
    // The AF_XDP socket is bound using a generic XDP interface provider.
    // This flag cannot be combined with XSK_BIND_FLAG_NATIVE.
    //
    XSK_BIND_FLAG_GENERIC = 0x4,

    //
    // The AF_XDP socket is bound using a native XDP interface provider.
    // This flag cannot be combined with XSK_BIND_FLAG_GENERIC.
    //
    XSK_BIND_FLAG_NATIVE = 0x8,
} XSK_BIND_FLAGS;

typedef
HRESULT
XSK_BIND_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    );
```

## Parameters

TODO

## Remarks

An AF_XDP socket can only be bound to a single network interface queue.

## See Also

[AF_XDP](../afxdp.md)
