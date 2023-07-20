# XDP_API_TABLE structure

A structure containing XDP API functions.

## Syntax

```C
typedef struct _XDP_API_TABLE {
    XDP_OPEN_API_FN *XdpOpenApi;
    XDP_CLOSE_API_FN *XdpCloseApi;
    XDP_GET_ROUTINE_FN *XdpGetRoutine;
    XDP_CREATE_PROGRAM_FN *XdpCreateProgram;
    XDP_INTERFACE_OPEN_FN *XdpInterfaceOpen;
    XSK_CREATE_FN *XskCreate;
    XSK_BIND_FN *XskBind;
    XSK_ACTIVATE_FN *XskActivate;
    XSK_NOTIFY_SOCKET_FN *XskNotifySocket;
    XSK_NOTIFY_ASYNC_FN *XskNotifyAsync;
    XSK_GET_NOTIFY_ASYNC_RESULT_FN *XskGetNotifyAsyncResult;
    XSK_SET_SOCKOPT_FN *XskSetSockopt;
    XSK_GET_SOCKOPT_FN *XskGetSockopt;
    XSK_IOCTL_FN *XskIoctl;
} XDP_API_TABLE;
```

## Members

TODO

## Remarks

TODO

## See Also

[`XdpOpenApi`](XdpOpenApi.md)  
[`XdpLoadApi`](XdpLoadApi.md)  
[`XdpCloseApi`](XdpCloseApi.md)  
[`XdpUnloadApi`](XdpUnloadApi.md)  
