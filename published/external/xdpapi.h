//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
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

typedef
HRESULT
XDP_CREATE_PROGRAM_FN(
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
typedef
HRESULT
XDP_INTERFACE_OPEN_FN(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

//
// RSS offload.
//

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

typedef struct _XDP_RSS_CAPABILITIES {
    XDP_OBJECT_HEADER Header;
    UINT32 Flags;

    //
    // Supported hash types. Contains OR'd XDP_RSS_HASH_TYPE_* flags, or 0 to
    // indicate RSS is not supported.
    //
    UINT32 HashTypes;

    //
    // Maximum hash secret key size, in bytes.
    //
    UINT32 HashSecretKeySize;

    //
    // Number of hardware receive queues.
    //
    UINT32 NumberOfReceiveQueues;

    //
    // Maximum number of indirection table entries.
    //
    UINT32 NumberOfIndirectionTableEntries;
} XDP_RSS_CAPABILITIES;

#define XDP_RSS_CAPABILITIES_REVISION_1 1

#define XDP_SIZEOF_RSS_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CAPABILITIES, NumberOfReceiveQueues)

#define XDP_RSS_CAPABILITIES_REVISION_2 2

#define XDP_SIZEOF_RSS_CAPABILITIES_REVISION_2 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CAPABILITIES, NumberOfIndirectionTableEntries)

//
// Initializes an RSS capabilities object.
//
inline
VOID
XdpInitializeRssCapabilities(
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities
    )
{
    RtlZeroMemory(RssCapabilities, sizeof(*RssCapabilities));
    RssCapabilities->Header.Revision = XDP_RSS_CAPABILITIES_REVISION_1;
    RssCapabilities->Header.Size = XDP_SIZEOF_RSS_CAPABILITIES_REVISION_1;
}

//
// Query RSS capabilities on an interface. If the input RssCapabilitiesSize is
// too small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned.
// Call with a NULL RssCapabilities to get the length.
//
typedef
HRESULT
XDP_RSS_GET_CAPABILITIES_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    );

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
typedef
HRESULT
XDP_RSS_SET_FN(
    _In_ HANDLE InterfaceHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    );

//
// Query RSS settings on an interface. If the input RssConfigurationSize is too
// small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned. Call
// with a NULL RssConfiguration to get the length.
//
typedef
HRESULT
XDP_RSS_GET_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    );

#include "afxdp.h"

typedef struct _XDP_API_TABLE XDP_API_TABLE;

//
// The only API version currently supported. Any change to the API is considered
// a breaking change and support for previous versions will be removed.
//
#define XDP_VERSION_PRERELEASE 100001

//
// Opens the API and returns an API function table with the rest of the API's
// functions. Each open must invoke a corresponding XdpCloseApi when the API
// will no longer be used.
//
typedef
HRESULT
XDP_OPEN_API_FN(
    _In_ UINT32 XdpApiVersion,
    _Out_ CONST XDP_API_TABLE **XdpApiTable
    );

XDPAPI XDP_OPEN_API_FN XdpOpenApi;

//
// Releases the reference to the API returned by XdpOpenApi.
//
typedef
VOID
XDP_CLOSE_API_FN(
    _In_ CONST XDP_API_TABLE *XdpApiTable
    );

XDPAPI XDP_CLOSE_API_FN XdpCloseApi;

typedef struct _XDP_API_TABLE {
    XDP_OPEN_API_FN *XdpOpenApi;
    XDP_CLOSE_API_FN *XdpCloseApi;
    XDP_CREATE_PROGRAM_FN *XdpCreateProgram;
    XDP_INTERFACE_OPEN_FN *XdpInterfaceOpen;
    XDP_RSS_GET_CAPABILITIES_FN *XdpRssGetCapabilities;
    XDP_RSS_SET_FN *XdpRssSet;
    XDP_RSS_GET_FN *XdpRssGet;
    XSK_CREATE_FN *XskCreate;
    XSK_BIND_FN *XskBind;
    XSK_ACTIVATE_FN *XskActivate;
    XSK_NOTIFY_SOCKET_FN *XskNotifySocket;
    XSK_NOTIFY_ASYNC_FN *XskNotifyAsync;
    XSK_GET_NOTIFY_ASYNC_RESULT_FN *XskGetNotifyAsyncResult;
    XSK_SET_SOCKOPT_FN *XskSetSockopt;
    XSK_GET_SOCKOPT_FN *XskGetSockopt;
    XSK_IOCTL_FN *XskIoctl;
} XDP_API_TABLE;

typedef struct _XDP_LOAD_CONTEXT *XDP_LOAD_API_CONTEXT;

#if !defined(_KERNEL_MODE)

//
// Dynamically loads XDP, then opens the API and returns an API function table
// with the rest of the API's functions. Each open must invoke a corresponding
// XdpCloseApi when the API will no longer be used.
//
// This routine cannot be called from DllMain.
//
inline
HRESULT
XdpLoadApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ XDP_LOAD_API_CONTEXT *XdpLoadApiContext,
    _Out_ CONST XDP_API_TABLE **XdpApiTable
    )
{
    HRESULT Result;
    HMODULE XdpHandle;
    XDP_OPEN_API_FN *OpenApi;

    *XdpLoadApiContext = NULL;
    *XdpApiTable = NULL;

    XdpHandle = LoadLibraryA("xdpapi.dll");
    if (XdpHandle == NULL) {
        Result = E_NOINTERFACE;
        goto Exit;
    }

    OpenApi = (XDP_OPEN_API_FN *)GetProcAddress(XdpHandle, "XdpOpenApi");
    if (OpenApi == NULL) {
        Result = E_NOINTERFACE;
        goto Exit;
    }

    Result = OpenApi(XdpApiVersion, XdpApiTable);

Exit:

    if (SUCCEEDED(Result)) {
        *XdpLoadApiContext = (XDP_LOAD_API_CONTEXT)XdpHandle;
    } else {
        if (XdpHandle != NULL) {
            FreeLibrary(XdpHandle);
        }
    }

    return Result;
}

//
// Releases the reference to the API returned by XdpOpenApi, then dynamically
// unloads XDP.
//
// This routine cannot be called from DllMain.
//
inline
VOID
XdpUnloadApi(
    _In_ XDP_LOAD_API_CONTEXT XdpLoadApiContext,
    _In_ CONST XDP_API_TABLE *XdpApiTable
    )
{
    HMODULE XdpHandle = (HMODULE)XdpLoadApiContext;

    XdpApiTable->XdpCloseApi(XdpApiTable);

    FreeLibrary(XdpHandle);
}

#endif // !defined(_KERNEL_MODE)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
