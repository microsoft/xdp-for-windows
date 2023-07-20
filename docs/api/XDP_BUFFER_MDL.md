# XDP_BUFFER_MDL structure

An XDP buffer extension containing an MDL mapping of the buffer.

## Syntax

```C
//
// XDP buffer extension containing an MDL mapping of the buffer.
//
typedef struct _XDP_BUFFER_MDL {
    //
    // An MDL mapped to the buffer's physical pages.
    //
    MDL *Mdl;

    //
    // The offset from the start of the MDL to the start of the buffer.
    //
    SIZE_T MdlOffset;
} XDP_BUFFER_MDL;

#define XDP_BUFFER_EXTENSION_MDL_NAME L"ms_buffer_mdl"
#define XDP_BUFFER_EXTENSION_MDL_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetMdlExtension`](XdpGetMdlExtension.md)
