# XskNotifySocket function

The purpose of this API is two-fold:

1. Pokes the underlying driver to continue IO processing
2. Waits on the underlying driver until IO is available

## Syntax

```C
typedef
HRESULT
XSK_NOTIFY_SOCKET_FN(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    );
```

## Parameters

`Flags`

One or more [`XSK_NOTIFY_FLAGS`](XSK_NOTIFY_FLAGS.md) flags.

TODO

## Remarks

Apps will commonly need to perform both of these actions at once, so a single API is offered to handle both in a single syscall. When performing both actions, the poke is executed first. If the poke fails, the API ignores the wait and returns immediately with a failure result. If the poke succeeds, then the wait is executed. The wait timeout interval can be set to INFINITE to specify that the wait will not time out.

## See Also

[AF_XDP](../afxdp.md)
