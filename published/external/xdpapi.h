//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDPAPI_H
#define XDPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xdp/hookid.h>
#include <xdp/objectheader.h>
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

//
// Attach to the interface using the generic XDP provider.
//
#define XDP_CREATE_PROGRAM_FLAG_GENERIC 0x1

//
// Attach to the interface using the native XDP provider. If the interface does
// not support native XDP, the attach will fail.
//
#define XDP_CREATE_PROGRAM_FLAG_NATIVE  0x2

//
// Allow sharing the XDP queue with other XDP programs. All programs on the
// interface must use this flag for sharing to be enabled.
//
#define XDP_CREATE_PROGRAM_FLAG_SHARE   0x4

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
// Interface API.
//

//
// Open a handle to get/set offloads/configurations/properties on an interface.
//
HRESULT
XDPAPI
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

//
// RSS offload.
//

//
// Upon set, indicates XDP_RSS_CONFIGURATION.HashType should not be ignored.
//
#define XDP_RSS_FLAG_SET_HASH_TYPE         0x0001
//
// Upon set, indicates XDP_RSS_CONFIGURATION.HashSecretKeySize and
// XDP_RSS_CONFIGURATION.HashSecretKeyOffset should not be ignored.
//
#define XDP_RSS_FLAG_SET_HASH_SECRET_KEY   0x0002
//
// Upon set, indicates XDP_RSS_CONFIGURATION.IndirectionTableSize and
// XDP_RSS_CONFIGURATION.IndirectionTableOffset should not be ignored.
//
#define XDP_RSS_FLAG_SET_INDIRECTION_TABLE 0x0004
//
// Upon set, indicates RSS should be disabled.
// Upon get, indicates RSS is disabled.
//
#define XDP_RSS_FLAG_DISABLED              0x0008
#define XDP_RSS_VALID_FLAGS ( \
    XDP_RSS_FLAG_SET_HASH_TYPE | \
    XDP_RSS_FLAG_SET_HASH_SECRET_KEY | \
    XDP_RSS_FLAG_SET_INDIRECTION_TABLE | \
    XDP_RSS_FLAG_DISABLED | \
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
    XDP_OBJECT_HEADER Header;

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

#define XDP_RSS_CONFIGURATION_REVISION_1 1

#define XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CONFIGURATION, IndirectionTableSize)

//
// Initializes a RSS configuration object.
//
inline
VOID
XdpInitializeRssConfiguration(
    _Out_writes_bytes_(RssConfigurationSize) XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    RtlZeroMemory(RssConfiguration, RssConfigurationSize);
    RssConfiguration->Header.Revision = XDP_RSS_CONFIGURATION_REVISION_1;
    RssConfiguration->Header.Size = XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1;
}

//
// Set RSS settings on an interface. Configured settings will remain valid until
// the handle is closed. Upon handle closure, RSS settings will revert back to
// their original state.
//
HRESULT
XDPAPI
XdpRssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    );

//
// Query RSS settings on an interface. If the input RssConfigurationSize is too
// small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned. Call
// with a NULL RssConfiguration to get the length.
//
HRESULT
XDPAPI
XdpRssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
