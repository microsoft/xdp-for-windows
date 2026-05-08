# XDP_LOAD_API_CONTEXT structure

An opaque structure used to track `xdpapi.dll` library loads and unloads.

**This structure is deprecated.** It is provided only for backward compatibility with applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2`. New applications should use `XDP_API_VERSION_3` or later, which provides header-only API implementations that do not require the `xdpapi.dll` library.

## Syntax

```C
typedef struct _XDP_LOAD_CONTEXT *XDP_LOAD_API_CONTEXT;
```

## Remarks

TODO

## See Also

[`XdpLoadApi`](XdpLoadApi.md)  
[`XdpUnloadApi`](XdpUnloadApi.md)  
