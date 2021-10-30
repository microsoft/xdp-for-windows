//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDPAPI_H
#define XDPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xdp/hookid.h>
#include <xdp/program.h>

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

//
// Create and attach an XDP program to an interface. The caller may optionally
// specify generic or native XDP binding mode. See xdp/program.h for placeholder
// program definitions.
//
// N.B. The current implementation supports only L2 RX inspect programs.
//

#define XDP_ATTACH_GENERIC  0x1
#define XDP_ATTACH_NATIVE   0x2

HRESULT
XDPAPI
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ UINT32 Flags,
    _In_reads_(RuleCount) CONST XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );

//
// Query/configure RSS settings on an interface. Configured settings will remain
// valid until the handle is closed.
// To disable RSS, supply 0 for HashType.
//

#define XDP_RSS_FLAG_HASH_TYPE_UNCHANGED         0x0001
#define XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED   0x0002
#define XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED 0x0004
#define XDP_RSS_VALID_FLAGS ( \
    XDP_RSS_FLAG_HASH_TYPE_UNCHANGED | \
    XDP_RSS_FLAG_HASH_SECRET_KEY_UNCHANGED | \
    XDP_RSS_FLAG_INDIRECTION_TABLE_UNCHANGED | \
    0)

#define XDP_RSS_HASH_TYPE_IPV4        0x001
#define XDP_RSS_HASH_TYPE_TCP_IPV4    0x002
#define XDP_RSS_HASH_TYPE_UDP_IPV4    0x004
#define XDP_RSS_HASH_TYPE_IPV6        0x008
#define XDP_RSS_HASH_TYPE_TCP_IPV6    0x010
#define XDP_RSS_HASH_TYPE_UDP_IPV6    0x020
#define XDP_RSS_HASH_TYPE_IPV6_EX     0x040
#define XDP_RSS_HASH_TYPE_TCP_IPV6_EX 0x080
#define XDP_RSS_HASH_TYPE_UDP_IPV6_EX 0x100
#define XDP_RSS_VALID_HASH_TYPES ( \
    XDP_RSS_HASH_TYPE_IPV4 | \
    XDP_RSS_HASH_TYPE_TCP_IPV4 | \
    XDP_RSS_HASH_TYPE_UDP_IPV4 | \
    XDP_RSS_HASH_TYPE_IPV6 | \
    XDP_RSS_HASH_TYPE_TCP_IPV6 | \
    XDP_RSS_HASH_TYPE_UDP_IPV6 | \
    XDP_RSS_HASH_TYPE_IPV6_EX | \
    XDP_RSS_HASH_TYPE_TCP_IPV6_EX | \
    XDP_RSS_HASH_TYPE_UDP_IPV6_EX | \
    0)

typedef struct _XDP_RSS_CONFIGURATION {
    UINT32 Flags;

    //
    // Packet hash type.
    // Contains OR'd XDP_RSS_HASH_TYPE_* flags, or 0 to indicate RSS is disabled.
    //
    UINT32 HashType;

    //
    // Number of bytes from the start of this struct to the start of the hash
    // secret key.
    //
    UINT16 HashSecretKeyOffset;

    //
    // Number of valid bytes in the hash secret key. Hash secret key
    // representation is UCHAR[].
    //
    UINT16 HashSecretKeySize;

    //
    // Number of bytes from the start of this struct to the start of the
    // indirection table.
    //
    UINT16 IndirectionTableOffset;

    //
    // Number of valid bytes in the indirection table. Indirection table
    // representation is PROCESSOR_NUMBER[].
    //
    UINT16 IndirectionTableSize;
} XDP_RSS_CONFIGURATION;

HRESULT
XDPAPI
XdpRssOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *RssHandle
    );

HRESULT
XDPAPI
XdpRssSet(
    _In_ HANDLE RssHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    );

HRESULT
XDPAPI
XdpRssGet(
    _In_ HANDLE RssHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
