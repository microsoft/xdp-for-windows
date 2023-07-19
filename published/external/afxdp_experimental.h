//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This header declares experimental AF_XDP interfaces. All definitions within
// this file are subject to breaking changes, including removal.
//

#ifndef AFXDP_EXPERIMENTAL_H
#define AFXDP_EXPERIMENTAL_H

#ifdef __cplusplus
extern "C" {
#endif

//
// XSK_SOCKOPT_POLL_MODE
//
// Supports: set
// Optval type: XSK_POLL_MODE
// Description: Sets the poll mode of a socket.
//

#define XSK_SOCKOPT_POLL_MODE 1000

typedef enum _XSK_POLL_MODE {
    //
    // Sets the XSK polling mode to the system default.
    //
    // Expectation: XSK_RING_FLAG_NEED_POKE varies.
    //
    XSK_POLL_MODE_DEFAULT,

    //
    // Sets the XSK polling mode to a kernel busy loop.
    //
    // Expectation: XSK_RING_FLAG_NEED_POKE is usually FALSE.
    //
    XSK_POLL_MODE_BUSY,

    //
    // Sets the XSK polling mode to poll only in the context of XskNotifySocket.
    //
    // Expectation: XSK_RING_FLAG_NEED_POKE is usually TRUE.
    //
    XSK_POLL_MODE_SOCKET,
} XSK_POLL_MODE;

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
#define XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION 1001

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
#define XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION 1002

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
#define XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX 1003

//
// XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES
//
// Supports: get
// Optval type: XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES
// Description: Returns the UDP checksum transmit offload capabilities. This
//              option requires the socket is bound.
//
#define XSK_SOCKOPT_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES 1004

typedef struct _XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES {
    BOOLEAN Supported;
} XSK_OFFLOAD_UDP_CHECKSUM_TX_CAPABILITIES;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
