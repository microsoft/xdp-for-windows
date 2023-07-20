# XDP_BUFFER_VIRTUAL_ADDRESS structure

An XDP buffer extension containing the virtual address of the buffer data.

## Syntax

```C
//
// XDP buffer extension containing the virtual address of the buffer data.
//
typedef struct XDP_BUFFER_VIRTUAL_ADDRESS {
    //
    // Contains the virtual address of a buffer.
    //
    UCHAR *VirtualAddress;
} XDP_BUFFER_VIRTUAL_ADDRESS;

#define XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME L"ms_buffer_virtual_address"
#define XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1 1U

```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetVirtualAddressExtension`](XdpGetVirtualAddressExtension.md)
