# XDP driver receive queue control path

This file contains declarations for the XDP Driver receive queue control path.

## Syntax

```C
DECLARE_HANDLE(XDP_RX_QUEUE_CONFIG_CREATE);
DECLARE_HANDLE(XDP_RX_QUEUE_CONFIG_ACTIVATE);

//
// Gets the target queue information.
//
CONST XDP_QUEUE_INFO *
XdpRxQueueGetTargetQueueInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    );

//
// Structure defining common RX capabilities and activation requirements for
// an XDP receive queue.
//
typedef struct _XDP_RX_CAPABILITIES {
    XDP_OBJECT_HEADER Header;

    //
    // Indicates the interface supports the virtual address buffer extension.
    // This is currently mandatory for RX queues.
    //
    BOOLEAN VirtualAddressSupported;

    //
    // Provides a hint indicating the interface's ideal frame queue length.
    //
    UINT16 ReceiveFrameCountHint;

    //
    // Specifies the maximum number of fragment buffers in an XDP frame. When
    // MaximumFragments > 0, enables the XDP_FRAME_FRAGMENT frame extension
    // and enables the fragment ring. The number of fragments does not include
    // the 0th buffer, which is in the frame ring.
    //
    // If GRO is enabled, the XDP platform uses the greater of this value or
    // the maximum GRO fragments.
    //
    UINT8 MaximumFragments;

    //
    // The XDP_RX_ACTION_TX action is supported on this RX queue.
    //
    BOOLEAN TxActionSupported;
} XDP_RX_CAPABILITIES;

//
// Initializes RX queue capabilities for driver-allocated buffers with virtual
// addresses.
//
// This is the only supported RX model for XDP version 1.
//
inline
VOID
XdpInitializeRxCapabilitiesDriverVa(
    _Out_ XDP_RX_CAPABILITIES *Capabilities
    );

//
// Structure defining optional descriptor contexts for XDP receive queues.
//
typedef struct _XDP_RX_DESCRIPTOR_CONTEXTS {
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
} XDP_RX_DESCRIPTOR_CONTEXTS;

//
// Initializes descriptor contexts for RX queues.
//
inline
VOID
XdpInitializeRxDescriptorContexts(
    _Out_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

//
// Mandatory. Sets the RX queue capabilities.
//
VOID
XdpRxQueueSetCapabilities(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_CAPABILITIES *Capabilities
    );

//
// Registers the highest supported version of an XDP extension on the RX queue.
//
VOID
XdpRxQueueRegisterExtensionVersion(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    );

//
// Sets the RX descriptor context configuration.
//
VOID
XdpRxQueueSetDescriptorContexts(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_DESCRIPTOR_CONTEXTS *DescriptorContexts
    );

//
// Sets the RX queue polling configuration.
//
VOID
XdpRxQueueSetPollInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    );

//
// Gets the XDP frame ring. Each element is an XDP_FRAME followed by extensions.
//
XDP_RING *
XdpRxQueueGetFrameRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

//
// Gets the XDP fragment (multi-buffer) ring. Each element is an XDP_BUFFER
// followed by extensions.
//
XDP_RING *
XdpRxQueueGetFragmentRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );

//
// Gets a registered and enabled XDP extension.
//
VOID
XdpRxQueueGetExtension(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    );

//
// Returns whether the buffer virtual address extension is enabled. If the
// extension is enabled, the NIC must provide a valid virtual address with
// each XDP buffer.
//
BOOLEAN
XdpRxQueueIsVirtualAddressEnabled(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    );
```
