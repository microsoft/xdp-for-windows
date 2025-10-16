# XdpCloseApi function

Releases the reference to the API returned by [`XdpOpenApi`](XdpOpenApi.md). This routine is exported by the `xdpapi.dll` library.

**This API is deprecated.** It is provided only for backward compatibility with applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2`. New applications should use `XDP_API_VERSION_3` or later, which provides header-only API implementations that do not require the `xdpapi.dll` library.

## Syntax

```C
typedef
VOID
XDP_CLOSE_API_FN(
    _In_ const XDP_API_TABLE *XdpApiTable
    );

XDPAPI XDP_CLOSE_API_FN XdpCloseApi;
```

## Parameters

`XdpApiTable`

A pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines previously returned by [`XdpOpenApi`](XdpOpenApi.md).

## Remarks

Each [`XdpOpenApi`](XdpOpenApi.md) must invoke a corresponding `XdpCloseApi` when the API will no longer be used.
