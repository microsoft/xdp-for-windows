//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifndef __BPF_HELPERS_H
#define __BPF_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

//
// BPF helper macros and function declarations for XDP programs.
//

// Section macro for program type annotation
#define SEC(name) __attribute__((section(name), used))

// Map definition helper macros
#define __uint(name, val) int(*name)[val]
#define __type(name, val) typeof(val) *name
#define __array(name, val) typeof(val) *name[]

// BPF map types
#define BPF_MAP_TYPE_UNSPEC 0
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_MAP_TYPE_PROG_ARRAY 3
#define BPF_MAP_TYPE_PERF_EVENT_ARRAY 4
#define BPF_MAP_TYPE_PERCPU_HASH 5
#define BPF_MAP_TYPE_PERCPU_ARRAY 6
#define BPF_MAP_TYPE_STACK_TRACE 7
#define BPF_MAP_TYPE_CGROUP_ARRAY 8
#define BPF_MAP_TYPE_LRU_HASH 9
#define BPF_MAP_TYPE_LRU_PERCPU_HASH 10
#define BPF_MAP_TYPE_LPM_TRIE 11
#define BPF_MAP_TYPE_ARRAY_OF_MAPS 12
#define BPF_MAP_TYPE_HASH_OF_MAPS 13
#define BPF_MAP_TYPE_DEVMAP 14
#define BPF_MAP_TYPE_SOCKMAP 15
#define BPF_MAP_TYPE_CPUMAP 16
#define BPF_MAP_TYPE_XSKMAP 17
#define BPF_MAP_TYPE_SOCKHASH 18
#define BPF_MAP_TYPE_CGROUP_STORAGE 19
#define BPF_MAP_TYPE_REUSEPORT_SOCKARRAY 20
#define BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE 21
#define BPF_MAP_TYPE_QUEUE 22
#define BPF_MAP_TYPE_STACK 23
#define BPF_MAP_TYPE_SK_STORAGE 24
#define BPF_MAP_TYPE_DEVMAP_HASH 25
#define BPF_MAP_TYPE_STRUCT_OPS 26
#define BPF_MAP_TYPE_RINGBUF 27
#define BPF_MAP_TYPE_INODE_STORAGE 28

// BPF map update flags
#define BPF_ANY 0      // Create new element or update existing
#define BPF_NOEXIST 1  // Create new element only
#define BPF_EXIST 2    // Update existing element only

// BPF program types (subset relevant for XDP)
#define BPF_PROG_TYPE_UNSPEC 0
#define BPF_PROG_TYPE_SOCKET_FILTER 1
#define BPF_PROG_TYPE_KPROBE 2
#define BPF_PROG_TYPE_SCHED_CLS 3
#define BPF_PROG_TYPE_SCHED_ACT 4
#define BPF_PROG_TYPE_TRACEPOINT 5
#define BPF_PROG_TYPE_XDP 6
#define BPF_PROG_TYPE_PERF_EVENT 7
#define BPF_PROG_TYPE_CGROUP_SKB 8
#define BPF_PROG_TYPE_CGROUP_SOCK 9

// BPF attach types (subset relevant for XDP)
#define BPF_CGROUP_INET_INGRESS 0
#define BPF_CGROUP_INET_EGRESS 1
#define BPF_CGROUP_INET_SOCK_CREATE 2
#define BPF_CGROUP_SOCK_OPS 3
#define BPF_SK_SKB_STREAM_PARSER 4
#define BPF_SK_SKB_STREAM_VERDICT 5
#define BPF_CGROUP_DEVICE 6
#define BPF_SK_MSG_VERDICT 7
#define BPF_CGROUP_INET4_BIND 8
#define BPF_CGROUP_INET6_BIND 9
#define BPF_CGROUP_INET4_CONNECT 10
#define BPF_CGROUP_INET6_CONNECT 11
#define BPF_CGROUP_INET4_POST_BIND 12
#define BPF_CGROUP_INET6_POST_BIND 13
#define BPF_CGROUP_UDP4_SENDMSG 14
#define BPF_CGROUP_UDP6_SENDMSG 15
#define BPF_XDP 37

// Common typedefs
#ifndef __u8
typedef unsigned char __u8;
#endif

#ifndef __u16
typedef unsigned short __u16;
#endif

#ifndef __u32
typedef unsigned int __u32;
#endif

