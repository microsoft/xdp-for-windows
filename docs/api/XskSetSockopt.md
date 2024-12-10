# XskSetSockopt function

Sets an AF_XDP [socket option](xsk-sockopts.md).

## Syntax

```C
XDP_STATUS
XskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    );
```

## Parameters

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)
