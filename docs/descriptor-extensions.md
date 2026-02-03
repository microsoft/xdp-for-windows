# XDP Descriptor Extensions

## Overview

Descriptor extensions are a core feature of the XDP for Windows data path that enable variable-sized metadata to be attached to frame and buffer descriptors. They provide a flexible, extensible mechanism for passing additional information alongside packet data without modifying the base descriptor structures.

## What Are Descriptor Extensions?

Descriptor extensions are optional metadata structures that are stored contiguously after base descriptors in memory. Each descriptor (frame, buffer, or TX completion) can have zero or more extensions enabled. Extensions allow the data path to carry information such as:

- Packet timestamps
- Checksum offload data
- Fragment buffer counts for multi-buffer frames
- Protocol layout information
- Interface-specific context
- Memory mapping details (virtual addresses, logical addresses, MDLs)

### Key Characteristics

- **Optional**: Extensions are only allocated when needed
- **Dynamic**: The set of enabled extensions can vary per queue/socket
- **Extensible**: New extensions can be added without breaking existing code
- **Efficient**: Extensions are stored inline with descriptors for cache efficiency
- **Type-safe**: Each extension has a specific type and version

## Why Use Descriptor Extensions?

Descriptor extensions provide several important benefits:

1. **Backward Compatibility**: Base descriptor structures remain unchanged, ensuring compatibility across versions
2. **Memory Efficiency**: Only the extensions actually needed are allocated
3. **Performance**: Inline storage provides cache-friendly access patterns
4. **Flexibility**: Different sockets/queues can enable different sets of extensions
5. **Extensibility**: New hardware offloads and features can be added without API changes

Without extensions, the only alternatives would be:

- Fixed-size descriptors large enough for all possible metadata (wastes memory)
- Out-of-band metadata structures (cache-unfriendly, complex lifetime management)
- Versioned descriptor structures (breaks compatibility, requires multiple code paths)

## Extension Architecture

### Extension Types

XDP defines three types of descriptor extensions based on the descriptor they attach to:

| Extension Type | Descriptor | Purpose |
|----------------|------------|---------|
| `XDP_EXTENSION_TYPE_FRAME` | `XDP_FRAME` / `XSK_FRAME_DESCRIPTOR` | Frame-level metadata (timestamps, checksums, layout, etc.) |
| `XDP_EXTENSION_TYPE_BUFFER` | `XDP_BUFFER` / `XSK_BUFFER_DESCRIPTOR` | Buffer-level metadata (addresses, MDLs, etc.) |
| `XDP_EXTENSION_TYPE_TX_FRAME_COMPLETION` | `XDP_TX_FRAME_COMPLETION` | TX completion metadata |

### Memory Layout

Descriptors with extensions are laid out contiguously in memory:

```
+------------------+
| Base Descriptor  |  (e.g., XDP_FRAME or XSK_FRAME_DESCRIPTOR)
+------------------+
| Extension 1      |  (e.g., XDP_FRAME_FRAGMENT)
+------------------+
| Extension 2      |  (e.g., XDP_FRAME_LAYOUT)
+------------------+
| Extension 3      |  (e.g., XDP_FRAME_CHECKSUM)
+------------------+
```

The `ElementStride` field in `XDP_RING` reflects the total size of the descriptor plus all enabled extensions.

### Accessing Extensions

Extensions are accessed using the `XdpGetExtensionData()` helper function and an `XDP_EXTENSION` handle:

```c
// Query for an extension during initialization
XDP_EXTENSION_INFO extensionInfo;
XdpInitializeExtensionInfo(
    &extensionInfo,
    XDP_FRAME_EXTENSION_FRAGMENT_NAME,
    XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1,
    XDP_EXTENSION_TYPE_FRAME);

// Get the extension handle (via XskGetSockopt or driver API)
XDP_EXTENSION fragmentExtension;
// ... (obtain fragmentExtension via sockopt or driver API) ...

// Access the extension data for a specific frame
XDP_FRAME_FRAGMENT *fragment = XdpGetFragmentExtension(frame, &fragmentExtension);
if (fragment->FragmentBufferCount > 0) {
    // Frame has additional buffers
}
```

The `XDP_EXTENSION` structure contains an offset that is added to the descriptor pointer to locate the extension data. This offset is populated by the XDP platform when the extension is queried.

## Available Extensions

XDP for Windows provides built-in extensions for common use cases. Extensions are categorized by the descriptor type they attach to:

- **Frame extensions**: Attach metadata to complete packets (e.g., fragment counts, checksums, timestamps, protocol layout)
- **Buffer extensions**: Attach metadata to individual buffers (e.g., virtual/logical addresses, MDLs)
- **TX completion extensions**: Attach metadata to TX completion descriptors

Refer to the individual extension API documentation for details on specific extensions.

## Usage Patterns

### AF_XDP Socket Applications

For AF_XDP applications, all V1 extensions are implicitly enabled by the AF_XDP subsystem. Applications can access extension data directly using the extension getter functions:

```c
// Get a frame descriptor from the RX ring
XSK_FRAME_DESCRIPTOR *frameDesc = /* ... get from RX ring ... */;

// Extensions are implicitly available - use getter functions to access them
// Note: Extension handles and offsets are managed internally by AF_XDP V1
XDP_FRAME_FRAGMENT *fragment = (XDP_FRAME_FRAGMENT *)
    ((BYTE *)frameDesc + /* fragment extension offset */);

// Check if frame has additional buffers
if (fragment->FragmentBufferCount > 0) {
    // Frame has additional buffers in the fragment ring
}
```

Note: AF_XDP V1 applications rely on the implicit extension layout. Extension offsets are determined by the ring's `ElementStride` and the fixed V1 extension ordering.

### XDP Drivers

XDP drivers (miniport, LWF) query for extensions during queue initialization and use them during data path processing:

```c
// During queue initialization, query for supported extensions
XDP_EXTENSION_INFO extensionInfo;
XdpInitializeExtensionInfo(
    &extensionInfo,
    XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
    XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
    XDP_EXTENSION_TYPE_BUFFER);

// Get the extension from the queue
XDP_EXTENSION virtualAddressExtension;
// ... (driver API to get extension) ...

// During RX processing, populate the extension
XDP_BUFFER_VIRTUAL_ADDRESS *va = 
    XdpGetVirtualAddressExtension(buffer, &virtualAddressExtension);
va->VirtualAddress = bufferVa;
```

## Best Practices

1. **Query extensions once**: Extension handles should be obtained during initialization and cached, not queried per-packet
2. **Check availability**: Not all extensions may be available in all configurations; check return values
3. **Version compatibility**: Always specify the extension version you support
4. **Minimal extensions**: Only enable extensions you actually use to minimize memory overhead
5. **Cache-friendly access**: Access extensions in a predictable pattern to maximize CPU cache efficiency

## See Also

- [AF_XDP Interface](afxdp.md)
- [`XDP_EXTENSION`](api/XDP_EXTENSION.md) - Base extension handle structure
- [`XDP_EXTENSION_INFO`](api/XDP_EXTENSION_INFO.md) - Extension registration information
- [`XdpInitializeExtensionInfo`](api/XdpInitializeExtensionInfo.md) - Extension initialization helper
- [Driver Data Path](api/driver-datapath.md) - Driver API data path details
- [xsk-sockopts](api/xsk-sockopts.md) - AF_XDP socket options including extension registration
