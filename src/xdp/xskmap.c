//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements the XSKMAP object, which is a fixed-size array
// mapping queue IDs (UINT32 keys) to XSK socket handles.
//

#include "precomp.h"
#include "xskmap.h"

//
// Maximum number of entries in an XSKMAP, corresponding to the maximum
// number of RSS indirection table entries in NDIS.
//
#define XSKMAP_MAX_SIZE 128

//
// Single global lock protecting all XSKMAPs. This is for simplicity; the
// built-in XSKMAP is a late feature addition while eBPF-based XSKMAPs are just
// being introduced. The eBPF equivalent maps provide full RCU semantics and
// are the preferred option.
//
static PNDIS_RW_LOCK_EX XdpXskMapLock;

typedef struct _XDP_XSKMAP {
    XDP_FILE_OBJECT_HEADER Header;
    RTL_REFERENCE_COUNT ReferenceCount;

    //
    // Fixed-size array of XSK handles (kernel pointers to XSK objects).
    // NULL indicates an empty slot. Protected by XdpXskMapLock.
    //
    VOID *Entries[XSKMAP_MAX_SIZE];
} XDP_XSKMAP;

static
VOID
XdpXskMapReference(
    _In_ XDP_XSKMAP *XskMap
    )
{
    XdpIncrementReferenceCount(&XskMap->ReferenceCount);
}

static
VOID
XdpXskMapDereference(
    _In_ XDP_XSKMAP *XskMap
    )
{
    if (XdpDecrementReferenceCount(&XskMap->ReferenceCount)) {
        //
        // Release all XSK references held by the map.
        //
        for (UINT32 i = 0; i < XSKMAP_MAX_SIZE; i++) {
            if (XskMap->Entries[i] != NULL) {
                XskDereferenceDatapathHandle(XskMap->Entries[i]);
                XskMap->Entries[i] = NULL;
            }
        }

        ExFreePoolWithTag(XskMap, XDP_POOLTAG_XSKMAP);
    }
}

