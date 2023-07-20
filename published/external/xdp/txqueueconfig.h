//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <xdp/dma.h>
#include <xdp/extension.h>
#include <xdp/extensioninfo.h>
#include <xdp/pollinfo.h>
#include <xdp/queueinfo.h>
#include <xdp/objectheader.h>

DECLARE_HANDLE(XDP_TX_QUEUE_CONFIG_CREATE);
DECLARE_HANDLE(XDP_TX_QUEUE_CONFIG_ACTIVATE);

CONST XDP_QUEUE_INFO *
XdpTxQueueGetTargetQueueInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

typedef struct _XDP_TX_CAPABILITIES {
    XDP_OBJECT_HEADER Header;
    BOOLEAN VirtualAddressEnabled;
    BOOLEAN MdlEnabled;
    XDP_DMA_CAPABILITIES *DmaCapabilities;
    UINT16 TransmitFrameCountHint;
    UINT32 MaximumBufferSize;
    UINT32 MaximumFrameSize;
    UINT8 MaximumFragments;
    BOOLEAN OutOfOrderCompletionEnabled;
} XDP_TX_CAPABILITIES;


#define XDP_TX_CAPABILITIES_REVISION_1 1

#define XDP_SIZEOF_TX_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_TX_CAPABILITIES, OutOfOrderCompletionEnabled)

//
// Reserved for system use.
//
inline
VOID
XdpInitializeTxCapabilities(
    _Out_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Header.Revision = XDP_TX_CAPABILITIES_REVISION_1;
    Capabilities->Header.Size = XDP_SIZEOF_TX_CAPABILITIES_REVISION_1;
    Capabilities->MaximumBufferSize = MAXUINT32;
    Capabilities->MaximumFrameSize = MAXUINT32;
}

inline
VOID
XdpInitializeTxCapabilitiesSystemVa(
    _Out_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XdpInitializeTxCapabilities(Capabilities);
    Capabilities->VirtualAddressEnabled = TRUE;
}

inline
VOID
XdpInitializeTxCapabilitiesSystemMdl(
    _Out_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XdpInitializeTxCapabilities(Capabilities);
    Capabilities->MdlEnabled = TRUE;
}

inline
VOID
XdpInitializeTxCapabilitiesSystemDma(
    _Out_ XDP_TX_CAPABILITIES *Capabilities,
    _In_ XDP_DMA_CAPABILITIES *DmaCapabilities
    )
{
    XdpInitializeTxCapabilities(Capabilities);
    Capabilities->DmaCapabilities = DmaCapabilities;
}

typedef struct _XDP_TX_DESCRIPTOR_CONTEXTS {
    XDP_OBJECT_HEADER Header;
    UINT8 FrameContextSize;
    UINT8 FrameContextAlignment;
    UINT8 BufferContextSize;
    UINT8 BufferContextAlignment;
} XDP_TX_DESCRIPTOR_CONTEXTS;

#define XDP_TX_DESCRIPTOR_CONTEXTS_REVISION_1 1

#define XDP_SIZEOF_TX_DESCRIPTOR_CONTEXTS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_TX_DESCRIPTOR_CONTEXTS, BufferContextAlignment)

inline
VOID
XdpInitializeTxDescriptorContexts(
    _Out_ XDP_TX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    )
{
    RtlZeroMemory(DescriptorContexts, sizeof(*DescriptorContexts));
    DescriptorContexts->Header.Revision = XDP_TX_DESCRIPTOR_CONTEXTS_REVISION_1;
    DescriptorContexts->Header.Size = XDP_SIZEOF_TX_DESCRIPTOR_CONTEXTS_REVISION_1;
}

VOID
XdpTxQueueSetCapabilities(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_CAPABILITIES *Capabilities
    );

VOID
XdpTxQueueRegisterExtensionVersion(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

VOID
XdpTxQueueSetDescriptorContexts(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

VOID
XdpTxQueueSetPollInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

XDP_RING *
XdpTxQueueGetFrameRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

XDP_RING *
XdpTxQueueGetFragmentRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

XDP_RING *
XdpTxQueueGetCompletionRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

VOID
XdpTxQueueGetExtension(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

BOOLEAN
XdpTxQueueIsFragmentationEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

BOOLEAN
XdpTxQueueIsTxCompletionContextEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

BOOLEAN
XdpTxQueueIsOutOfOrderCompletionEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

#include <xdp/details/txqueueconfig.h>

EXTERN_C_END
