//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef XDPEBPFHOOK_H
#define XDPEBPFHOOK_H

#include <ebpf_structs.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// BPF_MAP_TYPE_XSKMAP for AF_XDP socket redirection.
//

// TODO! BPF_MAP_TYPE_XSKMAP must be defined in eBPF headers.

#define BPF_MAP_TYPE_XSKMAP 16

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
// Program-specific helper functions.
//
#define XDP_EXT_HELPER_FN_BASE (EBPF_MAX_GENERAL_HELPER_FUNCTION + 1)

typedef enum _xdp_helper_id {
    BPF_FUNC_xdp_adjust_head = XDP_EXT_HELPER_FN_BASE,
    BPF_FUNC_redirect_map,
} xdp_helper_id_t;

#define XDP_EBPF_HELPER(return_type, name, args) typedef return_type(name##_t) args

XDP_EBPF_HELPER(int, bpf_xdp_adjust_head, (xdp_md_t *ctx, int delta));
#define bpf_xdp_adjust_head ((bpf_xdp_adjust_head_t *)BPF_FUNC_xdp_adjust_head)

XDP_EBPF_HELPER(intptr_t, bpf_redirect_map, (void *map, uint32_t key, uint64_t flags));
#define bpf_redirect_map ((bpf_redirect_map_t *)BPF_FUNC_redirect_map)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // XDPEBPFHOOK_H
