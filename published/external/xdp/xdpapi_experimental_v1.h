//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This header declares experimental XDP interfaces. All definitions within
// this file are subject to breaking changes, including removal.
//

#ifndef XDPAPI_EXPERIMENTAL_V1_H
#define XDPAPI_EXPERIMENTAL_V1_H

#ifdef __cplusplus
extern "C" {
#endif

//
// RSS offload.
//

//
// Query RSS capabilities on an interface. If the input RssCapabilitiesSize is
// too small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned.
// Call with a NULL RssCapabilities to get the length.
//
typedef
HRESULT
XDP_RSS_GET_CAPABILITIES_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssCapabilitiesSize) XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    );

#define XDP_RSS_GET_CAPABILITIES_FN_NAME "XdpRssGetCapabilitiesExperimental"


//
// Set RSS settings on an interface. Configured settings will remain valid until
// the handle is closed. Upon handle closure, RSS settings will revert back to
// their original state.
//
typedef
HRESULT
XDP_RSS_SET_FN(
    _In_ HANDLE InterfaceHandle,
    _In_ const XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    );

#define XDP_RSS_SET_FN_NAME "XdpRssSetExperimental"

//
// Query RSS settings on an interface. If the input RssConfigurationSize is too
// small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned. Call
// with a NULL RssConfiguration to get the length.
//
typedef
HRESULT
XDP_RSS_GET_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssConfigurationSize) XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    );

#define XDP_RSS_GET_FN_NAME "XdpRssGetExperimental"

//
// Set QEO settings on an interface. Configured settings will remain valid until
// the handle is closed. Upon handle closure, QEO settings will revert back to
// their original state.
//
typedef HRESULT
XDP_QEO_SET_FN(
    _In_ HANDLE InterfaceHandle,
    _Inout_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    );

#define XDP_QEO_SET_FN_NAME "XdpQeoSetExperimental"

#ifdef __cplusplus
} // extern "C"
#endif

#endif
