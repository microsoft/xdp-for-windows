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

#include <xdp/framechecksum.h>
#include <xdp/framelayout.h>

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
// Optval type: UINT16
// Description: Gets the XDP_FRAME_LAYOUT descriptor extension for the TX frame
//              ring. This requires the socket is bound, the TX ring size is
//              set, and at least one socket option has enabled the frame layout
//              extension. The returned value is the offset of the
//              XDP_FRAME_LAYOUT structure from the start of each TX descriptor.
//
#define XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION 1001

//
// XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION
//
// Supports: get
// Optval type: UINT16
// Description: Gets the XDP_FRAME_CHECKSUM descriptor extension for the TX
//              frame ring. This requires the socket is bound, the TX ring size
//              is set, and at least one socket option has enabled the checksum
//              extension. The returned value is the offset of the
//              XDP_FRAME_CHECKSUM structure from the start of each TX descriptor.
//
#define XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION 1002

//
// XSK_SOCKOPT_TX_OFFLOAD_CHECKSUM
//
// Supports: set
// Optval type: UINT32
// Description: Sets whether checksum transmit offload is enabled. This
//              option requires the socket is bound and the TX frame ring size
//              is not set. This option enables the XDP_FRAME_LAYOUT and
//              XDP_FRAME_CHECKSUM extensions on the TX frame ring.
//              If the socket is bound to a queue that has already been
//              activated by another socket without enabling checksum offload,
//              then enabling the offload on another socket is currently not
//              supported. Disabling the offload after is has been enabled is
//              also currently not supported.
//
#define XSK_SOCKOPT_TX_OFFLOAD_CHECKSUM 1003

//
// XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM
//
// Supports: get
// Optval type: XDP_CHECKSUM_CONFIGURATION
// Description: Returns the TX queue's current checksum offload configuration.
//
#define XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM 1004

//
// XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION
//
// Supports: get
// Optval type: UINT16
// Description: Gets the XDP_FRAME_CHECKSUM descriptor extension for the RX
//              frame ring. This requires the socket is bound, the RX ring size
//              is set. The returned value is the offset of the
//              XDP_FRAME_CHECKSUM structure from the start of each RX descriptor.
//
#define XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION 1005

//
// XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION
//
// Supports: get
// Optval type: UINT16
// Description: Gets the XDP_FRAME_LAYOUT descriptor extension for the RX frame
//              ring. This requires the socket is bound, the RX ring size is
//              set, and at least one socket option has enabled the frame layout
//              extension. The returned value is the offset of the
//              XDP_FRAME_LAYOUT structure from the start of each RX descriptor.
//
#define XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION 1006

//
// XSK_SOCKOPT_RX_OFFLOAD_CHECKSUM
//
// Supports: set
// Optval type: UINT32
// Description: Sets whether checksum receive offload is enabled. This
//              option requires the socket is bound and the RX frame ring size
//              is not set. This option enables the XDP_FRAME_LAYOUT and
//              XDP_FRAME_CHECKSUM extensions on the RX frame ring.
//              If the socket is bound to a queue that has already been
//              activated by another socket without enabling checksum offload,
//              then enabling the offload on another socket is currently not
//              supported. Disabling the offload after is has been enabled is
//              also currently not supported.
//
#define XSK_SOCKOPT_RX_OFFLOAD_CHECKSUM 1007

#ifdef __cplusplus
} // extern "C"
#endif

#endif
