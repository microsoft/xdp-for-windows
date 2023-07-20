# XskGetSockopt function

Gets an AF_XDP [socket option](xsk-sockopts.md).

## Syntax

```C
typedef
HRESULT
XSK_GET_SOCKOPT_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    );
```

## Parameters

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)
