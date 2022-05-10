//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * AF_XDP user-space access library.
 *
 * Copyright(c) 2018 - 2019 Intel Corporation.
 *
 * Author(s): Magnus Karlsson <magnus.karlsson@intel.com>
 */

#ifndef AFXDP_HELPER_LINUX_H
#define AFXDP_HELPER_LINUX_H

#include <stdio.h>
#include <stdint.h>
#ifdef WIN32
#include <stdlib.h>
#include <iphlpapi.h>
#include <afxdp.h>
#include <afxdp_helper.h>
#else
#include <linux/if_xdp.h>
#include "libbpf.h"
#include "libbpf_util.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
// PORTABILITY: On Windows, define these portability shims.
typedef UINT16 __u16;
typedef UINT32 __u32;
typedef UINT64 __u64;

// PORTABILITY: On Windows, re-define descriptor Linux style and ensure portability.
struct xdp_desc {
    __u64 addr;
    __u32 len;
    __u32 options;
};
C_ASSERT(FIELD_OFFSET(struct xdp_desc, addr) == FIELD_OFFSET(XSK_BUFFER_DESCRIPTOR, address));
C_ASSERT(RTL_FIELD_SIZE(struct xdp_desc, addr) == RTL_FIELD_SIZE(XSK_BUFFER_DESCRIPTOR, address));
C_ASSERT(FIELD_OFFSET(struct xdp_desc, len) == FIELD_OFFSET(XSK_BUFFER_DESCRIPTOR, length));
C_ASSERT(RTL_FIELD_SIZE(struct xdp_desc, len) == RTL_FIELD_SIZE(XSK_BUFFER_DESCRIPTOR, length));
C_ASSERT(sizeof(struct xdp_desc) == sizeof(XSK_BUFFER_DESCRIPTOR));
#endif

/* Do not access these members directly. Use the functions below. */
#define DEFINE_XSK_RING(name) \
struct name { \
    __u32 cached_prod; \
    __u32 cached_cons; \
    __u32 mask; \
    __u32 size; \
    __u32 *producer; \
    __u32 *consumer; \
    void *ring; \
    __u32 *flags; \
}

DEFINE_XSK_RING(xsk_ring_prod);
DEFINE_XSK_RING(xsk_ring_cons);

#ifdef WIN32
struct xsk_umem {
    HANDLE sockHandle;
    ULONG refCount;
    __u32 fill_size;
    __u32 comp_size;
    struct xsk_ring_prod *fill;
    struct xsk_ring_cons *comp;
};
struct xsk_socket {
    HANDLE sockHandle;
    struct xsk_umem *umem;
};
#else
struct xsk_umem;
struct xsk_socket;
#endif

inline __u64 *xsk_ring_prod__fill_addr(struct xsk_ring_prod *fill,
                          __u32 idx)
{
    return &((__u64*)fill->ring)[idx & fill->mask];
}

inline const __u64 *
xsk_ring_cons__comp_addr(const struct xsk_ring_cons *comp, __u32 idx)
{
    return &((__u64*)comp->ring)[idx & comp->mask];
}

inline struct xdp_desc *xsk_ring_prod__tx_desc(struct xsk_ring_prod *tx,
                              __u32 idx)
{
    return &((struct xdp_desc*)tx->ring)[idx & tx->mask];
}

inline const struct xdp_desc *
xsk_ring_cons__rx_desc(const struct xsk_ring_cons *rx, __u32 idx)
{
    return &((struct xdp_desc*)rx->ring)[idx & rx->mask];
}

inline int xsk_ring_prod__needs_wakeup(const struct xsk_ring_prod *r)
{
    return *r->flags & XSK_RING_FLAG_NEED_POKE;
}

// Returns the number of cached entries available for production that are not
// already reserved. The cached count is updated if current cached count < nb.
inline __u32 xsk_prod_nb_free(struct xsk_ring_prod *r, __u32 nb)
{
    __u32 free = r->cached_cons - r->cached_prod;
    if (free < nb) {
        r->cached_cons = *r->consumer + r->size;
        free = r->cached_cons - r->cached_prod;
    }

    return free;
}

// Returns the min of nb and number of cached entries available for consumption
// that have not been already peeked. The cached count is updated if current
// cached count == 0.
inline __u32 xsk_cons_nb_avail(struct xsk_ring_cons *r, __u32 nb)
{
    __u32 avail = r->cached_prod - r->cached_cons;
    if (avail == 0) {
        r->cached_prod = ReadULongAcquire((volatile ULONG*)r->producer);
        avail = r->cached_prod - r->cached_cons;
    }

    return min(avail, nb);
}