static XDP_FILE_IRP_ROUTINE XdpXskMapIrpIoControl;
static XDP_FILE_IRP_ROUTINE XdpXskMapIrpClose;
static const XDP_FILE_DISPATCH XdpXskMapFileDispatch = {
    .IoControl = XdpXskMapIrpIoControl,
    .Close = XdpXskMapIrpClose,
};

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpXskMapIrpCreate(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ const XDP_OPEN_PACKET *OpenPacket,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    XDP_XSKMAP *XskMap = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(OpenPacket);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (Disposition != FILE_CREATE) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    XskMap =
        (XDP_XSKMAP *)ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*XskMap), XDP_POOLTAG_XSKMAP);
    if (XskMap == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    XdpInitializeReferenceCount(&XskMap->ReferenceCount);

    XskMap->Header.ObjectType = XDP_OBJECT_TYPE_XSKMAP;
    XskMap->Header.Dispatch = &XdpXskMapFileDispatch;
    IrpSp->FileObject->FsContext = XskMap;

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpXskMapIrpIoControl(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_XSKMAP *XskMap = IrpSp->FileObject->FsContext;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;

    switch (IoControlCode) {

    case IOCTL_XSKMAP_INSERT:
    {
        XDP_XSKMAP_INSERT_PARAMS *Params;
        HANDLE XskKernelHandle = NULL;
        VOID *OldEntry;
        LOCK_STATE_EX LockState;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params)) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Params = (XDP_XSKMAP_INSERT_PARAMS *)Irp->AssociatedIrp.SystemBuffer;

        if (Params->Key >= XSKMAP_MAX_SIZE) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        //
        // Reference the XSK handle in the context of the calling process.
        //
        Status =
            XskReferenceDatapathHandle(
                Irp->RequestorMode, &Params->XskHandle, TRUE, &XskKernelHandle);
        if (!NT_SUCCESS(Status)) {
            break;
        }

        //
        // Insert the entry into the map, replacing any existing entry.
        //
        NdisAcquireRWLockWrite(XdpXskMapLock, &LockState, 0);
        OldEntry = XskMap->Entries[Params->Key];
        XskMap->Entries[Params->Key] = XskKernelHandle;
        NdisReleaseRWLock(XdpXskMapLock, &LockState);

        //
        // Release the old entry's reference, if any.
        //
        if (OldEntry != NULL) {
            XskDereferenceDatapathHandle(OldEntry);
        }

        Status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_XSKMAP_DELETE:
    {
        XDP_XSKMAP_DELETE_PARAMS *Params;
        VOID *OldEntry;
        LOCK_STATE_EX LockState;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params)) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Params = (XDP_XSKMAP_DELETE_PARAMS *)Irp->AssociatedIrp.SystemBuffer;

        if (Params->Key >= XSKMAP_MAX_SIZE) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        //
        // Remove the entry from the map.
        //
        NdisAcquireRWLockWrite(XdpXskMapLock, &LockState, 0);
        OldEntry = XskMap->Entries[Params->Key];
        XskMap->Entries[Params->Key] = NULL;
        NdisReleaseRWLock(XdpXskMapLock, &LockState);

        //
        // Release the old entry's reference, if any.
        //
        if (OldEntry != NULL) {
            XskDereferenceDatapathHandle(OldEntry);
        }

        Status = STATUS_SUCCESS;
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
XdpXskMapIrpClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_XSKMAP *XskMap = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    //
    // N.B.: Closing the map handle does not implicitly clear the map; its
    // entries remain as long as the map is referenced by anything, including by
    // programs.
    //
    XdpXskMapDereference(XskMap);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
VOID
XdpXskMapAcquireRead(
    _Out_ _IRQL_saves_ LOCK_STATE_EX *LockState
    )
{
    NdisAcquireRWLockRead(XdpXskMapLock, LockState, 0);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpXskMapReleaseRead(
    _In_ _IRQL_restores_ LOCK_STATE_EX *LockState
    )
{
    NdisReleaseRWLock(XdpXskMapLock, LockState);
}

_IRQL_requires_(DISPATCH_LEVEL)
VOID *
XdpXskMapLookup(
    _In_ HANDLE XskMapHandle,
    _In_ UINT32 Key
    )
{
    XDP_XSKMAP *XskMap = (XDP_XSKMAP *)XskMapHandle;

    if (Key >= XSKMAP_MAX_SIZE) {
        return NULL;
    }

    //
    // Caller must hold the XSKMAP read lock.
    //
    return XskMap->Entries[Key];
}

NTSTATUS
XdpXskMapReferenceDatapathHandle(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const VOID *HandleBuffer,
    _In_ BOOLEAN HandleBounced,
    _Out_ HANDLE *XskMapHandle
    )
{
    NTSTATUS Status;
    FILE_OBJECT *FileObject = NULL;
    HANDLE TargetHandle;
    XDP_XSKMAP *XskMap = NULL;

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
            TargetHandle, XDP_OBJECT_TYPE_XSKMAP, RequestorMode, FILE_GENERIC_WRITE, &FileObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    XskMap = (XDP_XSKMAP *)FileObject->FsContext;
    XdpXskMapReference(XskMap);
    *XskMapHandle = (HANDLE)XskMap;
    Status = STATUS_SUCCESS;

Exit:

    if (FileObject != NULL) {
        ObDereferenceObject(FileObject);
    }

    return Status;
}

VOID
XdpXskMapDereferenceDatapathHandle(
    _In_ HANDLE XskMapHandle
    )
{
    XDP_XSKMAP *XskMap = (XDP_XSKMAP *)XskMapHandle;

    XdpXskMapDereference(XskMap);
}

NTSTATUS
XdpXskMapStart(
    VOID
    )
{
    //
    // NdisAllocateRWLock accepts a NULL handle when the caller is not an
    // NDIS driver, but its SAL annotations do not allow NULL.
    //
#pragma warning(suppress: 6387)
    XdpXskMapLock = NdisAllocateRWLock(NULL);
    if (XdpXskMapLock == NULL) {
        return STATUS_NO_MEMORY;
    }

    return STATUS_SUCCESS;
}

VOID
XdpXskMapStop(
    VOID
    )
{
    if (XdpXskMapLock != NULL) {
        NdisFreeRWLock(XdpXskMapLock);
        XdpXskMapLock = NULL;
    }
}
