# XDP_BUFFER_LOGICAL_ADDRESS structure

An XDP buffer extension containing the device-logical (DMA) buffer address.

## Syntax

```C
typedef struct _XDP_BUFFER_LOGICAL_ADDRESS {
    //
    // Contains the device logical address of a buffer.
    //
    UINT64 LogicalAddress;
} XDP_BUFFER_LOGICAL_ADDRESS;

#define XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME L"ms_buffer_logical_address"
#define XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetLogicalAddressExtension`](XdpGetLogicalAddressExtension.md)
