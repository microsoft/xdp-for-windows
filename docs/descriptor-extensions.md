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

### Frame Extensions

Frame extensions attach metadata to complete packets (frames):

| Extension | Name | Structure | Purpose |
|-----------|------|-----------|---------|
| Fragment | `ms_frame_fragment` | [`XDP_FRAME_FRAGMENT`](api/XDP_FRAME_FRAGMENT.md) | Number of additional buffers in multi-buffer frames |
| Timestamp | `ms_frame_timestamp` | (Experimental) | Hardware timestamp of packet arrival |
| Layout | `ms_frame_layout` | [`XDP_FRAME_LAYOUT`](api/XDP_FRAME_LAYOUT.md) | Protocol header layout (IP version, protocol, offsets) |
| Checksum | `ms_frame_checksum` | [`XDP_FRAME_CHECKSUM`](api/XDP_FRAME_CHECKSUM.md) | Checksum offload information |
| RX Action | `ms_frame_rx_action` | [`XDP_FRAME_RX_ACTION`](api/XDP_FRAME_RX_ACTION.md) | RX action for eBPF programs (drop, pass, TX) |
| Interface Context | `ms_frame_interface_context` | [`XDP_FRAME_INTERFACE_CONTEXT`](api/XDP_FRAME_INTERFACE_CONTEXT.md) | Driver-specific frame context |

### Buffer Extensions

Buffer extensions attach metadata to individual buffers within a frame:

| Extension | Name | Structure | Purpose |
|-----------|------|-----------|---------|
| Virtual Address | `ms_buffer_virtual_address` | [`XDP_BUFFER_VIRTUAL_ADDRESS`](api/XDP_BUFFER_VIRTUAL_ADDRESS.md) | Virtual address mapping of the buffer |
| Logical Address | `ms_buffer_logical_address` | [`XDP_BUFFER_LOGICAL_ADDRESS`](api/XDP_BUFFER_LOGICAL_ADDRESS.md) | DMA logical address of the buffer |
| MDL | `ms_buffer_mdl` | [`XDP_BUFFER_MDL`](api/XDP_BUFFER_MDL.md) | Memory Descriptor List for the buffer |
| Interface Context | `ms_buffer_interface_context` | [`XDP_BUFFER_INTERFACE_CONTEXT`](api/XDP_BUFFER_INTERFACE_CONTEXT.md) | Driver-specific buffer context |

### TX Completion Extensions

TX completion extensions attach metadata to TX completion descriptors (for out-of-order TX completion):

| Extension | Name | Structure | Purpose |
|-----------|------|-----------|---------|
| Completion Context | `ms_tx_frame_completion_context` | [`XDP_TX_FRAME_COMPLETION_CONTEXT`](api/XDP_TX_FRAME_COMPLETION_CONTEXT.md) | Application context for TX completion |

## Usage Patterns

### AF_XDP Socket Applications

AF_XDP applications declare which extensions they need using the `XSK_SOCKOPT_RX_EXTENSION` and `XSK_SOCKOPT_TX_EXTENSION` socket options:

```c
// Declare support for fragment extension on RX
XDP_EXTENSION_INFO rxExtensionInfo;
XdpInitializeExtensionInfo(
    &rxExtensionInfo,
    XDP_FRAME_EXTENSION_FRAGMENT_NAME,
    XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1,
    XDP_EXTENSION_TYPE_FRAME);

UINT32 result = XskSetSockopt(
    socket,
    XSK_SOCKOPT_RX_EXTENSION,
    &rxExtensionInfo,
    sizeof(rxExtensionInfo));

// Retrieve the extension handle after binding
XDP_EXTENSION fragmentExtension;
UINT32 extensionSize = sizeof(fragmentExtension);
XskGetSockopt(
    socket,
    XSK_SOCKOPT_RX_EXTENSION,
    &rxExtensionInfo,
    &extensionSize);

// Extract the extension from the rxExtensionInfo.Extension field
```

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
