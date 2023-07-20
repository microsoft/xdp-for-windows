# XdpCreateProgram function

Creates a new XDP program.

## Syntax

```C
typedef
HRESULT
XDP_CREATE_PROGRAM_FN(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) CONST XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );
```

## Parameters

`InterfaceIndex`

The interface index to attach the program.

`HookId`

The [`XDP_HOOK_ID`](XDP_HOOK_ID.md) specifying the hook point the program is to be attached to.

`QueueId`

The ID specifying the [queue](../queues.md) the program is to be attached to.

`Flags`

One or more of the `XDP_CREATE_PROGRAM_FLAGS` flags:

- `XDP_CREATE_PROGRAM_FLAG_NONE`  
    The flag has no effect.
- `XDP_CREATE_PROGRAM_FLAG_GENERIC`  
    Attach to the interface using the generic XDP provider. The generic provider is always supported.
- `XDP_CREATE_PROGRAM_FLAG_NATIVE`  
    Attach to the interface using the native XDP provider. If the interface does not support native XDP, the attach will fail.
- `XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES`  
    Attach to all XDP queues on the interface.

`Rules`

An array of [`XDP_RULE](XDP_RULE.md) elements specifying the rules to be applied to each frame inspected by the XDP program. The rules are traversed in the order of the array; the first rule matching the frame performs an action; no further rules are processed for that frame.

`RuleCount`

The number of entries in the `Rules` array.

`Program`

If the program is created successfully, returns a Windows handle to the program. The program is removed by XDP when the handle is closed via `CloseHandle`.

## Remarks

Creates and attach an XDP program to an interface. If the caller does not specify the generic or native interface provider, XDP chooses the best provider available on the system.

N.B. The current implementation supports only L2 inspect programs.

## See Also

[`XDP_RULE`](XDP_RULE.md)  
[Queues](../queues.md)  
