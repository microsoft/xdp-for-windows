# XdpUnloadApi function

Releases the reference to the API returned by XdpOpenApi, then dynamically
unloads XDP. This function is provided inline in XDP headers.

## Syntax

```C
inline
VOID
XdpUnloadApi(
    _In_ XDP_LOAD_API_CONTEXT XdpLoadApiContext,
    _In_ CONST XDP_API_TABLE *XdpApiTable
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
