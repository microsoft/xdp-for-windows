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

typedef struct xdp_md {
    void *data;               ///< Pointer to start of packet data.
    void *data_end;           ///< Pointer to end of packet data.
    uint64_t data_meta;       ///< Packet metadata.
    uint32_t ingress_ifindex; ///< Ingress interface index.
    uint32_t rx_queue_index;  ///< RX queue index.
} xdp_md_t;

typedef enum _xdp_action {
    XDP_PASS = 1, ///< Allow the packet to pass.
    XDP_DROP,     ///< Drop the packet.
    XDP_TX,       ///< Bounce the received packet back out the same NIC it arrived on.
    XDP_REDIRECT  ///< Redirect the packet to another target (e.g., AF_XDP socket).
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
 * @retval XDP_REDIRECT Redirect the packet to another target.
 */
typedef
xdp_action_t
xdp_hook_t(
    xdp_md_t *context
    );

//
// N.B. bpf_redirect_map is a global (virtual) helper defined by eBPF for
// Windows (see bpf_helper_defs.h, BPF_FUNC_redirect_map). XDP provides the
// XDP-specific implementation via its global helper override table.
//

#ifdef __cplusplus
} // extern "C"
#endif

#endif // XDPEBPFHOOK_H
