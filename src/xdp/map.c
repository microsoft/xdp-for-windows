//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Common driver-side implementation for XDP_OBJECT_TYPE_MAP. Owns the map
// object's lifetime, IRP dispatch, IOCTL dispatch, and the single global
// read/write lock used to synchronize data path lookups against control
// path mutations. Type-specific behavior is delegated to a per-type
// dispatch table populated when the map is created.
//

#include "precomp.h"
#include "map.h"
#include "xskmap.h"

//
// Single global lock protecting the contents of all XDP maps. This is for
// simplicity; the built-in maps are a late feature addition and the eBPF
// equivalent maps provide full RCU semantics and are the preferred option.
//
static PNDIS_RW_LOCK_EX XdpMapLock;

static
VOID
XdpMapReference(
    _In_ XDP_MAP *Map
    )
{
    XdpIncrementReferenceCount(&Map->ReferenceCount);
}

static
VOID
XdpMapDereference(
    _In_ XDP_MAP *Map
    )
{
    if (XdpDecrementReferenceCount(&Map->ReferenceCount)) {
        if (Map->TypeDispatch->Cleanup != NULL) {
            Map->TypeDispatch->Cleanup(Map);
        }

        ExFreePoolWithTag(Map, XDP_POOLTAG_MAP);
    }
}

static
const XDP_MAP_TYPE_DISPATCH *
XdpMapGetTypeDispatch(
    _In_ XDP_MAP_TYPE Type,
    _Out_ SIZE_T *AllocationSize
    )
{
    switch (Type) {
    case XDP_MAP_TYPE_XSKMAP:
        *AllocationSize = XdpXskMapAllocationSize;
        return &XdpXskMapTypeDispatch;
    default:
        *AllocationSize = 0;
        return NULL;
    }
}

static XDP_FILE_IRP_ROUTINE XdpMapIrpIoControl;
static XDP_FILE_IRP_ROUTINE XdpMapIrpClose;
static const XDP_FILE_DISPATCH XdpMapFileDispatch = {
    .IoControl = XdpMapIrpIoControl,
    .Close = XdpMapIrpClose,
};

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpMapIrpCreate(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ const XDP_OPEN_PACKET *OpenPacket,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    const XDP_MAP_OPEN *MapOpen;
    const XDP_MAP_TYPE_DISPATCH *TypeDispatch;
    SIZE_T AllocationSize;
    XDP_MAP *Map = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(OpenPacket);

    if (Disposition != FILE_CREATE) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (InputBufferLength < sizeof(*MapOpen)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    MapOpen = (const XDP_MAP_OPEN *)InputBuffer;

    TypeDispatch = XdpMapGetTypeDispatch(MapOpen->Type, &AllocationSize);
    if (TypeDispatch == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ASSERT(AllocationSize >= sizeof(XDP_MAP));

    Map = (XDP_MAP *)ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_MAP);
    if (Map == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&Map->ReferenceCount);
    Map->Header.ObjectType = XDP_OBJECT_TYPE_MAP;
    Map->Header.Dispatch = &XdpMapFileDispatch;
    Map->Type = MapOpen->Type;
    Map->TypeDispatch = TypeDispatch;
    IrpSp->FileObject->FsContext = Map;

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpMapIrpIoControl(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_MAP *Map = IrpSp->FileObject->FsContext;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;

    switch (IoControlCode) {

    case IOCTL_MAP_INSERT:
    {
        XDP_MAP_INSERT_PARAMS *Params;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params)) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Params = (XDP_MAP_INSERT_PARAMS *)Irp->AssociatedIrp.SystemBuffer;

        Status =
            Map->TypeDispatch->Insert(
                Map, Params->Key, Irp->RequestorMode, &Params->Value, TRUE);
        break;
    }

    case IOCTL_MAP_DELETE:
    {
        XDP_MAP_DELETE_PARAMS *Params;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params)) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Params = (XDP_MAP_DELETE_PARAMS *)Irp->AssociatedIrp.SystemBuffer;

        Status = Map->TypeDispatch->Delete(Map, Params->Key);
        break;
    }

    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpMapIrpClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_MAP *Map = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    //
    // N.B.: Closing the map handle does not implicitly clear the map; its
    // entries remain as long as the map is referenced by anything, including
    // by programs.
    //
    XdpMapDereference(Map);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpMapAcquireRead(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    )
{
    NdisAcquireRWLockRead(XdpMapLock, LockState, 0);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpMapReleaseRead(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    )
{
    NdisReleaseRWLock(XdpMapLock, LockState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpMapAcquireWrite(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    )
{
    NdisAcquireRWLockWrite(XdpMapLock, LockState, 0);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpMapReleaseWrite(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    )
{
    NdisReleaseRWLock(XdpMapLock, LockState);
}

NTSTATUS
XdpMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ XDP_MAP **Map
    )
{
    NTSTATUS Status;
    FILE_OBJECT *FileObject = NULL;
    HANDLE TargetHandle;
    XDP_MAP *ReferencedMap = NULL;

    if (RequestorMode != KernelMode && !HandleBounced) {
        __try {
            ProbeForRead((VOID *)HandleBuffer, sizeof(HANDLE), PROBE_ALIGNMENT(HANDLE));
            TargetHandle = ReadHandleNoFence((volatile const HANDLE *)HandleBuffer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Status = GetExceptionCode();
            goto Exit;
        }
    } else {
        TargetHandle = *(HANDLE *)HandleBuffer;
    }

    Status =
        XdpReferenceObjectByHandle(
            TargetHandle, XDP_OBJECT_TYPE_MAP, RequestorMode, FILE_GENERIC_WRITE, &FileObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ReferencedMap = (XDP_MAP *)FileObject->FsContext;
    XdpMapReference(ReferencedMap);
    *Map = ReferencedMap;
    Status = STATUS_SUCCESS;

Exit:

    if (FileObject != NULL) {
        ObDereferenceObject(FileObject);
    }

    return Status;
}

VOID
XdpMapDereferenceDatapathHandle(
    _In_ XDP_MAP *Map
    )
{
    XdpMapDereference(Map);
}

XDP_MAP_TYPE
XdpMapGetType(
    _In_ XDP_MAP *Map
    )
{
    return Map->Type;
}

NTSTATUS
XdpMapStart(
    VOID
    )
{
    //
    // NdisAllocateRWLock accepts a NULL handle when the caller is not an
    // NDIS driver, but its SAL annotations do not allow NULL.
    //
#pragma warning(suppress: 6387)
    XdpMapLock = NdisAllocateRWLock(NULL);
    if (XdpMapLock == NULL) {
        return STATUS_NO_MEMORY;
    }

    return STATUS_SUCCESS;
}

VOID
XdpMapStop(
    VOID
    )
{
    if (XdpMapLock != NULL) {
        NdisFreeRWLock(XdpMapLock);
        XdpMapLock = NULL;
    }
}
