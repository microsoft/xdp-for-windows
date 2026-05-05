//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _XDP_REDIRECT_BATCH XDP_REDIRECT_BATCH;

XDP_FILE_CREATE_ROUTINE XdpXskMapIrpCreate;

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpXskMapAcquireRead(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpXskMapReleaseRead(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID *
XdpXskMapLookup(
    _In_ HANDLE XskMapHandle,
    _In_ UINT32 Key
    );

NTSTATUS
XdpXskMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskMapHandle
    );

VOID
XdpXskMapDereferenceDatapathHandle(
    _In_ HANDLE XskMapHandle
    );

NTSTATUS
XdpXskMapStart(
    VOID
    );

VOID
XdpXskMapStop(
    VOID
    );