// If >= nb entries are available and not already reserved for production, nb
// entries are marked as reserved, nb is returned as well as the index of the
// first reserved entry. Otherwise, returns 0 and no entries are marked as
// reserved.
inline size_t xsk_ring_prod__reserve(struct xsk_ring_prod *prod,
                        size_t nb, __u32 *idx)
{
    __u32 free = xsk_prod_nb_free(prod, (__u32)nb);
    if (free < nb) {
        return 0;
    }

    *idx = prod->cached_prod;
    prod->cached_prod += (__u32)nb;

    return nb;
}

// Produces nb ring entries.
inline void xsk_ring_prod__submit(struct xsk_ring_prod *prod, size_t nb)
{
    __u32 new_prod = *prod->producer + (__u32)nb;
    WriteULongRelease((volatile ULONG*)prod->producer, new_prod);
}

// Returns the min of nb and number of entries available for consumption that
// have not been already peeked. If return value is > 0, the index of the
// starting entry is also returned.
inline size_t xsk_ring_cons__peek(struct xsk_ring_cons *cons,
                     size_t nb, __u32 *idx)
{
    __u32 avail = xsk_cons_nb_avail(cons, (__u32)nb);
    if (avail == 0) {
        return 0;
    }

    *idx = cons->cached_cons;
    cons->cached_cons += avail;

    return avail;
}

// Consumes nb ring entries.
inline void xsk_ring_cons__release(struct xsk_ring_cons *cons, size_t nb)
{
    *cons->consumer += (__u32)nb;
}

inline void *xsk_umem__get_data(void *umem_area, __u64 addr)
{
    return &((char*)umem_area)[addr];
}

inline __u64 xsk_umem__extract_addr(__u64 addr)
{
    return addr & ~XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MASK;
}

inline __u64 xsk_umem__extract_offset(__u64 addr)
{
    return
        (addr & XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_MASK) >>
            XSK_BUFFER_DESCRIPTOR_ADDR_OFFSET_SHIFT;
}

inline __u64 xsk_umem__add_offset_to_addr(__u64 addr)
{
    return xsk_umem__extract_addr(addr) + xsk_umem__extract_offset(addr);
}

#ifdef WIN32
// PORTABILITY: On Windows, HANDLE is returned instead of int.
HANDLE xsk_umem__fd(const struct xsk_umem *umem)
{
    return umem->sockHandle;
}
HANDLE xsk_socket__fd(const struct xsk_socket *xsk)
{
    return xsk->sockHandle;
}
#else
LIBBPF_API int xsk_umem__fd(const struct xsk_umem *umem);
LIBBPF_API int xsk_socket__fd(const struct xsk_socket *xsk);
#endif

#define XSK_RING_CONS__DEFAULT_NUM_DESCS      2048
#define XSK_RING_PROD__DEFAULT_NUM_DESCS      2048
#define XSK_UMEM__DEFAULT_FRAME_SHIFT    12 /* 4096 bytes */
#define XSK_UMEM__DEFAULT_FRAME_SIZE     (1 << XSK_UMEM__DEFAULT_FRAME_SHIFT)
#define XSK_UMEM__DEFAULT_FRAME_HEADROOM 0
#define XSK_UMEM__DEFAULT_FLAGS 0

struct xsk_umem_config {
    __u32 fill_size;
    __u32 comp_size;
    __u32 frame_size;
    __u32 frame_headroom;
    __u32 flags; // PORTABILITY: On Windows, currently unused.
};

#ifdef WIN32
// PORTABILITY: On Windows, there is no equivalent
// XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD flag.
#else
/* Flags for the libbpf_flags field. */
#define XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD (1 << 0)
#endif

struct xsk_socket_config {
    __u32 rx_size;
    __u32 tx_size;
    __u32 libbpf_flags; // PORTABILITY: On Windows, currently unused.
    __u32 xdp_flags;    // PORTABILITY: On Windows, currently unused.
    __u16 bind_flags;   // PORTABILITY: On Windows, currently unused.
};

/* Set config to NULL to get the default configuration. */
#ifndef WIN32
LIBBPF_API int xsk_umem__create(struct xsk_umem **umem,
                void *umem_area, __u64 size,
                struct xsk_ring_prod *fill,
                struct xsk_ring_cons *comp,
                const struct xsk_umem_config *config);
