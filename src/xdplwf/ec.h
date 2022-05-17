//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Poll callback performs a quantum of work and returns whether more work
// can be performed.
//
typedef
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XDP_EC_POLL_ROUTINE(
    _In_ VOID *Context
    );

typedef struct _XDP_EC {
    XDP_EC_POLL_ROUTINE *Poll;
    VOID *PollContext;
    BOOLEAN Armed;
    BOOLEAN InPoll;
    BOOLEAN SkipYieldCheck;
    BOOLEAN CleanupPassiveThread;
    ULONG *IdealProcessor;
    ULONG OwningProcessor;
    LARGE_INTEGER LastYieldTick;
    KDPC Dpc;
    PKTHREAD PassiveThread;
    KEVENT PassiveEvent;
    KEVENT *CleanupComplete;
} XDP_EC;

//
// Initialize a generic XDP execution context. Each EC serializes an
// asynchronous poll callback with inline callers. The EC is optimized for
// generic RSS workloads where work is highly affinitized to a single CPU and
// tends to execute at dispatch level. The IdealProcessor parameter allows
// the EC to follow the target RSS processor as the indirection table changes.
//
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
XdpEcInitialize(
    _Inout_ XDP_EC *Ec,
    _In_ XDP_EC_POLL_ROUTINE *Poll,
    _In_ VOID *PollContext,
    _In_ ULONG *IdealProcessor
    );

//
// Cleans up the EC. The notify routine must not be invoked.
//
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpEcCleanup(
    _In_ XDP_EC *Ec
    );

//
// Notify the EC has work ready to be performed. The poll callback will be
// invoked until no more work is available.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpEcNotify(
    _In_ XDP_EC *Ec
    );

//
// Notify the EC has work ready to be performed. If the EC is able to serialize
// this request with other work requests (i.e. the poll callback and any other
// inline callers) the enter succeeds.
//
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN
XdpEcEnterInline(
    _In_ XDP_EC *Ec,
    _In_ ULONG CurrentProcessor
    );

//
// Notify the EC the inline work has completed.
//
_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpEcExitInline(
    _In_ XDP_EC *Ec
    );
