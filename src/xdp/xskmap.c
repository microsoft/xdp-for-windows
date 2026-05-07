//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// XSKMAP-specific implementation. Object lifetime, IRP / IOCTL dispatch, and
// the global map read/write lock are all owned by the common XDP map code in
// map.c; this file only provides the XSKMAP entry storage and the per-type
// callbacks that map.c invokes.
//

#include "precomp.h"
#include "xskmap.h"

//
// Maximum number of entries in an XSKMAP, corresponding to the maximum
// number of RSS indirection table entries in NDIS.
//
#define XSKMAP_MAX_SIZE 128

typedef struct _XDP_XSKMAP {
    XDP_MAP Map;

    //
    // Fixed-size array of XSK handles (kernel pointers to XSK objects).
    // NULL indicates an empty slot. Protected by the global map lock.
    //
    VOID *Entries[XSKMAP_MAX_SIZE];
} XDP_XSKMAP;

const SIZE_T XdpXskMapAllocationSize = sizeof(XDP_XSKMAP);

static XDP_MAP_CLEANUP XdpXskMapCleanup;
static XDP_MAP_INSERT XdpXskMapInsert;
static XDP_MAP_DELETE XdpXskMapDelete;

static
VOID
XdpXskMapCleanup(
    _In_ XDP_MAP *Map
    )
{
    XDP_XSKMAP *XskMap = CONTAINING_RECORD(Map, XDP_XSKMAP, Map);

    for (UINT32 i = 0; i < XSKMAP_MAX_SIZE; i++) {
        if (XskMap->Entries[i] != NULL) {
            XskDereferenceDatapathHandle(XskMap->Entries[i]);
            XskMap->Entries[i] = NULL;
        }
    }
}

static
NTSTATUS
XdpXskMapInsert(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *ValueHandleBuffer,
    _In_ BOOLEAN HandleBounced
    )
{
    XDP_XSKMAP *XskMap = CONTAINING_RECORD(Map, XDP_XSKMAP, Map);
    HANDLE XskKernelHandle = NULL;
    VOID *OldEntry;
    LOCK_STATE_EX LockState;
    NTSTATUS Status;

    if (Key >= XSKMAP_MAX_SIZE) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Reference the XSK handle in the context of the calling process.
    //
    Status =
        XskReferenceDatapathHandle(
            RequestorMode, ValueHandleBuffer, HandleBounced, &XskKernelHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Insert the entry into the map, replacing any existing entry.
    //
    XdpMapAcquireWrite(&LockState);
    OldEntry = XskMap->Entries[Key];
    XskMap->Entries[Key] = XskKernelHandle;
    XdpMapReleaseWrite(&LockState);

    //
    // Release the old entry's reference, if any.
    //
    if (OldEntry != NULL) {
        XskDereferenceDatapathHandle(OldEntry);
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
NTSTATUS
XdpXskMapDelete(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key
    )
{
    XDP_XSKMAP *XskMap = CONTAINING_RECORD(Map, XDP_XSKMAP, Map);
    VOID *OldEntry;
    LOCK_STATE_EX LockState;

    if (Key >= XSKMAP_MAX_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }

    XdpMapAcquireWrite(&LockState);
    OldEntry = XskMap->Entries[Key];
    XskMap->Entries[Key] = NULL;
    XdpMapReleaseWrite(&LockState);

    if (OldEntry != NULL) {
        XskDereferenceDatapathHandle(OldEntry);
    }

    return STATUS_SUCCESS;
}

const XDP_MAP_TYPE_DISPATCH XdpXskMapTypeDispatch = {
    .Cleanup = XdpXskMapCleanup,
    .Insert = XdpXskMapInsert,
    .Delete = XdpXskMapDelete,
};

_IRQL_requires_(DISPATCH_LEVEL)
VOID *
XdpXskMapLookup(
    _In_ XDP_MAP *Map,
    _In_ UINT32 Key
    )
{
    XDP_XSKMAP *XskMap;

    ASSERT(Map->Type == XDP_MAP_TYPE_XSKMAP);

    if (Key >= XSKMAP_MAX_SIZE) {
        return NULL;
    }

    XskMap = CONTAINING_RECORD(Map, XDP_XSKMAP, Map);

    //
    // Caller must hold the global map read lock.
    //
    return XskMap->Entries[Key];
}
