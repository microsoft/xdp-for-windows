//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_XDPAPI_EXPERIMENTAL_H
#define XDP_DETAILS_XDPAPI_EXPERIMENTAL_H

#include <xdpapi_experimental.h>
#include <xdp/status.h>
#include <xdp/details/ioctldef.h>
#include <xdp/details/ioctlfn.h>

#ifdef __cplusplus
extern "C" {
#endif

inline
XDP_STATUS
XdpRssGetCapabilities(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssCapabilitiesSize) XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    )
{
    return
        _XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES,
            NULL, 0, RssCapabilities, *RssCapabilitiesSize,
            (ULONG *)RssCapabilitiesSize, NULL, TRUE);
}

inline
XDP_STATUS
XdpRssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ const XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    return
        _XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_SET,
            (XDP_RSS_CONFIGURATION *)RssConfiguration, RssConfigurationSize,
            NULL, 0, NULL, NULL, TRUE);
}

inline
XDP_STATUS
XdpRssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_writes_bytes_opt_(*RssConfigurationSize) XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    return
        _XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_RSS_GET, NULL, 0, RssConfiguration,
            *RssConfigurationSize, (ULONG *)RssConfigurationSize, NULL, TRUE);
}

inline
XDP_STATUS
XdpQeoSet(
    _In_ HANDLE InterfaceHandle,
    _Inout_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    )
{
    return
        _XdpIoctl(
            InterfaceHandle, IOCTL_INTERFACE_OFFLOAD_QEO_SET,
            QuicConnections, QuicConnectionsSize,
            QuicConnections, QuicConnectionsSize,
            (ULONG *)&QuicConnectionsSize, NULL, TRUE);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
