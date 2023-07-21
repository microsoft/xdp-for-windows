# XDP_DMA_CAPABILITIES structure

A structure defining the DMA capabilities of an XDP queue.

## Syntax

```C
//
// Structure defining the DMA capabilities of an XDP queue.
//
typedef struct _XDP_DMA_CAPABILITIES {
    ULONG Size;

    //
    // Specifies the physical device object to map to.
    //
    DEVICE_OBJECT *PhysicalDeviceObject;
} XDP_DMA_CAPABILITIES;
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpInitializeDmaCapabilitiesPdo`](XdpInitializeDmaCapabilitiesPdo.md)
