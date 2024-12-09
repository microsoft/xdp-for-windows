//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef XDPEBPFHOOK_H
#define XDPEBPFHOOK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xdp_md_ {
    void *data;               ///< Pointer to start of packet data.
    void *data_end;           ///< Pointer to end of packet data.
    uint64_t data_meta;       ///< Packet metadata.
    uint32_t ingress_ifindex; ///< Ingress interface index.

    /* size: 26, cachelines: 1, members: 4 */
    /* last cacheline: 26 bytes */
} xdp_md_t;

typedef enum _xdp_action {
    XDP_PASS = 1, ///< Allow the packet to pass.
    XDP_DROP,     ///< Drop the packet.
    XDP_TX        ///< Bounce the received packet back out the same NIC it arrived on.
} xdp_action_t;

/**
 * @brief Handle an incoming packet as early as possible.
 *
 * Program type: \ref EBPF_PROGRAM_TYPE_XDP
 *
 * @param[in] context Packet metadata.
 * @retval XDP_PASS Allow the packet to pass.
 * @retval XDP_DROP Drop the packet.
 * @retval XDP_TX Bounce the received packet back out the same NIC it arrived on.
 */
typedef
xdp_action_t
xdp_hook_t(
    xdp_md_t *context
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif // XDPEBPFHOOK_H
