# XdpOpenApi function

Opens the API and returns an API function table with the rest of the API's
functions. This function is exported by the `xdpapi` DLL.

## Syntax

```C
typedef
HRESULT
XDP_OPEN_API_FN(
    _In_ UINT32 XdpApiVersion,
    _Out_ CONST XDP_API_TABLE **XdpApiTable
    );

XDPAPI XDP_OPEN_API_FN XdpOpenApi;
```

## Parameters

`XdpApiVersion`

The XDP API version required by the caller. Currently only `XDP_API_VERSION_1` is supported.

`XdpApiTable`

A double pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines.

## Remarks

Each `XdpOpenApi` must invoke a corresponding [`XdpCloseApi`](XdpCloseApi.md) when the API will no longer be used.