#ifndef __u64
typedef unsigned long long __u64;
#endif

#ifndef __s8
typedef signed char __s8;
#endif

#ifndef __s16
typedef signed short __s16;
#endif

#ifndef __s32
typedef signed int __s32;
#endif

#ifndef __s64
typedef signed long long __s64;
#endif

//
// BPF helper function declarations
//
// These are the standard BPF helper functions that can be called from
// BPF programs. The actual implementation is provided by the eBPF runtime.
//

/**
 * @brief Look up an element in a map.
 * @param map Pointer to the map.
 * @param key Pointer to the key.
 * @return Pointer to the value, or NULL if not found.
 */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;

/**
 * @brief Update or create an element in a map.
 * @param map Pointer to the map.
 * @param key Pointer to the key.
 * @param value Pointer to the value.
 * @param flags Update flags (BPF_ANY, BPF_NOEXIST, BPF_EXIST).
 * @return 0 on success, negative value on error.
 */
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, __u64 flags) = (void *)2;

/**
 * @brief Delete an element from a map.
 * @param map Pointer to the map.
 * @param key Pointer to the key.
 * @return 0 on success, negative value on error.
 */
static long (*bpf_map_delete_elem)(void *map, const void *key) = (void *)3;

/**
 * @brief Print debug message to trace pipe.
 * @param fmt Format string.
 * @param fmt_size Size of format string.
 * @return Length of the formatted string, or negative value on error.
 */
static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...) = (void *)6;

/**
 * @brief Get the current process/thread ID.
 * @return Current PID/TID.
 */
static __u64 (*bpf_get_current_pid_tgid)(void) = (void *)14;

/**
 * @brief Get the current UID/GID.
 * @return Current UID/GID.
 */
static __u64 (*bpf_get_current_uid_gid)(void) = (void *)15;

/**
 * @brief Get current timestamp in nanoseconds.
 * @return Timestamp in nanoseconds.
 */
static __u64 (*bpf_ktime_get_ns)(void) = (void *)5;

/**
 * @brief Get current boot time in nanoseconds.
 * @return Boot timestamp in nanoseconds.
 */
static __u64 (*bpf_ktime_get_boot_ns)(void) = (void *)125;

/**
 * @brief Get a pseudo-random number.
 * @return Random number.
 */
static __u32 (*bpf_get_prandom_u32)(void) = (void *)7;

// Convenience macro for bpf_trace_printk
#define bpf_printk(fmt, ...)                       \
({                                                  \
    char ____fmt[] = fmt;                          \
    bpf_trace_printk(____fmt, sizeof(____fmt),     \
                     ##__VA_ARGS__);                \
})

/**
 * @brief Adjust the XDP data pointer (move the packet header).
 * @param ctx XDP context.
 * @param delta Number of bytes to adjust (can be negative).
 * @return 0 on success, negative value on error.
 */
static long (*bpf_xdp_adjust_head)(void *ctx, int delta) = (void *)44;

/**
 * @brief Adjust the XDP metadata pointer.
 * @param ctx XDP context.
 * @param delta Number of bytes to adjust (can be negative).
 * @return 0 on success, negative value on error.
 */
static long (*bpf_xdp_adjust_meta)(void *ctx, int delta) = (void *)54;

/**
 * @brief Adjust the XDP tail pointer (grow or shrink the packet).
 * @param ctx XDP context.
 * @param delta Number of bytes to adjust (can be negative).
 * @return 0 on success, negative value on error.
 */
static long (*bpf_xdp_adjust_tail)(void *ctx, int delta) = (void *)65;

// Helper macros for pointer arithmetic in bounds-checked way
#define bpf_clamp_umax(VAR, UMAX)                                       \
    asm volatile("if %0 <= %[max] goto +1; %0 = %[max];"               \
                 : "+r"(VAR)                                            \
                 : [max]"i"(UMAX))

#define bpf_clamp_smax(VAR, SMAX)                                       \
    asm volatile("if %0 s<= %[max] goto +1; %0 = %[max];"              \
                 : "+r"(VAR)                                            \
                 : [max]"i"(SMAX))

// Barrier macros for memory ordering
#define barrier() asm volatile("" ::: "memory")
#define barrier_var(var) asm volatile("" : "+r"(var))

// Optimization hints
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __BPF_HELPERS_H
