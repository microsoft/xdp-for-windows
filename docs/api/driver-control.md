# XDP driver control path

This file contains declarations for the XDP Driver API control path.

## Syntax

```C
DECLARE_HANDLE(XDP_REGISTRATION_HANDLE);

//
// Each XDP-capable interface advertises its minimum XDP contract version. XDP
// interfaces can enable additional features at runtime by testing the current
// XDP platform's level of support.
//

typedef
_When_(DriverApiVersionCount == 0, _Struct_size_bytes_(Header.Size))
_When_(DriverApiVersionCount > 0, _Struct_size_bytes_(DriverApiVersionsOffset + DriverApiVersionCount * sizeof(XDP_VERSION)))
struct _XDP_CAPABILITIES_EX {
    XDP_OBJECT_HEADER Header;

    //
    // The interface may support one or more major versions of the XDP driver API.
    // The versions are represented as an array of XDP_VERSION entries.
    //
    // The array of minimum API versions which are supported starts at offset
    // DriverApiVersionsOffset and contains DriverApiVersionCount number
    // of XDP_VERSION entries.
    //
    // For each version entry of the form "Major.Minor.Patch" present in the array,
    // the interface supports any API version higher or equal to "Major.Minor.Patch"
    // but lower than "(Major + 1).0.0".
    //
    UINT32 DriverApiVersionsOffset;
    UINT32 DriverApiVersionCount;

    //
    // The driver API version published by the development kit.
    //
    XDP_VERSION DdkDriverApiVersion;

    //
    // The unique ID for the interface.
    //
    GUID InstanceId;
} XDP_CAPABILITIES_EX;

typedef struct _XDP_CAPABILITIES {
    XDP_CAPABILITIES_EX CapabilitiesEx;
    XDP_VERSION DriverApiVersion;
} XDP_CAPABILITIES;

//
// Initialize XDP interface capabilities with the given minimum API version.
//
inline
NTSTATUS
XdpInitializeCapabilities(
    _Out_ XDP_CAPABILITIES *Capabilities,
    _In_ CONST XDP_VERSION *DriverApiVersion
    )
{
    CONST XDP_VERSION DdkDriverApiVersion = {
        XDP_DRIVER_API_MAJOR_VER,
        XDP_DRIVER_API_MINOR_VER,
        XDP_DRIVER_API_PATCH_VER
    };

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->CapabilitiesEx.Header.Revision = XDP_CAPABILITIES_EX_REVISION_1;
    Capabilities->CapabilitiesEx.Header.Size = XDP_SIZEOF_CAPABILITIES_EX_REVISION_1;

    Capabilities->CapabilitiesEx.DriverApiVersionsOffset =
        FIELD_OFFSET(XDP_CAPABILITIES, DriverApiVersion);
    Capabilities->CapabilitiesEx.DriverApiVersionCount = 1;

    Capabilities->DriverApiVersion = *DriverApiVersion;
    Capabilities->CapabilitiesEx.DdkDriverApiVersion = DdkDriverApiVersion;

    return XdpGuidCreate(&Capabilities->CapabilitiesEx.InstanceId);
}

NTSTATUS
XdpRegisterInterfaceEx(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_CAPABILITIES_EX *CapabilitiesEx,
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    );

//
// Each XDP-capable interface must register with the XDP platform. The interface
// declares its capabilities and provides a control path dispatch table; once
// registered, XDP may open the interface and invoke the interface dispatch
// table routines.
//
// N.B. Drivers are encouraged to define POOL_ZERO_DOWN_LEVEL_SUPPORT and call
//      ExInitializeDriverRuntime prior to invoking this routine.
//
// Parameters:
//
//      InterfaceIndex:
//          Specifies the interface index.
//
//      Capabilities:
//          Specifies the interface XDP capabilities.
//
//      InterfaceContext:
//          Provides an opaque context. XDP provides this context when invoking
//          certain interface dispatch table routines.
//
//      InterfaceDispatch:
//          Provides the interface XDP dispatch table.
//
//      RegistrationHandle:
//          Upon success, returns a registration handle. This handle is used to
//          deregister the interface.
//
inline
NTSTATUS
XdpRegisterInterface(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_CAPABILITIES *Capabilities,
    _In_ VOID *InterfaceContext,
    _In_ CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch,
    _Out_ XDP_REGISTRATION_HANDLE *RegistrationHandle
    )
{
    return
        XdpRegisterInterfaceEx(
            InterfaceIndex, &Capabilities->CapabilitiesEx, InterfaceContext,
            InterfaceDispatch, RegistrationHandle);
}

//
// Each XDP interface must deregister from the XDP platform prior to
// invalidating its interface context and dispatch table. When an interface
// initiates deregistration, the XDP platform automatically deletes any
// previously-created XDP queues, invokes the interface close callback, and then
// completes the deregistration request.
//
// Parameters:
//
//      RegistrationHandle:
//          Provides the handle to a registered XDP interface.
//
VOID
XdpDeregisterInterface(
    _In_ XDP_REGISTRATION_HANDLE RegistrationHandle
    );

//
// Optional. Performs initialization when XDP opens an interface.
//
// The XDP platform dynamically opens and closes XDP interfaces; the platform
// ensures each interface has at most one opened XDP session at a time. When an
// interface is opened by XDP, the platform provides an interface-level
// configuration object. Once the interface is opened, XDP may create XDP queues
// or close the interface.
//
// Parameters:
//
//      InterfaceContext:
//          Provides the interface context from registration.
//
//      InterfaceConfig:
//          Provides an interface configuration object. This object is valid
//          only within the scope of the XDP_OPEN_INTERFACE routine; interfaces
//          must not use the InterfaceConfig object after returning from this
//          routine.
//          For details on interface configuration, see xdp/interfaceconfig.h.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_OPEN_INTERFACE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_INTERFACE_CONFIG InterfaceConfig
    );

//
// Optional. Releases the resources created when an XDP interface was opened.
//
// If the XDP interface was successfully opened, XDP will eventually invoke the
// close interface routine. The close interface routine will not be invoked
// until all previously-created XDP queues are deleted.
//
// Parameters:
//
//      InterfaceContext:
//          Provides the interface context from registration.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_CLOSE_INTERFACE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext
    );

//
// XDP interface per-receive queue dispatch table.
//
typedef struct _XDP_INTERFACE_RX_QUEUE_DISPATCH {
    //
    // Interface data path notification callback. The XDP platform invokes the
    // interface notification routine to request data path operations.
    //
    XDP_INTERFACE_NOTIFY_QUEUE *InterfaceNotifyQueue;
} XDP_INTERFACE_RX_QUEUE_DISPATCH;

//
// Creates an XDP receive queue. The XDP platform ensures at most one XDP queue
// exists per hardware queue.
//
// The queue creation configuration object specifies the XDP queue parameters,
// including the target queue identifier, and allows interfaces to declare queue
// capabilities and their activation requirements. Upon successful queue
// creation, the interface returns its queue context and XDP dispatch table.
//
// When the XDP platform creates an XDP queue object, the XDP queue is not ready
// to be used on the inspected data path. Instead, interfaces use the queue
// activation callback to retrieve the queue's data path parameters and insert
// the XDP queue into the interface data path.
//
// Under some conditions, XDP will invoke the queue delete routine without
// activating the queue.
//
// Parameters:
//
//      InterfaceContext:
//          Provides the interface context from registration.
//
//      Config:
//          Provides a receive queue creation configuration object. This object
//          is valid only within the scope of the XDP_CREATE_RX_QUEUE routine;
//          interfaces must not use the Config object after returning from this
//          routine.
//          The interface must provide its RX capabilities prior to returning.
//          For more details, see xdp/rxqueueconfig.h.
//
//      InterfaceRxQueue:
//          Upon success, the XDP interface returns its queue context.
//
//      InterfaceRxQueueDispatch:
//          Upon success, the XDP interface returns its queue dispatch table.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_CREATE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    );

//
// Activates an XDP receive queue. The queue activation configuration object
// specifies the activated XDP queue data path state, including shared rings,
// extensions, and offloads. The XDP interface uses this configuration to
// insert the queue into its data path.
//
// Parameters:
//
//      InterfaceRxQueue:
//          The XDP interface queue context returned by queue creation.
//
//      XdpRxQueue:
//          The XDP queue data path handle.
//
//      Config:
//          Provides a receive queue activation configuration object. This
//          object is valid only within the scope of the XDP_ACTIVATE_RX_QUEUE
//          routine; interfaces must not use the Config object after returning
//          from this routine.
//          For more details, see xdp/rxqueueconfig.h.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_ACTIVATE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    );

//
// Deletes an XDP receive queue.
//
// The interface must not invoke XDP callbacks for the queue nor access
// descriptor rings nor access XDP buffers after this routine completes.
//
// Parameters:
//
//      InterfaceRxQueue:
//          The XDP interface queue context returned by queue creation.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_DELETE_RX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    );

//
// XDP interface per-transmit queue dispatch table.
//
typedef struct _XDP_INTERFACE_TX_QUEUE_DISPATCH {
    //
    // Interface data path notification callback. The XDP platform invokes the
    // interface notification routine to request data path operations.
    //
    XDP_INTERFACE_NOTIFY_QUEUE *InterfaceNotifyQueue;
} XDP_INTERFACE_TX_QUEUE_DISPATCH;

//
// Creates an XDP transmit queue. The XDP platform ensures at most one XDP queue
// exists per hardware queue.
//
// The queue creation configuration object specifies the XDP queue parameters,
// including the target queue identifier, and allows interfaces to declare queue
// capabilities and their activation requirements. Upon successful queue
// creation, the interface returns its queue context and XDP dispatch table.
//
// When the XDP platform creates an XDP queue object, the XDP queue is not ready
// to be used on the inspected data path. Instead, interfaces use the queue
// activation callback to retrieve the queue's data path parameters and insert
// the XDP queue into the interface data path.
//
// Under some conditions, XDP will invoke the queue delete routine without
// activating the queue.
//
// Parameters:
//
//      InterfaceContext:
//          Provides the interface context from registration.
//
//      Config:
//          Provides a transmit queue creation configuration object. This object
//          is valid only within the scope of the XDP_CREATE_TX_QUEUE routine;
//          interfaces must not use the Config object after returning from this
//          routine.
//          The interface must provide its TX capabilities prior to returning.
//          For more details, see xdp/txqueueconfig.h.
//
//      InterfaceTxQueue:
//          Upon success, the XDP interface returns its queue context.
//
//      InterfaceTxQueueDispatch:
//          Upon success, the XDP interface returns its queue dispatch table.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_CREATE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    );

//
// Activates an XDP transmit queue. The queue activation configuration object
// specifies the activated XDP queue data path state, including shared rings,
// extensions, and offloads. The XDP interface uses this configuration to
// insert the queue into its data path.
//
// Parameters:
//
//      InterfaceTxQueue:
//          The XDP interface queue context returned by queue creation.
//
//      XdpTxQueue:
//          The XDP queue data path handle.
//
//      Config:
//          Provides a transmit queue activation configuration object. This
//          object is valid only within the scope of the XDP_ACTIVATE_TX_QUEUE
//          routine; interfaces must not use the Config object after returning
//          from this routine.
//          For more details, see xdp/txqueueconfig.h.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XDP_ACTIVATE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    );

//
// Deletes an XDP transmit queue.
//
// The interface must not invoke XDP callbacks for the queue nor access
// descriptor rings nor access XDP buffers after this routine completes.
//
// Parameters:
//
//      InterfaceTxQueue:
//          The XDP interface queue context returned by queue creation.
//
typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_DELETE_TX_QUEUE(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    );

//
// The XDP interface control dispatch table. This dispatch table is provided
// during XDP interface registration.
//
typedef struct _XDP_INTERFACE_DISPATCH {
    XDP_OBJECT_HEADER       Header;
    XDP_OPEN_INTERFACE      *OpenInterface;
    XDP_CLOSE_INTERFACE     *CloseInterface;
    XDP_CREATE_RX_QUEUE     *CreateRxQueue;
    XDP_ACTIVATE_RX_QUEUE   *ActivateRxQueue;
    XDP_DELETE_RX_QUEUE     *DeleteRxQueue;
    XDP_CREATE_TX_QUEUE     *CreateTxQueue;
    XDP_ACTIVATE_TX_QUEUE   *ActivateTxQueue;
    XDP_DELETE_TX_QUEUE     *DeleteTxQueue;
} XDP_INTERFACE_DISPATCH;
```
