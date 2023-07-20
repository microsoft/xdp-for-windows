# XskCreate function

Creates an AF_XDP socket.

## Syntax

```C
typedef
HRESULT
XSK_CREATE_FN(
    _Out_ HANDLE* Socket
    );
```

## Parameters

TODO

## Remarks

Creates an AF_XDP socket object and returns a handle to it. To close the socket object, call CloseHandle.

## See Also

[AF_XDP](../afxdp.md)
