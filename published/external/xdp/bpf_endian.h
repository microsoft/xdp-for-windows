//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef __BPF_ENDIAN_H
#define __BPF_ENDIAN_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Endianness conversion macros for BPF programs.
// These macros provide byte order conversion functionality.
//

// Byte swap macros
#ifndef ___bpf_concat
#define ___bpf_concat(a, b) a ## b
#endif

#ifndef ___bpf_apply
#define ___bpf_apply(fn, n) ___bpf_concat(fn, n)
#endif

#ifndef ___bpf_nth
#define ___bpf_nth(_, _1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, N, ...) N
#endif

#ifndef ___bpf_narg
#define ___bpf_narg(...) \
    ___bpf_nth(_, ##__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#endif

// Determine the endianness
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define __bpf_target_little_endian
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define __bpf_target_big_endian
#else
// Default to little endian for Windows
#define __bpf_target_little_endian
#endif

// Byte swap operations
#define ___bpf_bswap_16(x) \
    ((unsigned short)((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))

#define ___bpf_bswap_32(x) \
    ((unsigned int)((((x) >> 24) & 0xff) | \
                    (((x) >> 8) & 0xff00) | \
                    (((x) << 8) & 0xff0000) | \
                    (((x) << 24) & 0xff000000)))

#define ___bpf_bswap_64(x) \
    ((unsigned long long)((((x) >> 56) & 0xff) | \
                          (((x) >> 40) & 0xff00ULL) | \
                          (((x) >> 24) & 0xff0000ULL) | \
                          (((x) >> 8) & 0xff000000ULL) | \
                          (((x) << 8) & 0xff00000000ULL) | \
                          (((x) << 24) & 0xff0000000000ULL) | \
                          (((x) << 40) & 0xff000000000000ULL) | \
                          (((x) << 56) & 0xff00000000000000ULL)))

// Convert to big endian (network byte order)
#ifdef __bpf_target_little_endian
#define bpf_htons(x) ___bpf_bswap_16(x)
#define bpf_ntohs(x) ___bpf_bswap_16(x)
#define bpf_htonl(x) ___bpf_bswap_32(x)
#define bpf_ntohl(x) ___bpf_bswap_32(x)
#define bpf_htonll(x) ___bpf_bswap_64(x)
#define bpf_ntohll(x) ___bpf_bswap_64(x)
#else
#define bpf_htons(x) (x)
#define bpf_ntohs(x) (x)
#define bpf_htonl(x) (x)
#define bpf_ntohl(x) (x)
#define bpf_htonll(x) (x)
#define bpf_ntohll(x) (x)
#endif

// CPU to big endian
#define bpf_cpu_to_be16(x) bpf_htons(x)
#define bpf_cpu_to_be32(x) bpf_htonl(x)
#define bpf_cpu_to_be64(x) bpf_htonll(x)

// Big endian to CPU
#define bpf_be16_to_cpu(x) bpf_ntohs(x)
#define bpf_be32_to_cpu(x) bpf_ntohl(x)
#define bpf_be64_to_cpu(x) bpf_ntohll(x)

// Standard network byte order conversion macros
#ifndef htons
#define htons(x) bpf_htons(x)
#define ntohs(x) bpf_ntohs(x)
#define htonl(x) bpf_htonl(x)
#define ntohl(x) bpf_ntohl(x)
#endif

// Legacy compatibility macros
#ifndef __bpf_ntohs
#define __bpf_ntohs(x) bpf_ntohs(x)
#define __bpf_htons(x) bpf_htons(x)
#define __bpf_ntohl(x) bpf_ntohl(x)
#define __bpf_htonl(x) bpf_htonl(x)
#define __bpf_constant_ntohs(x) ___bpf_bswap_16(x)
#define __bpf_constant_htons(x) ___bpf_bswap_16(x)
#define __bpf_constant_ntohl(x) ___bpf_bswap_32(x)
#define __bpf_constant_htonl(x) ___bpf_bswap_32(x)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __BPF_ENDIAN_H
