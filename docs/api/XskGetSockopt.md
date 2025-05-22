# XskGetSockopt function

Gets an AF_XDP [socket option](xsk-sockopts.md).

## Syntax

```C
XDP_STATUS
XskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_opt_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    );
```

## Parameters

- Socket - An AF_XDP socket handle.
- OptionName - A [socket option](xsk-sockopts.md).
- OptionValue - An optional buffer of size `OptionLength`. If `OptionLength` is nonzero, the buffer must not be `NULL`.
- OptionLength - The size of the `OptionValue` output buffer. If the input value is `0` and `XskGetSockopt` returns `HRESULT_FROM_WIN32(ERROR_MORE_DATA)`, the output value is the required buffer size. If `XskGetSockopt` succeeds, the output value is the number of bytes written to the output buffer.

## Remarks

None.

## See Also

[AF_XDP](../afxdp.md)
