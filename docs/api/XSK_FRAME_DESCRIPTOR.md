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

## Remarks

Frame descriptors can have optional [descriptor extensions](../descriptor-extensions.md) attached to carry additional metadata. Extensions are enabled per-socket using the `XSK_SOCKOPT_RX_EXTENSION` and `XSK_SOCKOPT_TX_EXTENSION` socket options.

## See Also

[AF_XDP](../afxdp.md)  
[Descriptor Extensions](../descriptor-extensions.md)
