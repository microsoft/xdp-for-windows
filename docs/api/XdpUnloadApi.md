# XdpUnloadApi function

Releases the reference to the API returned by XdpOpenApi, then dynamically
unloads the `xdpapi.dll` library. This function is provided inline in XDP headers.

**This API is deprecated.** It is provided only for backward compatibility with applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2`. New applications should use `XDP_API_VERSION_3` or later, which provides header-only API implementations that do not require the `xdpapi.dll` library.

## Syntax

```C
inline
VOID
XdpUnloadApi(
    _In_ XDP_LOAD_API_CONTEXT XdpLoadApiContext,
    _In_ const XDP_API_TABLE *XdpApiTable
    );
```

## Parameters

`XdpLoadApiContext`

An pointer to an [`XDP_LOAD_API_CONTEXT`](XDP_LOAD_API_CONTEXT.md) opaque structure tracking the load.

`XdpApiTable`

A pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines.

## Remarks

This routine cannot be called from `DllMain`.

Each [`XdpLoadApi`](XdpLoadApi.md) must invoke a corresponding `XdpUnloadApi` when the API will no longer be used.
