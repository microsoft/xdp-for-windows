# XdpOpenApi function

Opens the API and returns an API function table with the rest of the API's
functions. This function is exported by the `xdpapi` DLL.

** This API is deprecated. **

## Syntax

```C
typedef
HRESULT
XDP_OPEN_API_FN(
    _In_ UINT32 XdpApiVersion,
    _Out_ const XDP_API_TABLE **XdpApiTable
    );

XDPAPI XDP_OPEN_API_FN XdpOpenApi;
```

## Parameters

`XdpApiVersion`

The XDP API version required by the caller.

### XDP_API_VERSION_1

This is the minimum version currently supported by XDP.

### XDP_API_VERSION_2

This is the maximum version currently supported by XDP.
This version disables XSK processor affinity options by default.

`XdpApiTable`

A double pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines.

## Remarks

Each `XdpOpenApi` must invoke a corresponding [`XdpCloseApi`](XdpCloseApi.md) when the API will no longer be used.
