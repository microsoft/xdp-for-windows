//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This header declares the AF_XDP sockets interface. AF_XDP sockets are used by
// user mode applications to receive/inspect/drop/send network traffic via XDP
// hook points. To receive traffic, packets must be steered to a socket by
// configuring XDP rules/programs using an interface declared elsewhere. Traffic
// is passed as flat buffer, L2 frames across this interface using single
// producer, single consumer shared memory rings.
//

#ifndef AFXDP_H
#define AFXDP_H

#include <xdp/hookid.h>

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

//
// Descriptor format for RX/TX rings.
//

#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MAX 65535ull
#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT 48
#define XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MASK \
    (XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MAX << XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT)

typedef struct _XSK_BUFFER_DESCRIPTOR {
    // Bits 0:47 encode the address of the chunk, relative to UMEM start address.
    // Bits 48:63 encode the packet offset within the chunk.
    UINT64 address;
    // Length of the packet.
    UINT32 length;
    // Must be 0.
    UINT32 reserved;
} XSK_BUFFER_DESCRIPTOR;

//
// Descriptor format for RX/TX frames. Each frame consists of one or more
// buffers; any buffer beyond the first buffer is stored on a separate fragment
// buffer ring.
//
typedef struct _XSK_FRAME_DESCRIPTOR {
    //
    // The first buffer in the frame.
    //
    XSK_BUFFER_DESCRIPTOR buffer;

    //
    // Followed by various descriptor extensions, e.g:
    //
    //   - Additional fragment count
    //   - Offload metadata (e.g. Layout, Checksum, GSO, GRO)
    //
    // To retrieve extensions, use the appropriate extension helper routine.
    //
} XSK_FRAME_DESCRIPTOR;

//
// Ensure frame-unaware apps can treat the frame ring as a buffer ring.
//
C_ASSERT(FIELD_OFFSET(XSK_FRAME_DESCRIPTOR, buffer) == 0);

typedef enum _XSK_RING_FLAGS {
    XSK_RING_FLAG_NONE = 0x0,

    //
    // The ring is in a terminal state and is no longer usable. This flag is set
    // by the driver; detailed information can be retrieved via:
    //     XSK_SOCKOPT_RX_ERROR
    //     XSK_SOCKOPT_RX_FILL_ERROR
    //     XSK_SOCKOPT_TX_ERROR
    //     XSK_SOCKOPT_TX_COMPLETION_ERROR
    //
    XSK_RING_FLAG_ERROR = 0x1,

    //
    // The driver must be poked in order to make IO progress on this ring. This
    // flag is set by the driver when it is stalled due to lack of IO posted or
    // IO completions consumed by the application. Applications should check for
    // this flag on the RX fill and TX rings after producing on them or after
    // consuming from the TX completion ring. The driver must be poked using
    // XskNotifySocket with the appropriate poke flags. See XskNotifySocket for
    // more information.
    //
    XSK_RING_FLAG_NEED_POKE = 0x2,

    //
    // The processor affinity of this ring has changed. Querying the updated
    // ideal processor via XSK_SOCKOPT_RX_PROCESSOR_AFFINITY for RX rings or
    // XSK_SOCKOPT_TX_PROCESSOR_AFFINITY for TX rings will reset this flag.
    //
    XSK_RING_FLAG_AFFINITY_CHANGED = 0x4,
} XSK_RING_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_RING_FLAGS);
C_ASSERT(sizeof(XSK_RING_FLAGS) == sizeof(UINT32));

//
// XskCreate
//
// Creates an AF_XDP socket object and returns a handle to it.
// To close the socket object, call CloseHandle.
//
typedef
HRESULT
XSK_CREATE_FN(
    _Out_ HANDLE* socket
    );

typedef enum _XSK_BIND_FLAGS {
    XSK_BIND_FLAG_NONE = 0x0,

    //
    // The AF_XDP socket is bound to the RX data path.
    //
    XSK_BIND_FLAG_RX = 0x1,

    //
    // The AF_XDP socket is bound to the TX data path.
    //
    XSK_BIND_FLAG_TX = 0x2,

    //
    // The AF_XDP socket is bound using a generic XDP interface provider.
    // This flag cannot be combined with XSK_BIND_FLAG_NATIVE.
    //
    XSK_BIND_FLAG_GENERIC = 0x4,

    //
    // The AF_XDP socket is bound using a native XDP interface provider.
    // This flag cannot be combined with XSK_BIND_FLAG_GENERIC.
    //
    XSK_BIND_FLAG_NATIVE = 0x8,
} XSK_BIND_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_BIND_FLAGS)
C_ASSERT(sizeof(XSK_BIND_FLAGS) == sizeof(UINT32));

