//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/extension.h>
#include <xdp/extensioninfo.h>
#include <xdp/pollinfo.h>
#include <xdp/queueinfo.h>
#include <xdp/objectheader.h>

DECLARE_HANDLE(XDP_RX_QUEUE_CONFIG_CREATE);
DECLARE_HANDLE(XDP_RX_QUEUE_CONFIG_ACTIVATE);

CONST XDP_QUEUE_INFO *
XdpRxQueueGetTargetQueueInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

typedef struct _XDP_RX_CAPABILITIES {
    XDP_OBJECT_HEADER Header;
    BOOLEAN VirtualAddressSupported;
    UINT16 ReceiveFrameCountHint;
    UINT8 MaximumFragments;
    BOOLEAN TxActionSupported;
} XDP_RX_CAPABILITIES;

#define XDP_RX_CAPABILITIES_REVISION_1 1

#define XDP_SIZEOF_RX_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_CAPABILITIES, TxActionSupported)

inline
VOID
XdpInitializeRxCapabilitiesDriverVa(
    _Out_ XDP_RX_CAPABILITIES *Capabilities
    )
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Header.Revision = XDP_RX_CAPABILITIES_REVISION_1;
    Capabilities->Header.Size = XDP_SIZEOF_RX_CAPABILITIES_REVISION_1;
    Capabilities->VirtualAddressSupported = TRUE;
}

typedef struct _XDP_RX_DESCRIPTOR_CONTEXTS {
    XDP_OBJECT_HEADER Header;
    UINT8 FrameContextSize;
    UINT8 FrameContextAlignment;
    UINT8 BufferContextSize;
    UINT8 BufferContextAlignment;
} XDP_RX_DESCRIPTOR_CONTEXTS;

#define XDP_RX_DESCRIPTOR_CONTEXTS_REVISION_1 1

#define XDP_SIZEOF_RX_DESCRIPTOR_CONTEXTS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RX_DESCRIPTOR_CONTEXTS, BufferContextAlignment)

inline
VOID
XdpInitializeRxDescriptorContexts(
    _Out_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    )
{
    RtlZeroMemory(DescriptorContexts, sizeof(*DescriptorContexts));
    DescriptorContexts->Header.Revision = XDP_RX_DESCRIPTOR_CONTEXTS_REVISION_1;
    DescriptorContexts->Header.Size = XDP_SIZEOF_RX_DESCRIPTOR_CONTEXTS_REVISION_1;
}

VOID
XdpRxQueueSetCapabilities(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_CAPABILITIES *Capabilities
    );

VOID
XdpRxQueueRegisterExtensionVersion(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

VOID
XdpRxQueueSetDescriptorContexts(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

VOID
XdpRxQueueSetPollInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

XDP_RING *
XdpRxQueueGetFrameRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

XDP_RING *
XdpRxQueueGetFragmentRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

VOID
XdpRxQueueGetExtension(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

BOOLEAN
XdpRxQueueIsVirtualAddressEnabled(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

#include <xdp/details/rxqueueconfig.h>

EXTERN_C_END
