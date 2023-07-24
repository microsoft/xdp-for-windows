# XSK_BUFFER_DESCRIPTOR structure

A structure for an AF_XDP data buffer.

## Syntax

```C
typedef struct _XSK_BUFFER_DESCRIPTOR {
    XSK_BUFFER_ADDRESS Address;
    UINT32 Length;
    UINT32 Reserved;
} XSK_BUFFER_DESCRIPTOR;
```

## Members

`Address`

A [`XSK_BUFFER_ADDRESS`](XSK_BUFFER_ADDRESS.md) structure specifying the XSK buffer and data offset within the buffer.

`Length`

The length of data within the buffer.

`Reserved`

Reserved, should be set to zero.

## See Also

[AF_XDP](../afxdp.md)
