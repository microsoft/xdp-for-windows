# XskNotifyAsync function

The purpose of this API is two-fold:

1. Pokes the underlying driver to continue IO processing
2. Waits on the underlying driver until IO is available

## Syntax

```C
//
// XskNotifyAsync
//
// The purpose of this API is two-fold:
//     1) Pokes the underlying driver to continue IO processing
//     2) Waits on the underlying driver until IO is available
//
//

typedef
HRESULT
XSK_NOTIFY_ASYNC_FN(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ OVERLAPPED *Overlapped
    );
```

## Parameters

`Flags`

One or more [`XSK_NOTIFY_FLAGS`](XSK_NOTIFY_FLAGS.md) flags.

TODO

## Remarks

Apps will commonly need to perform both of these actions at once, so a single API is offered to handle both in a single syscall.

When performing both actions, the poke is executed first. If the poke fails, the API ignores the wait and returns immediately with a failure result. If the poke succeeds, then the wait is executed.

Unlike [`XskNotifySocket`](XskNotifySocket.md), this routine does not perform the wait inline. Instead, if a wait was requested and could not be immediately satisfied, the routine returns HRESULT_FROM_WIN32(ERROR_IO_PENDING) and the overlapped IO will be completed asynchronously. Once the IO has completed, the [`XskGetNotifyAsyncResult`](XskGetNotifyAsyncResult.md) routine may be used to retrieve the result flags.

## See Also

[AF_XDP](../afxdp.md)
