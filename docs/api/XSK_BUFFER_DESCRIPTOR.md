# XSK_BUFFER_DESCRIPTOR structure

A structure for an AF_XDP data buffer.

## Syntax

```C
#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MAX 65535ull
#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT 48
#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MASK \
    (XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MAX << XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT)

typedef struct _XSK_BUFFER_DESCRIPTOR {
    // Bits 0:47 encode the address of the chunk, relative to UMEM start address.
    // Bits 48:63 encode the packet offset within the chunk.
    UINT64 Address;
    // Length of the packet.
    UINT32 Length;
    // Must be 0.
    UINT32 Reserved;
} XSK_BUFFER_DESCRIPTOR;
```

## See Also

[AF_XDP](../afxdp.md)
