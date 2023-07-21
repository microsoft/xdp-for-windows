# XdpOpenApi function

Releases the reference to the API returned by [`XdpOpenApi`](XdpOpenApi.md). This routine is exported by the `xdpapi` DLL.

## Syntax

```C
typedef
VOID
XDP_CLOSE_API_FN(
    _In_ CONST XDP_API_TABLE *XdpApiTable
    );

XDPAPI XDP_CLOSE_API_FN XdpCloseApi;
```

## Parameters

`XdpApiTable`

A pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines previously returned by [`XdpOpenApi`](XdpOpenApi.md).

## Remarks

Each [`XdpOpenApi`](XdpOpenApi.md) must invoke a corresponding `XdpCloseApi` when the API will no longer be used.