#else
inline int xsk_umem__create(struct xsk_umem **umem,
                void *umem_area, __u64 size,
                struct xsk_ring_prod *fill,
                struct xsk_ring_cons *comp,
                const struct xsk_umem_config *config)
{
    HRESULT hres;
    struct xsk_umem *umemObj = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    XSK_UMEM_REG umemReg = { 0 };
    struct xsk_umem_config defaultConfig = { 0 };

    if (config == NULL) {
        defaultConfig.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
        defaultConfig.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
        defaultConfig.frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
        defaultConfig.frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM;
        defaultConfig.flags = XSK_UMEM__DEFAULT_FLAGS;
        config = &defaultConfig;
    }

    hres = XskCreate(&handle);
    if (hres != S_OK) {
        goto error;
    }

    umemReg.totalSize = size;
    umemReg.chunkSize = config->frame_size;
    umemReg.headroom = config->frame_headroom;
    umemReg.address = umem_area;
    hres = XskSetSockopt(handle, XSK_SOCKOPT_UMEM_REG, &umemReg, sizeof(umemReg));
    if (hres != S_OK) {
        goto error;
    }

    umemObj = (struct xsk_umem *)malloc(sizeof(*umemObj));
    if (umemObj == NULL) {
        goto error;
    }

    umemObj->refCount = 1;
    umemObj->sockHandle = handle;

    //
    // Shim the UMEM fill/completion rings via XSK creation.
    //
    umemObj->fill_size = config->fill_size;
    umemObj->comp_size = config->comp_size;
    umemObj->fill = fill;
    umemObj->comp = comp;
    *umem = umemObj;
    umemObj = NULL;
    handle = INVALID_HANDLE_VALUE;
    return 0;

error:
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    if (umemObj != NULL) {
        free(umemObj);
    }
    return -1;
}
#endif

#ifdef WIN32
// PORTABILITY: On Windows, there are no versioned xsk_umem__create functions.
#else
LIBBPF_API int xsk_umem__create_v0_0_2(struct xsk_umem **umem,
                       void *umem_area, __u64 size,
                       struct xsk_ring_prod *fill,
                       struct xsk_ring_cons *comp,
                       const struct xsk_umem_config *config);
LIBBPF_API int xsk_umem__create_v0_0_4(struct xsk_umem **umem,
                       void *umem_area, __u64 size,
                       struct xsk_ring_prod *fill,
                       struct xsk_ring_cons *comp,
                       const struct xsk_umem_config *config);
#endif

#ifndef WIN32
LIBBPF_API int xsk_socket__create(struct xsk_socket **xsk,
                  const char *ifname, __u32 queue_id,
                  struct xsk_umem *umem,
                  struct xsk_ring_cons *rx,
                  struct xsk_ring_prod *tx,
                  const struct xsk_socket_config *config);
