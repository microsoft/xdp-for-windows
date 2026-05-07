//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Common XDP map abstraction. Map type-specific behavior (entry storage,
// value capture and release, lookup) is implemented by a per-type dispatch
// table; common behavior (object lifetime, IRP / IOCTL dispatch, the global
// map read/write lock) is implemented in map.c.
//

typedef struct _XDP_MAP XDP_MAP;
typedef struct _XDP_MAP_TYPE_DISPATCH XDP_MAP_TYPE_DISPATCH;

//
// Type-specific operations.
//
//   Cleanup: release any resources / entry references held by the map.
//   Insert:  capture the value handle (in the caller's process context) and
//            store it at Key, replacing any existing entry. Holds the global
//            map write lock.
//   Delete:  remove any entry at Key. Holds the global map write lock.
//
typedef
VOID
XDP_MAP_CLEANUP(
    _In_ XDP_MAP *Map
    );

typedef
NTSTATUS
XDP_MAP_INSERT(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *ValueHandleBuffer,
    _In_ BOOLEAN HandleBounced
    );

typedef
NTSTATUS
XDP_MAP_DELETE(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key
    );

struct _XDP_MAP_TYPE_DISPATCH {
    XDP_MAP_CLEANUP *Cleanup;
    XDP_MAP_INSERT *Insert;
    XDP_MAP_DELETE *Delete;
};

//
// The base XDP_MAP object. Type-specific implementations embed this as the
// first member of their own struct.
//
struct _XDP_MAP {
    XDP_FILE_OBJECT_HEADER Header;
    RTL_REFERENCE_COUNT ReferenceCount;
    XDP_MAP_TYPE Type;
    const XDP_MAP_TYPE_DISPATCH *TypeDispatch;
};

XDP_FILE_CREATE_ROUTINE XdpMapIrpCreate;

NTSTATUS
XdpMapStart(
    VOID
    );

VOID
XdpMapStop(
    VOID
    );

//
// Reference / dereference an XDP_OBJECT_TYPE_MAP file handle for use on the
// data path.
//
NTSTATUS
XdpMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ XDP_MAP **Map
    );

VOID
XdpMapDereferenceDatapathHandle(
    _In_ XDP_MAP *Map
    );

//
// Returns the type of a map referenced via XdpMapReferenceDatapathHandle.
//
XDP_MAP_TYPE
XdpMapGetType(
    _In_ XDP_MAP *Map
    );

//
// Acquire / release the global map read lock used to synchronize data path
// lookups against control path Insert / Delete operations.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpMapAcquireRead(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpMapReleaseRead(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    );

//
// Internal helpers for use by map type implementations.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpMapAcquireWrite(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpMapReleaseWrite(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    );
