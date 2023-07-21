# XSK_BUFFER_DESCRIPTOR structure

A structure for an AF_XDP data frame. Currently unused/reserved.

## Syntax

```C
//
// Descriptor format for RX/TX frames. Each frame consists of one or more
// buffers; any buffer beyond the first buffer is stored on a separate fragment
// buffer ring.
//
typedef struct _XSK_FRAME_DESCRIPTOR {
    //
    // The first buffer in the frame.
    //
    XSK_BUFFER_DESCRIPTOR Buffer;

    //
    // Followed by various descriptor extensions, e.g:
    //
    //   - Additional fragment count
    //   - Offload metadata (e.g. Layout, Checksum, GSO, GRO)
    //
    // To retrieve extensions, use the appropriate extension helper routine.
    //
} XSK_FRAME_DESCRIPTOR;
```

## See Also

[AF_XDP](../afxdp.md)
