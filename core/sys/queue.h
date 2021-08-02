//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

//
// Data path routines.
//

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XDP_QUEUE_SYNC_CALLBACK(
    _In_opt_ VOID *CallbackContext
    );

typedef struct _XDP_QUEUE_SYNC {
    KSPIN_LOCK Lock;
    LIST_ENTRY PendingList;
} XDP_QUEUE_SYNC;

typedef struct _XDP_QUEUE_SYNC_ENTRY {
    LIST_ENTRY Link;
    XDP_QUEUE_SYNC_CALLBACK *Callback;
    VOID *CallbackContext;
} XDP_QUEUE_SYNC_ENTRY;

typedef struct _XDP_QUEUE_BLOCKING_SYNC_CONTEXT {
    XDP_QUEUE_SYNC_ENTRY SyncEntry;
    XDP_QUEUE_SYNC_CALLBACK *Callback;
    VOID *CallbackContext;
    KEVENT Event;
} XDP_QUEUE_BLOCKING_SYNC_CONTEXT;

__declspec(noinline)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpQueueDatapathSyncSlow(
    _In_ XDP_QUEUE_SYNC *Sync
    );

FORCEINLINE
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpQueueDatapathSync(
    _In_ XDP_QUEUE_SYNC *Sync
    )
{
    if (ReadPointerNoFence(&Sync->PendingList.Flink) != &Sync->PendingList) {
        XdpQueueDatapathSyncSlow(Sync);
    }
}

VOID
XdpQueueSyncInitialize(
    _Out_ XDP_QUEUE_SYNC *Sync
    );

VOID
XdpQueueSyncInsert(
    _In_ XDP_QUEUE_SYNC *Sync,
    _In_ XDP_QUEUE_SYNC_ENTRY *Entry,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    );

VOID
XdpQueueBlockingSyncInsert(
    _In_ XDP_QUEUE_SYNC *Sync,
    _In_ XDP_QUEUE_BLOCKING_SYNC_CONTEXT *Entry,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    );

#if DBG
typedef struct _XDP_DBG_QUEUE_EC {
    KSPIN_LOCK Lock;
    BOOLEAN Active;
    UINT32 NotifyFlags;
    LARGE_INTEGER NotifyQpc;
    LARGE_INTEGER FlushQpc;
    UINT32 NotifyCpu;
    UINT32 FlushCpu;
} XDP_DBG_QUEUE_EC;

VOID
XdpDbgInitializeQueueEc(
    _Out_ XDP_DBG_QUEUE_EC *Ec
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgEnterQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec,
    _In_ BOOLEAN Flush
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgExitQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpDbgNotifyQueueEc(
    _Inout_ XDP_DBG_QUEUE_EC *Ec,
    _In_ UINT32 NotifyFlags
    );

#define XdbgInitializeQueueEc(T)            XdpDbgInitializeQueueEc(&(T)->DbgEc)
#define XdbgEnterQueueEc(T, Flush)          XdpDbgEnterQueueEc(&(T)->DbgEc, Flush)
#define XdbgExitQueueEc(T)                  XdpDbgExitQueueEc(&(T)->DbgEc)
#define XdbgNotifyQueueEc(T, NotifyFlags)   XdpDbgNotifyQueueEc(&(T)->DbgEc, NotifyFlags)

#else

#define XdbgInitializeQueueEc(T)
#define XdbgEnterQueueEc(T, Flush)
#define XdbgExitQueueEc(T)
#define XdbgNotifyQueueEc(T, NotifyFlags)

#endif
