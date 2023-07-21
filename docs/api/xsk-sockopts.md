# AF_XDP socket options

AF_XDP sockets can be configured and queried using socket options.

## Syntax

```C
//
// XSK_SOCKOPT_UMEM_REG
//
// Supports: set
// Optval type: XSK_UMEM_REG
// Description: Register a UMEM to a socket. A socket intending to share an
//              already registered UMEM must not register a UMEM itself.
//

#define XSK_SOCKOPT_UMEM_REG 1

typedef struct _XSK_UMEM_REG {
    UINT64 TotalSize;
    UINT32 ChunkSize;
    UINT32 Headroom;
    VOID *Address;
} XSK_UMEM_REG;

//
// XSK_SOCKOPT_RX_RING_SIZE/XSK_SOCKOPT_TX_RING_SIZE/
// XSK_SOCKOPT_RX_FILL_RING_SIZE/XSK_SOCKOPT_TX_COMPLETION_RING_SIZE
//
// Supports: set
// Optval type: UINT32 (number of descriptors)
// Description: Sets the size of a ring for the socket. The size must be a power
//              of 2. Only one ring of each type may be configured per socket.
//              Rings must be configured in order to use them (the system does
//              not provide a default size).
//

#define XSK_SOCKOPT_RX_RING_SIZE              2
#define XSK_SOCKOPT_RX_FILL_RING_SIZE         3
#define XSK_SOCKOPT_TX_RING_SIZE              4
#define XSK_SOCKOPT_TX_COMPLETION_RING_SIZE   5

//
// XSK_SOCKOPT_RING_INFO
//
// Supports: get
// Optval type: XSK_RING_INFO_SET
// Description: Gets info about all rings associated with a socket. This
//              includes fill and completion rings from shared UMEMs.
//

#define XSK_SOCKOPT_RING_INFO 6

typedef struct _XSK_RING_INFO {
    BYTE *Ring;
    UINT32 DescriptorsOffset;   // XSK_FRAME_DESCRIPTOR[] for rx/tx, UINT64[] for fill/completion
    UINT32 ProducerIndexOffset; // UINT32
    UINT32 ConsumerIndexOffset; // UINT32
    UINT32 FlagsOffset;         // UINT32
    UINT32 Size;
    UINT32 ElementStride;
    UINT32 Reserved;
} XSK_RING_INFO;

typedef struct _XSK_RING_INFO_SET {
    XSK_RING_INFO Fill;
    XSK_RING_INFO Completion;
    XSK_RING_INFO Rx;
    XSK_RING_INFO Tx;
} XSK_RING_INFO_SET;

//
// XSK_SOCKOPT_STATISTICS
//
// Supports: get
// Optval type: XSK_STATISTICS
// Description: Gets statistics from a socket.
//

#define XSK_SOCKOPT_STATISTICS 7

typedef struct _XSK_STATISTICS {
    UINT64 RxDropped;
    UINT64 RxTruncated;
    UINT64 RxInvalidDescriptors;
    UINT64 TxInvalidDescriptors;
} XSK_STATISTICS;

//
// XSK_SOCKOPT_RX_HOOK_ID/XSK_SOCKOPT_TX_HOOK_ID
//
// Supports: get/set
//
// Optval type: XDP_HOOK_ID
// Description: Gets or sets the XDP hook ID for RX/TX binding. If unset, XSK
//              defaults to {XDP_HOOK_L2_RX, XDP_HOOK_INSPECT} for RX and
//              {XDP_HOOK_L2_TX, XDP_HOOK_INJECT} for TX. This option can only
//              be set prior to binding.
//
#define XSK_SOCKOPT_RX_HOOK_ID 8
#define XSK_SOCKOPT_TX_HOOK_ID 9

typedef enum _XSK_ERROR {
    XSK_NO_ERROR                = 0,

    //
    // The queue has been detached from the underlying interface.
    //
    XSK_ERROR_INTERFACE_DETACH  = 0x80000000,

    //
    // The XDP driver detected an invalid ring.
    //
    XSK_ERROR_INVALID_RING      = 0xC0000000,
} XSK_ERROR;

//
// Supports: get
//
// Optval type: XSK_ERROR
// Description: Gets the error code for an XSK ring.
//
#define XSK_SOCKOPT_RX_ERROR              10
#define XSK_SOCKOPT_RX_FILL_ERROR         11
#define XSK_SOCKOPT_TX_ERROR              12
#define XSK_SOCKOPT_TX_COMPLETION_ERROR   13

//
// XSK_SOCKOPT_RX_PROCESSOR_AFFINITY
//
// Supports: get
// Optval type: PROCESSOR_NUMBER
// Description: Returns the ideal processor of the kernel RX data path. This
//              option may return errors, including but not limited to
//              HRESULT_FROM_WIN32(ERROR_NOT_READY) and
//              HRESULT_FROM_WIN32(ERROR_NOT_CAPABLE), when the ideal processor
//              affinity is unknown or unknowable.
//
#define XSK_SOCKOPT_RX_PROCESSOR_AFFINITY 14

//
// XSK_SOCKOPT_TX_PROCESSOR_AFFINITY
//
// Supports: get
// Optval type: PROCESSOR_NUMBER
// Description: Returns the ideal processor of the kernel TX data path. This
//              option may return errors, including but not limited to
//              HRESULT_FROM_WIN32(ERROR_NOT_READY) and
//              HRESULT_FROM_WIN32(ERROR_NOT_CAPABLE), when the ideal processor
//              affinity is unknown or unknowable.
//
#define XSK_SOCKOPT_TX_PROCESSOR_AFFINITY 15
```

## See Also

[AF_XDP](../afxdp.md)  
[XskSetSockopt](XskSetSockopt.md)  
[XskGetSockopt](XskGetSockopt.md)  
[XskIoctl](XskIoctl.md)  
