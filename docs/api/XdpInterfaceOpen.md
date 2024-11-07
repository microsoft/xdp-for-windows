# XdpInterfaceOpen function

Open a handle to get/set offloads/configurations/properties on an interface.

## Syntax

```C
XDP_STATUS
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );
```

## Parameters

`InterfaceIndex`

The interface index to attach the program.

`InterfaceHandle`

If the interface object is created successfully, returns a Windows handle to the program. The interface object is removed by XDP when the handle is closed via `CloseHandle` and all configurations are reverted.

## Remarks

This API is currently only used by experimental APIs, including RSS and QEO.
