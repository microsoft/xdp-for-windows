# XDP_FRAME_FRAGMENT structure

An XDP frame extension containing information for fragmented (multi-buffer) frames.

## Syntax

```C
typedef struct _XDP_FRAME_FRAGMENT {
    //
    // The number of additional buffers in the frame. These buffers are stored
    // in a separate fragment ring.
    //
    UINT8 FragmentBufferCount;
} XDP_FRAME_FRAGMENT;

#define XDP_FRAME_EXTENSION_FRAGMENT_NAME L"ms_frame_fragment"
#define XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1 1U
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpGetFragmentExtension`](XdpGetFragmentExtension.md)
