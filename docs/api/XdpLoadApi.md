# XdpLoadApi function

Dynamically loads the `xdpapi.dll` library, then opens the API and returns an API function table with the rest of the API's functions. This function is provided inline in XDP headers.

**This API is deprecated.** It is provided only for backward compatibility with applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2`. New applications should use `XDP_API_VERSION_3` or later, which provides header-only API implementations that do not require the `xdpapi.dll` library.

## Syntax

```C
inline
HRESULT
XdpLoadApi(
    _In_ UINT32 XdpApiVersion,
    _Out_ XDP_LOAD_API_CONTEXT *XdpLoadApiContext,
    _Out_ const XDP_API_TABLE **XdpApiTable
    );
```

## Parameters

`XdpApiVersion`

The XDP API version required by the caller.

`XdpLoadApiContext`

An pointer to an [`XDP_LOAD_API_CONTEXT`](XDP_LOAD_API_CONTEXT.md) opaque structure tracking the load.

`XdpApiTable`

A double pointer to an [`XDP_API_TABLE`](XDP_API_TABLE.md) structure containing a set of API routines.

## Remarks

This routine cannot be called from `DllMain`.

Each `XdpLoadApi` must invoke a corresponding [`XdpUnloadApi`](XdpUnloadApi.md) when the API will no longer be used.
