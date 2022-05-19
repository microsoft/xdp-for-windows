//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/rxqueueconfig.h>
#include <xdp/details/export.h>

typedef
CONST XDP_QUEUE_INFO *
XDP_RX_QUEUE_GET_TARGET_QUEUE_INFO(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef
BOOLEAN
XDP_RX_QUEUE_CREATE_IS_ENABLED(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef
VOID
XDP_RX_QUEUE_REGISTER_EXTENSION_VERSION(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

typedef
VOID
XDP_RX_QUEUE_SET_CAPABILITIES(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_CAPABILITIES *Capabilities
    );

typedef
VOID
XDP_RX_QUEUE_SET_DESCRIPTOR_CONTEXTS(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

typedef
VOID
XDP_RX_QUEUE_SET_POLL_INFO(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

typedef
VOID
XDP_RX_QUEUE_GET_EXTENSION(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

typedef
XDP_RING *
XDP_RX_QUEUE_GET_RING(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

typedef
BOOLEAN
XDP_RX_QUEUE_ACTIVATE_IS_ENABLED(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

typedef struct _XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH {
    XDP_OBJECT_HEADER                       Header;
    CONST VOID                              *Reserved;
    XDP_RX_QUEUE_GET_TARGET_QUEUE_INFO      *GetTargetQueueInfo;
    XDP_RX_QUEUE_REGISTER_EXTENSION_VERSION *RegisterExtensionVersion;
    XDP_RX_QUEUE_SET_CAPABILITIES           *SetRxQueueCapabilities;
    XDP_RX_QUEUE_SET_DESCRIPTOR_CONTEXTS    *SetRxDescriptorContexts;
    XDP_RX_QUEUE_SET_POLL_INFO              *SetPollInfo;
} XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH;

#define XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_RX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH, SetPollInfo)

typedef struct _XDP_RX_QUEUE_CONFIG_CREATE_DETAILS {
    CONST XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH *Dispatch;
} XDP_RX_QUEUE_CONFIG_CREATE_DETAILS;

typedef struct _XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH {
    XDP_OBJECT_HEADER                       Header;
    CONST VOID                              *Reserved;
    XDP_RX_QUEUE_GET_RING                   *GetFrameRing;
    XDP_RX_QUEUE_GET_RING                   *GetFragmentRing;
    XDP_RX_QUEUE_GET_EXTENSION              *GetExtension;
    XDP_RX_QUEUE_ACTIVATE_IS_ENABLED        *IsVirtualAddressEnabled;
} XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH;

#define XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH, IsVirtualAddressEnabled)

typedef struct _XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS {
    CONST XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH *Dispatch;
} XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS;

inline
CONST XDP_QUEUE_INFO *
XDPEXPORT(XdpRxQueueGetTargetQueueInfo)(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    return Details->Dispatch->GetTargetQueueInfo(RxQueueConfig);
}

inline
VOID
XDPEXPORT(XdpRxQueueSetCapabilities)(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_CAPABILITIES *Capabilities
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    Details->Dispatch->SetRxQueueCapabilities(RxQueueConfig, Capabilities);
}

inline
VOID
XDPEXPORT(XdpRxQueueRegisterExtensionVersion)(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    Details->Dispatch->RegisterExtensionVersion(RxQueueConfig, ExtensionInfo);
}

inline
VOID
XDPEXPORT(XdpRxQueueSetDescriptorContexts)(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    Details->Dispatch->SetRxDescriptorContexts(RxQueueConfig, DescriptorContexts);
}

inline
VOID
XDPEXPORT(XdpRxQueueSetPollInfo)(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    )
{
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_CREATE_DETAILS *)RxQueueConfig;
    Details->Dispatch->SetPollInfo(RxQueueConfig, PollInfo);
}

inline
XDP_RING *
XDPEXPORT(XdpRxQueueGetFrameRing)(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *)RxQueueConfig;
    return Details->Dispatch->GetFrameRing(RxQueueConfig);
}

inline
XDP_RING *
XDPEXPORT(XdpRxQueueGetFragmentRing)(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *)RxQueueConfig;
    return Details->Dispatch->GetFragmentRing(RxQueueConfig);
}

inline
VOID
XDPEXPORT(XdpRxQueueGetExtension)(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    )
{
    XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *)RxQueueConfig;
    Details->Dispatch->GetExtension(RxQueueConfig, ExtensionInfo, Extension);
}

inline
BOOLEAN
XDPEXPORT(XdpRxQueueIsVirtualAddressEnabled)(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS *)RxQueueConfig;
    return Details->Dispatch->IsVirtualAddressEnabled(RxQueueConfig);
}

EXTERN_C_END
