# XskIoctl function

Performs an AF_XDP [socket option](xsk-sockopts.md) IOCTL.

## Syntax

```C
typedef
HRESULT
XSK_IOCTL_FN(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    );
```

## Parameters

TODO

## Remarks

TODO

## See Also

[AF_XDP](../afxdp.md)
