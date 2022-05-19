//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This header declares experimental AF_XDP interfaces. All definitions within
// this file are subject to breaking changes.
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

#ifdef __cplusplus
} // extern "C"
#endif

#endif
