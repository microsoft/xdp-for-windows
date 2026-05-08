//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_MAP XDP_MAP;

typedef
VOID
XDP_MAP_CLEANUP(
    _In_ XDP_MAP *Map
    );

//
// Type-specific Insert callback. Key and Value are raw pointers in the
// caller's address space (or kernel pointers if RequestorMode is KernelMode).
// The callback is responsible for safely probing/capturing both buffers
// according to the map type's known key/value layout.
//
typedef
NTSTATUS
XDP_MAP_INSERT(
    _In_ XDP_MAP *Map,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *Key,
    _In_ const VOID *Value
    );

//
// Type-specific Delete callback. Key is a raw pointer in the caller's address
// space; the callback is responsible for safely probing it.
//
typedef
NTSTATUS
XDP_MAP_DELETE(
    _In_ XDP_MAP *Map,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *Key
    );

typedef struct _XDP_MAP_TYPE_DISPATCH {
    XDP_MAP_CLEANUP *Cleanup;
    XDP_MAP_INSERT *Insert;
    XDP_MAP_DELETE *Delete;
} XDP_MAP_TYPE_DISPATCH;

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

NTSTATUS
XdpMapReadUInt32FromMode(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *Buffer,
    _Out_ UINT32 *Value
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