#else
inline int xsk_socket__create(struct xsk_socket **xsk,
                  const char *ifname, __u32 queue_id,
                  struct xsk_umem *umem,
                  struct xsk_ring_cons *rx,
                  struct xsk_ring_prod *tx,
                  const struct xsk_socket_config *config)
{
    HRESULT hres;
    struct xsk_socket *sockObj = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    XSK_RING_INFO_SET info = { 0 };
    UINT32 ringInfoSize = sizeof(info);
    int ifIndex;
    SIZE_T ringSize;
    XSK_BIND_FLAGS bindFlags = XSK_BIND_FLAG_NONE;

    ifIndex = if_nametoindex(ifname);
    if (ifIndex == 0) {
        goto error;
    }

    hres = XskCreate(&handle);
    if (hres != S_OK) {
        goto error;
    }

    if (config->rx_size > 0) {
        ringSize = config->rx_size;
        hres = XskSetSockopt(handle, XSK_SOCKOPT_RX_RING_SIZE, &ringSize, sizeof(ringSize));
        if (hres != S_OK) {
            goto error;
        }
        bindFlags |= XSK_BIND_FLAG_RX;
    }
    if (config->tx_size > 0) {
        ringSize = config->tx_size;
        hres = XskSetSockopt(handle, XSK_SOCKOPT_TX_RING_SIZE, &ringSize, sizeof(ringSize));
        if (hres != S_OK) {
            goto error;
        }
        bindFlags |= XSK_BIND_FLAG_TX;
    }

    ringSize = umem->fill_size;
    hres = XskSetSockopt(handle, XSK_SOCKOPT_RX_FILL_RING_SIZE, &ringSize, sizeof(ringSize));
    if (hres != S_OK) {
        goto error;
    }
    ringSize = umem->comp_size;
    hres =
        XskSetSockopt(handle, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &ringSize, sizeof(ringSize));
    if (hres != S_OK) {
        goto error;
    }

    hres = XskGetSockopt(handle, XSK_SOCKOPT_RING_INFO, &info, &ringInfoSize);
    if (hres != S_OK) {
        goto error;
    }
    if (config->rx_size > 0) {
        rx->ring = (XSK_BUFFER_DESCRIPTOR*)(info.rx.ring + info.rx.descriptorsOffset);
        rx->producer = (UINT32*)(info.rx.ring + info.rx.producerIndexOffset);
        rx->consumer = (UINT32*)(info.rx.ring + info.rx.consumerIndexOffset);
        rx->flags = (UINT32*)(info.rx.ring + info.rx.flagsOffset);
        rx->cached_prod = 0;
        rx->cached_cons = 0;
        rx->size = info.rx.size;
        rx->mask = info.rx.size - 1;

        umem->fill->ring = (UINT64*)(info.fill.ring + info.fill.descriptorsOffset);
        umem->fill->producer = (UINT32*)(info.fill.ring + info.fill.producerIndexOffset);
        umem->fill->consumer = (UINT32*)(info.fill.ring + info.fill.consumerIndexOffset);
        umem->fill->flags = (UINT32*)(info.fill.ring + info.fill.flagsOffset);
        umem->fill->cached_prod = 0;
        umem->fill->cached_cons = 0;
        umem->fill->size = info.fill.size;
        umem->fill->mask = info.fill.size - 1;
    }
    if (config->tx_size > 0) {
        tx->ring = (XSK_BUFFER_DESCRIPTOR*)(info.tx.ring + info.tx.descriptorsOffset);
        tx->producer = (UINT32*)(info.tx.ring + info.tx.producerIndexOffset);
        tx->consumer = (UINT32*)(info.tx.ring + info.tx.consumerIndexOffset);
        tx->flags = (UINT32*)(info.tx.ring + info.tx.flagsOffset);
        tx->cached_prod = 0;
        tx->cached_cons = 0;
        tx->size = info.tx.size;
        tx->mask = info.tx.size - 1;

        umem->comp->ring = (UINT64*)(info.completion.ring + info.completion.descriptorsOffset);
        umem->comp->producer = (UINT32*)(info.completion.ring + info.completion.producerIndexOffset);
        umem->comp->consumer = (UINT32*)(info.completion.ring + info.completion.consumerIndexOffset);
        umem->comp->flags = (UINT32*)(info.completion.ring + info.completion.flagsOffset);
        umem->comp->cached_prod = 0;
        umem->comp->cached_cons = 0;
        umem->comp->size = info.completion.size;
        umem->comp->mask = info.completion.size - 1;
    }

    hres = XskBind(handle, ifIndex, queue_id, (XSK_BIND_FLAGS)config->bind_flags | bindFlags);
    if (hres != S_OK) {
        goto error;
    }

    if (umem->sockHandle != NULL) {
        hres =
            XskSetSockopt(
                handle, XSK_SOCKOPT_SHARE_UMEM, &umem->sockHandle, sizeof(umem->sockHandle));
        if (hres != S_OK) {
            goto error;
        }
    }

    hres = XskActivate(handle, XSK_ACTIVATE_FLAG_NONE);
    if (hres != S_OK) {
        goto error;
    }

    sockObj = (struct xsk_socket *)malloc(sizeof(*sockObj));
    if (sockObj == NULL) {
        goto error;
    }

    sockObj->sockHandle = handle;
    sockObj->umem = umem;
    ++sockObj->umem->refCount;
    *xsk = sockObj;
    sockObj = NULL;
    handle = INVALID_HANDLE_VALUE;
    return 0;

error:
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    if (sockObj != NULL) {
        free(sockObj);
    }
    return -1;
}
#endif

#ifdef WIN32
inline
int
deref_umem(struct xsk_umem *umem)
{
    if (--umem->refCount == 0) {
        CloseHandle(umem->sockHandle);
        free(umem);
        return 0;
    }
    return -1;
}
// PORTABILITY: On Windows, return -1 if the umem is still in use.
inline int xsk_umem__delete(struct xsk_umem *umem)
{
    return deref_umem(umem);
}
inline void xsk_socket__delete(struct xsk_socket *xsk)
{
    deref_umem(xsk->umem);
    CloseHandle(xsk->sockHandle);
    free(xsk);
}
#else
/* Returns 0 for success and -EBUSY if the umem is still in use. */
LIBBPF_API int xsk_umem__delete(struct xsk_umem *umem);
LIBBPF_API void xsk_socket__delete(struct xsk_socket *xsk);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
