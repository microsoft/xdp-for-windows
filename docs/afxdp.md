# AF_XDP interface

The [`afxdp.h`](../published/external/afxdp.h) header declares the AF_XDP sockets interface. AF_XDP sockets are used by user mode applications to receive/inspect/drop/send network traffic via XDP hook points. To receive traffic, packets must be steered to a socket by configuring XDP rules/programs using an interface declared elsewhere. Traffic is passed as flat buffer, L2 frames across this interface using single producer, single consumer shared memory rings.

## Overview

AF_XDP provides a high-performance packet interface that allows user-mode applications to bypass the traditional Windows networking stack. Similar to Linux AF_XDP, it uses shared memory rings for zero-copy packet transfer between kernel and user space.

### Key Features

- **Zero-copy packet I/O**: Direct memory access to packet buffers eliminates costly data copying
- **High throughput**: Designed for applications requiring millions of packets per second
- **Low latency**: Minimal kernel involvement in the data path
- **L2 frame access**: Applications receive raw Ethernet frames with full control over packet processing
- **Flexible steering**: Packets can be directed to specific sockets using XDP programs or rules

## Basic Usage

To use AF_XDP sockets, include the following headers:

```c
#include <afxdp.h>        // AF_XDP sockets API
#include <xdpapi.h>       // XDP API for program management
#include <afxdp_helper.h> // Optional helper functions
```

### Socket Lifecycle

1. **Create** an AF_XDP socket using `XskCreate()`
2. **Bind** the socket to a network interface and queue using `XskBind()`
3. **Activate** the socket to begin packet transfer using `XskActivate()`
4. **Process** packets using the shared memory rings
5. **Close** the socket when done

### Shared Memory Rings

AF_XDP uses four shared memory rings for packet transfer:

- **RX Ring**: Contains descriptors for incoming packets
- **TX Ring**: Contains descriptors for outgoing packets  
- **Fill Ring**: Provides buffers for incoming packets
- **Completion Ring**: Returns buffers after packet transmission

## Programming Model

### Receiving Packets

```c
// Reserve space in the RX ring
UINT32 Index;
if (XskRingConsumerReserve(&RxRing, 1, &Index) == 1) {
    // Get the packet descriptor
    XSK_BUFFER_DESCRIPTOR *Buffer = XskRingGetElement(&RxRing, Index);
    
    // Process the packet data
    UCHAR *PacketData = XskDescriptorGetAddress(Buffer);
    UINT32 PacketLength = XskDescriptorGetLength(Buffer);
    
    // Release the descriptor back to the kernel
    XskRingConsumerRelease(&RxRing, 1);
}
```

### Sending Packets

```c
// Reserve space in the TX ring
UINT32 Index;
if (XskRingProducerReserve(&TxRing, 1, &Index) == 1) {
    // Get a buffer descriptor
    XSK_BUFFER_DESCRIPTOR *Buffer = XskRingGetElement(&TxRing, Index);
    
    // Set packet data and length
    XskDescriptorSetAddress(Buffer, PacketAddress);
    XskDescriptorSetLength(Buffer, PacketLength);
    
    // Submit the packet for transmission
    XskRingProducerSubmit(&TxRing, 1);
}
```

## Performance Considerations

- **Batch processing**: Process multiple packets per ring operation for better performance
- **Memory alignment**: Ensure packet buffers are properly aligned for optimal access
- **Ring sizing**: Choose appropriate ring sizes based on expected traffic patterns
- **CPU affinity**: Pin threads to specific CPUs to avoid context switching overhead

## Samples

The following samples demonstrate different AF_XDP use cases:

- **[`xskfwd`](../samples/xskfwd/)** - Simple echo server that forwards packets back to sender using AF_XDP sockets
- **[`xdpcap`](../samples/xdpcap/)** - Packet capture tool that demonstrates traffic monitoring and analysis using AF_XDP

## See Also

[`XSK_BUFFER_ADDRESS`](api/XSK_BUFFER_ADDRESS.md)  
[`XSK_BUFFER_DESCRIPTOR`](api/XSK_BUFFER_DESCRIPTOR.md)  
[`XSK_FRAME_DESCRIPTOR`](api/XSK_FRAME_DESCRIPTOR.md)  
[`XSK_NOTIFY_FLAGS`](api/XSK_NOTIFY_FLAGS.md)  
[`XSK_NOTIFY_RESULT_FLAGS`](api/XSK_NOTIFY_RESULT_FLAGS.md)  
[`XSK_RING_FLAGS`](api/XSK_RING_FLAGS.md)  
[xsk-sockopts](api/xsk-sockopts.md)  
[`XskActivate`](api/XskActivate.md)  
[`XskBind`](api/XskBind.md)  
[`XskCreate`](api/XskCreate.md)  
[`XskGetNotifyAsyncResult`](api/XskGetNotifyAsyncResult.md)  
[`XskGetSockopt`](api/XskGetSockopt.md)  
[`XskIoctl`](api/XskIoctl.md)  
[`XskNotifyAsync`](api/XskNotifyAsync.md)  
[`XskNotifySocket`](api/XskNotifySocket.md)  
[`XskSetSockopt`](api/XskSetSockopt.md)  
