# AF_XDP interface

The [`afxdp.h`](../published/external/afxdp.h) header declares the AF_XDP sockets interface. AF_XDP sockets are used by user mode applications to receive/inspect/drop/send network traffic via XDP hook points. To receive traffic, packets must be steered to a socket by configuring XDP rules/programs using an interface declared elsewhere. Traffic is passed as flat buffer, L2 frames across this interface using single producer, single consumer shared memory rings.

> **TODO** Complete AF_XDP documentation.

## Samples

The [`xskfwd`](../samples/xskfwd/) sample provides a simple echo server using `AF_XDP` sockets.

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
