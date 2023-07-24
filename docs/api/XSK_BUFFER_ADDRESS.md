# XSK_BUFFER_ADDRESS structure

A structure containing an XSK buffer address and offset packed into a `UINT64`.

## Syntax

```C
typedef union _XSK_BUFFER_ADDRESS {
    struct {
        UINT64 BaseAddress : 48;
        UINT64 Offset : 16;
    } DUMMYUNIONNAME;
    UINT64 AddressAndOffset;
} XSK_BUFFER_ADDRESS;
```

## Members

`BaseAddress`

The base address of the buffer, relative to the UMEM starting address.

`Offset`

The start of data relative to the start of the XSK buffer.

`AddressAndOffset`

The `BaseAddress` and `Offset` fields packed into a `UINT64`.

## See Also

[AF_XDP](../afxdp.md)  
[`XSK_BUFFER_DESCRIPTOR`](XSK_BUFFER_DESCRIPTOR.md)  
