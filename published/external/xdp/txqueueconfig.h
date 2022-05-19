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

//
// Gets the target queue information.
//
CONST XDP_QUEUE_INFO *
XdpTxQueueGetTargetQueueInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig
    );

//
// Structure defining common TX capabilities and activation requirements for
// an XDP transmit queue.
//
typedef struct _XDP_TX_CAPABILITIES {
    XDP_OBJECT_HEADER Header;

    //
    // Indicates the interface uses the virtual address buffer extension.
    //
    BOOLEAN VirtualAddressEnabled;

    //
    // Indicates the interface uses the MDL buffer extension.
    //
    BOOLEAN MdlEnabled;

    //
    // Optional. Indicates the interface DMA capabilities.
    //
    XDP_DMA_CAPABILITIES *DmaCapabilities;

    //
    // Provides a hint indicating the interface's ideal frame queue length.
    //
    UINT16 TransmitFrameCountHint;

    //
    // Specifies the maximum TX buffer size.
    //
    UINT32 MaximumBufferSize;

    //
    // Specifies the maximum TX frame size, including all fragment buffers.
    //
    // The XDP platform ignores this value for GSO frames.
    //
    UINT32 MaximumFrameSize;

    //
    // Specifies the maximum number of fragment buffers in an XDP frame. When
    // MaximumFragments > 0, the XDP platform may enable the XDP_FRAME_FRAGMENT
    // frame extension and the fragment ring. The number of fragments does not
    // include the 0th buffer, which is in the frame ring. Interfaces use
    // XdpTxQueueIsFragmentationEnabled during queue activation to determine
    // whether the platform has enabled fragmentation.
    //
    // The XDP platform ignores this value for GSO frames.
    //
    UINT8 MaximumFragments;

    //
    // Indicates the interface is using out-of-order TX completion.
    //
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

//
// Initializes TX queue capabilities for system-allocated buffers with virtual
// addresses.
//
inline
VOID
XdpInitializeTxCapabilitiesSystemVa(
    _Out_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XdpInitializeTxCapabilities(Capabilities);
    Capabilities->VirtualAddressEnabled = TRUE;
}

//
// Initializes TX queue capabilities for system-allocated buffers with MDLs.
//
inline
VOID
XdpInitializeTxCapabilitiesSystemMdl(
    _Out_ XDP_TX_CAPABILITIES *Capabilities
    )
{
    XdpInitializeTxCapabilities(Capabilities);
    Capabilities->MdlEnabled = TRUE;
}

//
// Initializes TX queue capabilities for system-allocated buffers and DMA.
//
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

//
// Structure defining optional descriptor contexts for XDP transmit queues.
//
typedef struct _XDP_TX_DESCRIPTOR_CONTEXTS {
    XDP_OBJECT_HEADER Header;

    //
    // If non-zero, enables the XDP_FRAME_INTERFACE_CONTEXT frame extension. The
    // interface can store arbitrary data within this extension.
    //
    UINT8 FrameContextSize;

    //
    // Sets the XDP_FRAME_INTERFACE_CONTEXT frame extension alignment. Must be
    // a power of two and less than or equal to SYSTEM_CACHE_ALIGNMENT_SIZE.
    //
    UINT8 FrameContextAlignment;

    //
    // If non-zero, enables the XDP_BUFFER_INTERFACE_CONTEXT buffer extension.
    // The interface can store arbitrary data within this extension.
    //
    UINT8 BufferContextSize;

    //
    // Sets the XDP_BUFFER_INTERFACE_CONTEXT buffer extension alignment. Must be
    // a power of two and less than or equal to SYSTEM_CACHE_ALIGNMENT_SIZE.
    //
    UINT8 BufferContextAlignment;
} XDP_TX_DESCRIPTOR_CONTEXTS;

#define XDP_TX_DESCRIPTOR_CONTEXTS_REVISION_1 1

#define XDP_SIZEOF_TX_DESCRIPTOR_CONTEXTS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_TX_DESCRIPTOR_CONTEXTS, BufferContextAlignment)

//
// Initializes descriptor contexts for TX queues.
//
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

//
// Mandatory. Sets the TX queue capabilities.
//
VOID
XdpTxQueueSetCapabilities(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_CAPABILITIES *Capabilities
    );

//
// Registers the highest supported version of an XDP extension on the TX queue.
//
VOID
XdpTxQueueRegisterExtensionVersion(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

//
// Sets the TX descriptor context configuration.
//
VOID
XdpTxQueueSetDescriptorContexts(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_TX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

//
// Sets the TX queue polling configuration.
//
VOID
XdpTxQueueSetPollInfo(
    _In_ XDP_TX_QUEUE_CONFIG_CREATE TxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

//
// Gets the XDP frame ring. Each element is an XDP_FRAME followed by extensions.
//
XDP_RING *
XdpTxQueueGetFrameRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

//
// Gets the XDP fragment (multi-buffer) ring. Each element is an XDP_BUFFER
// followed by extensions.
//
XDP_RING *
XdpTxQueueGetFragmentRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

//
// Gets the XDP completion ring. Each element is an XDP_TX_FRAME_COMPLETION
// followed by extensions.
//
XDP_RING *
XdpTxQueueGetCompletionRing(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

//
// Gets a registered and enabled XDP extension.
//
VOID
XdpTxQueueGetExtension(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

//
// Returns whether the fragment ring and the frame fragment count extension are
// enabled.
//
BOOLEAN
XdpTxQueueIsFragmentationEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

//
// Returns whether the frame TX completion extension is enabled. If the TX
// completion extension is enabled, the NIC must provide this context in the
// TX completion ring.
//
BOOLEAN
XdpTxQueueIsTxCompletionContextEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

//
// Returns whether in-order TX completion is enabled.
//
BOOLEAN
XdpTxQueueIsOutOfOrderCompletionEnabled(
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE TxQueueConfig
    );

#include <xdp/details/txqueueconfig.h>

EXTERN_C_END