//
// XskBind
//
// Binds an AF_XDP socket to a network interface queue. An AF_XDP socket
// can only be bound to a single network interface queue.
//

typedef
HRESULT
XSK_BIND_FN(
    _In_ HANDLE socket,
    _In_ UINT32 ifIndex,
    _In_ UINT32 queueId,
    _In_ XSK_BIND_FLAGS flags
    );

typedef enum _XSK_ACTIVATE_FLAGS {
    XSK_ACTIVATE_FLAG_NONE = 0x0,
} XSK_ACTIVATE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_ACTIVATE_FLAGS)
C_ASSERT(sizeof(XSK_ACTIVATE_FLAGS) == sizeof(UINT32));

//
// XskActivate
//
// Activate the data path of an AF_XDP socket. An AF_XDP socket cannot send or
// receive data until it is successfully activated. An AF_XDP socket can only be
// activated after it has been successfully bound.
//
// Before calling XskActivate:
// 1) The socket object must have at least TX + TX completion and RX + RX fill
//    rings configured if bound to the TX and RX data paths, respectively.
// 2) The socket object must have registered or shared a UMEM.
//

typedef
HRESULT
XSK_ACTIVATE_FN(
    _In_ HANDLE socket,
    _In_ XSK_ACTIVATE_FLAGS flags
    );

//
// XskNotifySocket
//
// The purpose of this API is two-fold:
//     1) Pokes the underlying driver to continue IO processing
//     2) Waits on the underlying driver until IO is available
//
// Apps will commonly need to perform both of these actions at once, so a single
// API is offered to handle both in a single syscall.
//
// When performing both actions, the poke is executed first. If the poke fails,
// the API ignores the wait and returns immediately with a failure result. If
// the poke succeeds, then the wait is executed.
//
// The wait timeout interval can be set to INFINITE to specify that the wait
// will not time out.
//

typedef enum _XSK_NOTIFY_FLAGS {
    XSK_NOTIFY_FLAG_NONE = 0x0,

    //
    // Poke the driver to perform RX. Apps poke RX when entries are produced
    // on the RX fill ring and the RX fill ring is marked as needing a poke.
    //
    XSK_NOTIFY_FLAG_POKE_RX = 0x1,

    //
    // Poke the driver to perform TX. Apps poke TX when entries are produced
    // on the TX ring or consumed from the TX completion ring and the TX
    // ring is marked as needing a poke.
    //
    XSK_NOTIFY_FLAG_POKE_TX = 0x2,

    //
    // Wait until a RX ring entry is available.
    //
    XSK_NOTIFY_FLAG_WAIT_RX = 0x4,

    //
    // Wait until a TX completion ring entry is available.
    //
    XSK_NOTIFY_FLAG_WAIT_TX = 0x8,
} XSK_NOTIFY_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_NOTIFY_FLAGS)
C_ASSERT(sizeof(XSK_NOTIFY_FLAGS) == sizeof(UINT32));

typedef enum _XSK_NOTIFY_RESULT_FLAGS {
    XSK_NOTIFY_RESULT_FLAG_NONE = 0x0,

    //
    // RX ring entry is available.
    //
    XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE = 0x1,

    //
    // TX completion ring entry is available.
    //
    XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE = 0x2,
} XSK_NOTIFY_RESULT_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_NOTIFY_RESULT_FLAGS)
C_ASSERT(sizeof(XSK_NOTIFY_RESULT_FLAGS) == sizeof(UINT32));

typedef
HRESULT
XSK_NOTIFY_SOCKET_FN(
    _In_ HANDLE socket,
    _In_ XSK_NOTIFY_FLAGS flags,
    _In_ UINT32 waitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *result
    );

typedef struct _OVERLAPPED OVERLAPPED;

//
// XskNotifyAsync
//
// The purpose of this API is two-fold:
//     1) Pokes the underlying driver to continue IO processing
//     2) Waits on the underlying driver until IO is available
//
// Apps will commonly need to perform both of these actions at once, so a single
// API is offered to handle both in a single syscall.
//
// When performing both actions, the poke is executed first. If the poke fails,
// the API ignores the wait and returns immediately with a failure result. If
// the poke succeeds, then the wait is executed.
//
// Unlike XskNotifySocket, this routine does not perform the wait inline.
// Instead, if a wait was requested and could not be immediately satisfied, the
// routine returns HRESULT_FROM_WIN32(ERROR_IO_PENDING) and the overlapped IO
// will be completed asynchronously. Once the IO has completed, the
// XskGetNotifyAsyncResult routine may be used to retrieve the result flags.
//

