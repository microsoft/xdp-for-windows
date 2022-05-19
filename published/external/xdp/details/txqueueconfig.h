//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/txqueueconfig.h>
#include <xdp/details/export.h>

typedef
CONST XDP_QUEUE_INFO *
XDP_TX_QUEUE_GET_TARGET_QUEUE_INFO(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

typedef
BOOLEAN
XDP_TX_QUEUE_CREATE_IS_ENABLED(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

typedef
VOID
XDP_TX_QUEUE_REGISTER_EXTENSION_VERSION(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

typedef
VOID
XDP_TX_QUEUE_SET_CAPABILITIES(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_CAPABILITIES *Capabilities
    );

typedef
VOID
XDP_TX_QUEUE_SET_DESCRIPTOR_CONTEXTS(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

typedef
VOID
XDP_TX_QUEUE_SET_POLL_INFO(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

typedef
VOID
XDP_TX_QUEUE_GET_EXTENSION(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

typedef
XDP_RING *
XDP_TX_QUEUE_GET_RING(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

typedef
BOOLEAN
XDP_TX_QUEUE_ACTIVATE_IS_ENABLED(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

typedef struct _XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH {
    XDP_OBJECT_HEADER                       Header;
    CONST VOID                              *Reserved;
    XDP_TX_QUEUE_GET_TARGET_QUEUE_INFO      *GetTargetQueueInfo;
    XDP_TX_QUEUE_REGISTER_EXTENSION_VERSION *RegisterExtensionVersion;
    XDP_TX_QUEUE_SET_CAPABILITIES           *SetTxQueueCapabilities;
    XDP_TX_QUEUE_SET_DESCRIPTOR_CONTEXTS    *SetTxDescriptorContexts;
    XDP_TX_QUEUE_SET_POLL_INFO              *SetPollInfo;
} XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH;

#define XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_TX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH, SetPollInfo)

typedef struct _XDP_TX_QUEUE_CONFIG_CREATE_DETAILS {
    CONST XDP_TX_QUEUE_CONFIG_CREATE_DISPATCH *Dispatch;
} XDP_TX_QUEUE_CONFIG_CREATE_DETAILS;

typedef struct _XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH {
    XDP_OBJECT_HEADER                       Header;
    CONST VOID                              *Reserved;
    XDP_TX_QUEUE_GET_RING                   *GetFrameRing;
    XDP_TX_QUEUE_GET_RING                   *GetFragmentRing;
    XDP_TX_QUEUE_GET_RING                   *GetCompletionRing;
    XDP_TX_QUEUE_GET_EXTENSION              *GetExtension;
    XDP_TX_QUEUE_ACTIVATE_IS_ENABLED        *IsTxCompletionContextEnabled;
    XDP_TX_QUEUE_ACTIVATE_IS_ENABLED        *IsFragmentationEnabled;
    XDP_TX_QUEUE_ACTIVATE_IS_ENABLED        *IsOutOfOrderCompletionEnabled;
} XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH;

#define XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1 1

#define XDP_SIZEOF_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH, IsOutOfOrderCompletionEnabled)

typedef struct _XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS {
    CONST XDP_TX_QUEUE_CONFIG_ACTIVATE_DISPATCH *Dispatch;
} XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS;

inline
CONST XDP_QUEUE_INFO *
XDPEXPORT(XdpTxQueueGetTargetQueueInfo)(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->GetTargetQueueInfo(TxQueueConfig);
}

inline
VOID
XDPEXPORT(XdpTxQueueSetCapabilities)(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    Details->Dispatch->SetTxQueueCapabilities(TxQueueConfig, Capabilities);
}

inline
VOID
XDPEXPORT(XdpTxQueueRegisterExtensionVersion)(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    Details->Dispatch->RegisterExtensionVersion(TxQueueConfig, ExtensionInfo);
}

inline
VOID
XDPEXPORT(XdpTxQueueSetDescriptorContexts)(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    Details->Dispatch->SetTxDescriptorContexts(TxQueueConfig, DescriptorContexts);
}

inline
VOID
XDPEXPORT(XdpTxQueueSetPollInfo)(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    )
{
    XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_CREATE_DETAILS *)TxQueueConfig;
    Details->Dispatch->SetPollInfo(TxQueueConfig, PollInfo);
}

inline
XDP_RING *
XDPEXPORT(XdpTxQueueGetFrameRing)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->GetFrameRing(TxQueueConfig);
}

inline
XDP_RING *
XDPEXPORT(XdpTxQueueGetFragmentRing)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->GetFragmentRing(TxQueueConfig);
}

inline
XDP_RING *
XDPEXPORT(XdpTxQueueGetCompletionRing)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->GetCompletionRing(TxQueueConfig);
}

inline
VOID
XDPEXPORT(XdpTxQueueGetExtension)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    Details->Dispatch->GetExtension(TxQueueConfig, ExtensionInfo, Extension);
}

inline
BOOLEAN
XDPEXPORT(XdpTxQueueIsTxCompletionContextEnabled)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->IsTxCompletionContextEnabled(TxQueueConfig);
}

inline
BOOLEAN
XDPEXPORT(XdpTxQueueIsFragmentationEnabled)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->IsFragmentationEnabled(TxQueueConfig);
}

inline
BOOLEAN
XDPEXPORT(XdpTxQueueIsOutOfOrderCompletionEnabled)(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    )
{
    XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *Details = (XDP_TX_QUEUE_CONFIG_ACTIVATE_DETAILS *)TxQueueConfig;
    return Details->Dispatch->IsOutOfOrderCompletionEnabled(TxQueueConfig);
}

EXTERN_C_END