typedef
HRESULT
XSK_NOTIFY_ASYNC_FN(
    _In_ HANDLE socket,
    _In_ XSK_NOTIFY_FLAGS flags,
    _Inout_ OVERLAPPED *overlapped
    );

//
// Retrieves the result flags from a previously completed XskNotifyAsync.
//

typedef
HRESULT
XSK_GET_NOTIFY_ASYNC_RESULT_FN(
    _In_ OVERLAPPED *overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *result
    );

//
// XskSetSockopt
//
// Sets a socket option.
//
typedef
HRESULT
XSK_SET_SOCKOPT_FN(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _In_reads_bytes_opt_(optionLength) const VOID *optionValue,
    _In_ UINT32 optionLength
    );

//
// XskGetSockopt
//
// Gets a socket option.
//
typedef
HRESULT
XSK_GET_SOCKOPT_FN(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _Out_writes_bytes_(*optionLength) VOID *optionValue,
    _Inout_ UINT32 *optionLength
    );

//
// XskIoctl
//
// Performs a socket IOCTL.
//
typedef
HRESULT
XSK_IOCTL_FN(
    _In_ HANDLE socket,
    _In_ UINT32 optionName,
    _In_reads_bytes_opt_(inputLength) const VOID *inputValue,
    _In_ UINT32 inputLength,
    _Out_writes_bytes_(*outputLength) VOID *outputValue,
    _Inout_ UINT32 *outputLength
    );

//
// Socket options
//

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
    UINT64 totalSize;
    UINT32 chunkSize;
    UINT32 headroom;
    VOID *address;
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
    BYTE *ring;
    UINT32 descriptorsOffset;   // XSK_FRAME_DESCRIPTOR[] for rx/tx, UINT64[] for fill/completion
    UINT32 producerIndexOffset; // UINT32
    UINT32 consumerIndexOffset; // UINT32
    UINT32 flagsOffset;         // UINT32
    UINT32 size;
    UINT32 elementStride;
    UINT32 reserved;
} XSK_RING_INFO;

typedef struct _XSK_RING_INFO_SET {
    XSK_RING_INFO fill;
    XSK_RING_INFO completion;
    XSK_RING_INFO rx;
    XSK_RING_INFO tx;
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
    UINT64 rxDropped;
    UINT64 rxTruncated;
    UINT64 rxInvalidDescriptors;
    UINT64 txInvalidDescriptors;
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
// Supports: set
//
// Optval type: HANDLE
// Description: An AF_XDP socket can share a UMEM registered with another AF_XDP
//              socket by supplying the handle of the socket already registered
//              with a UMEM.
//
#define XSK_SOCKOPT_SHARE_UMEM 14

//
// XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION
//
// Supports: get
// Optval type: XDP_EXTENSION
// Description: Gets the XDP_FRAME_LAYOUT descriptor extension for the TX frame
//              ring. This requires the socket is bound, the TX ring size is
//              set, and at least one socket option has enabled the frame layout
//              extension.
//
#define XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION 15

//
// XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION
//
// Supports: get
// Optval type: XDP_EXTENSION
// Description: Gets the XDP_FRAME_CHECKSUM descriptor extension for the TX
//              frame ring. This requires the socket is bound, the TX ring size
//              is set, and at least one socket option has enabled the frame
//              layout extension.
//
#define XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION 16

//
// XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX
//
// Supports: set
// Optval type: BOOLEAN
// Description: Sets whether UDP checksum transmit offload is enabled. This
//              option requires the socket is bound and the TX frame ring size
//              is not set. This option enables the XDP_FRAME_LAYOUT and
//              XDP_FRAME_CHECKSUM extensions on the TX frame ring.
//
#define XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX 17

//
// XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES
//
// Supports: get
// Optval type: XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES
// Description: Returns the UDP checksum transmit offload capabilities. This
//              option requires the socket is bound.
//
#define XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES 18

typedef struct _XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES {
    BOOLEAN Supported;
} XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES;

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
#define XSK_SOCKOPT_RX_PROCESSOR_AFFINITY 19

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
#define XSK_SOCKOPT_TX_PROCESSOR_AFFINITY 20

#ifdef __cplusplus
} // extern "C"
#endif

#endif
