//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements QEO offload routines.
//

#include "precomp.h"
#include "offloadqeo.tmh"

typedef enum _XDP_OFFLOAD_QEO_CONNECTION_STATE {
    XdpOffloadQeoInvalid,
    XdpOffloadQeoAdding,
    XdpOffloadQeoAdded,
    XdpOffloadQeoRemoving,
} XDP_OFFLOAD_QEO_CONNECTION_STATE;

typedef struct _XDP_OFFLOAD_QEO_CONNECTION {
    LIST_ENTRY Entry;
    XDP_OFFLOAD_QEO_CONNECTION_STATE State;
    HRESULT *OutputResult;
    XDP_OFFLOAD_PARAMS_QEO_CONNECTION Offload;
} XDP_OFFLOAD_QEO_CONNECTION;

static
BOOLEAN
XdpOffloadQeoEqualConnections(
    _In_ const XDP_QUIC_CONNECTION *A,
    _In_ const XDP_QUIC_CONNECTION *B
    )
{
    ASSERT(A->ConnectionIdLength <= sizeof(A->ConnectionId));
    ASSERT(B->ConnectionIdLength <= sizeof(B->ConnectionId));

    return
        A->Direction == B->Direction &&
        A->AddressFamily == B->AddressFamily &&
        A->UdpPort == B->UdpPort &&
        A->ConnectionIdLength == B->ConnectionIdLength &&
        RtlEqualMemory(A->ConnectionId, B->ConnectionId, A->ConnectionIdLength);
}

static
_Requires_lock_held_(QeoSettings->Lock)
XDP_OFFLOAD_QEO_CONNECTION *
XdpOffloadQeoFindOffloadedConnection(
    _In_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings,
    _In_ const XDP_QUIC_CONNECTION *ConnectionKey
    )
{
    LIST_ENTRY *Entry = QeoSettings->Connections.Flink;

    while (Entry != &QeoSettings->Connections) {
        XDP_OFFLOAD_QEO_CONNECTION *Connection =
            CONTAINING_RECORD(Entry, XDP_OFFLOAD_QEO_CONNECTION, Entry);
        Entry = Entry->Flink;

        if (XdpOffloadQeoEqualConnections(&Connection->Offload.Params, ConnectionKey)) {
            return Connection;
        }
    }

    return NULL;
}

static
VOID
XdpOffloadQeoDereferenceConnection(
    _In_ XDP_OFFLOAD_QEO_CONNECTION *Connection
    )
{
    ASSERT(Connection->State == XdpOffloadQeoInvalid);
    ASSERT(IsListEmpty(&Connection->Entry));
    ASSERT(IsListEmpty(&Connection->Offload.TransactionEntry));
    ASSERT(Connection->OutputResult == NULL);

    ExFreePoolWithTag(Connection, XDP_POOLTAG_OFFLOAD_QEO);
}

static
_Requires_exclusive_lock_held_(QeoSettings->Lock)
VOID
XdpOffloadQeoInvalidateConnection(
    _In_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings,
    _In_ XDP_OFFLOAD_QEO_CONNECTION *Connection
    )
{
    UNREFERENCED_PARAMETER(QeoSettings);

    ASSERT(Connection->State != XdpOffloadQeoInvalid);
    ASSERT(!IsListEmpty(&Connection->Entry));
    ASSERT(IsListEmpty(&Connection->Offload.TransactionEntry));
    ASSERT(Connection->OutputResult == NULL);

    Connection->State = XdpOffloadQeoInvalid;
    RemoveEntryList(&Connection->Entry);
    InitializeListHead(&Connection->Entry);
    XdpOffloadQeoDereferenceConnection(Connection);
}

NTSTATUS
XdpIrpInterfaceOffloadQeoSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_OFFLOAD_QEO_SETTINGS *QeoSettings;
    XDP_OFFLOAD_PARAMS_QEO QeoParams = {0};
    const XDP_QUIC_CONNECTION *ConnectionsIn = Irp->AssociatedIrp.SystemBuffer;
    UINT32 InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 OutputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    BOOLEAN RundownAcquired = FALSE;
    BOOLEAN LockHeld = FALSE;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

    InitializeListHead(&QeoParams.Connections);
    QeoParams.ConnectionCount = 0;

    QeoSettings =
        &XdpIfGetOffloadIfSettings(
            InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle)->Qeo;

    if (InputBufferLength == 0 || OutputBufferLength != InputBufferLength) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Acquire an interface offload rundown reference to ensure offload cleanup
    // waits until all QEO pre- and post-processing has completed.
    //
    if (XdpIfAcquireOffloadRundown(InterfaceObject->IfSetHandle)) {
        RundownAcquired = TRUE;
    } else {
        Status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&QeoSettings->Lock);
    LockHeld = TRUE;

    while (InputBufferLength > 0) {
        XDP_OFFLOAD_QEO_CONNECTION *OffloadConnection = NULL;

        //
        // Validate input.
        //

        if (InputBufferLength < sizeof(ConnectionsIn->Header) ||
            InputBufferLength < ConnectionsIn->Header.Size) {
            TraceError(
                TRACE_CORE,
                "Interface=%p Input buffer length too small InputBufferLength=%u",
                InterfaceObject, InputBufferLength);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if (ConnectionsIn->Header.Revision != XDP_QUIC_CONNECTION_REVISION_1 ||
            ConnectionsIn->Header.Size < XDP_SIZEOF_QUIC_CONNECTION_REVISION_1) {
            TraceError(
                TRACE_CORE, "Interface=%p Unsupported revision Revision=%u Size=%u",
                InterfaceObject, ConnectionsIn->Header.Revision, ConnectionsIn->Header.Size);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if ((UINT32)ConnectionsIn->Operation > (UINT32)XDP_QUIC_OPERATION_REMOVE ||
            (UINT32)ConnectionsIn->Direction > (UINT32)XDP_QUIC_DIRECTION_RECEIVE ||
            (UINT32)ConnectionsIn->DecryptFailureAction > (UINT32)XDP_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE ||
            (UINT32)ConnectionsIn->CipherType > (UINT32)XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM ||
            (UINT32)ConnectionsIn->AddressFamily > (UINT32)XDP_QUIC_ADDRESS_FAMILY_INET6 ||
            ConnectionsIn->ConnectionIdLength > sizeof(ConnectionsIn->ConnectionId)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        switch (ConnectionsIn->Operation) {
        case XDP_QUIC_OPERATION_ADD:
            if (XdpOffloadQeoFindOffloadedConnection(QeoSettings, ConnectionsIn)) {
                Status = STATUS_DUPLICATE_OBJECTID;
                goto Exit;
            }

            OffloadConnection =
                ExAllocatePoolZero(
                    NonPagedPoolNx, sizeof(*OffloadConnection), XDP_POOLTAG_OFFLOAD_QEO);
            if (OffloadConnection == NULL) {
                Status = STATUS_NO_MEMORY;
                goto Exit;
            }

            OffloadConnection->State = XdpOffloadQeoAdding;
            RtlCopyMemory(
                &OffloadConnection->Offload.Params, ConnectionsIn,
                sizeof(OffloadConnection->Offload.Params));
            InsertTailList(&QeoParams.Connections, &OffloadConnection->Offload.TransactionEntry);
            InsertTailList(&QeoSettings->Connections, &OffloadConnection->Entry);

            break;
        case XDP_QUIC_OPERATION_REMOVE:
            OffloadConnection = XdpOffloadQeoFindOffloadedConnection(QeoSettings, ConnectionsIn);
            if (OffloadConnection == NULL) {
                Status = STATUS_NOT_FOUND;
                goto Exit;
            }

            if (OffloadConnection->State != XdpOffloadQeoAdded) {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto Exit;
            }

            OffloadConnection->State = XdpOffloadQeoRemoving;
            OffloadConnection->Offload.Params.Operation = XDP_QUIC_OPERATION_REMOVE;
            ASSERT(IsListEmpty(&OffloadConnection->Offload.TransactionEntry));
            InsertTailList(&QeoParams.Connections, &OffloadConnection->Offload.TransactionEntry);

            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        OffloadConnection->Offload.Params.Status = HRESULT_FROM_WIN32(ERROR_IO_PENDING);

        //
        // Store a pointer to the connection's status field in the output
        // buffer. Cast away the const-ness for this field only.
        //
        ASSERT(OffloadConnection->OutputResult == NULL);
        OffloadConnection->OutputResult = (HRESULT *)&ConnectionsIn->Status;

        InputBufferLength -= ConnectionsIn->Header.Size;
        ConnectionsIn = RTL_PTR_ADD(ConnectionsIn, ConnectionsIn->Header.Size);
        QeoParams.ConnectionCount++;
    }

    RtlReleasePushLockExclusive(&QeoSettings->Lock);
    LockHeld = FALSE;

    ASSERT(InputBufferLength == 0);
    InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;

    //
    // Issue the internal request to the interface.
    //
    Status =
        XdpIfSetInterfaceOffload(
            InterfaceObject->IfSetHandle, InterfaceObject->InterfaceOffloadHandle,
            XdpOffloadQeo, &QeoParams, sizeof(QeoParams));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (!LockHeld) {
        RtlAcquirePushLockExclusive(&QeoSettings->Lock);
        LockHeld = TRUE;
    }

    while (!IsListEmpty(&QeoParams.Connections)) {
        LIST_ENTRY *Entry = RemoveHeadList(&QeoParams.Connections);
        XDP_OFFLOAD_QEO_CONNECTION *Connection =
            CONTAINING_RECORD(Entry, XDP_OFFLOAD_QEO_CONNECTION, Offload.TransactionEntry);

        InitializeListHead(&Connection->Offload.TransactionEntry);

        if (NT_SUCCESS(Status)) {
            ASSERT(Connection->OutputResult != NULL);
            *Connection->OutputResult = Connection->Offload.Params.Status;
        }

        Connection->OutputResult = NULL;

        switch (Connection->State) {
        case XdpOffloadQeoAdding:
            if (NT_SUCCESS(Status) && SUCCEEDED(Connection->Offload.Params.Status)) {
                Connection->State = XdpOffloadQeoAdded;
            } else {
                XdpOffloadQeoInvalidateConnection(QeoSettings, Connection);
            }

            break;

        case XdpOffloadQeoRemoving:
            if (NT_SUCCESS(Status) && SUCCEEDED(Connection->Offload.Params.Status)) {
                XdpOffloadQeoInvalidateConnection(QeoSettings, Connection);
            } else {
                Connection->State = XdpOffloadQeoAdded;
                Connection->Offload.Params.Operation = XDP_QUIC_OPERATION_ADD;
                Connection->Offload.Params.Status = S_OK;
            }

            break;

        default:
            FRE_ASSERT(FALSE);
            break;
        }
    }

    if (LockHeld) {
        RtlReleasePushLockExclusive(&QeoSettings->Lock);
    }

    if (RundownAcquired) {
        XdpIfReleaseOffloadRundown(InterfaceObject->IfSetHandle);
    }

    if (NT_SUCCESS(Status)) {
        Irp->IoStatus.Information = OutputBufferLength;
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpOffloadQeoInitializeSettings(
    _Inout_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings
    )
{
    ExInitializePushLock(&QeoSettings->Lock);
    InitializeListHead(&QeoSettings->Connections);
}

VOID
XdpOffloadQeoRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle
    )
{
    XDP_OFFLOAD_QEO_SETTINGS *QeoSettings;
    LIST_ENTRY *Entry;
    NTSTATUS Status;
    static const UINT32 BatchLimit = 10000;

    QeoSettings = &XdpIfGetOffloadIfSettings(IfSetHandle, InterfaceOffloadHandle)->Qeo;

    RtlAcquirePushLockExclusive(&QeoSettings->Lock);

    while (!IsListEmpty(&QeoSettings->Connections)) {
        XDP_OFFLOAD_PARAMS_QEO QeoParams = {0};

        InitializeListHead(&QeoParams.Connections);
        QeoParams.ConnectionCount = 0;

        //
        // Revert connections in batches to avoid creating excessively large
        // OID requests. Arguably this logic belongs at the LWF layer, but it is
        // simpler to perform here.
        //

        Entry = QeoSettings->Connections.Flink;

        while (Entry != &QeoSettings->Connections && QeoParams.ConnectionCount++ < BatchLimit) {
            XDP_OFFLOAD_QEO_CONNECTION *Connection =
                CONTAINING_RECORD(Entry, XDP_OFFLOAD_QEO_CONNECTION, Entry);
            Entry = Entry->Flink;

            FRE_ASSERT(Connection->State == XdpOffloadQeoAdded);
            Connection->State = XdpOffloadQeoRemoving;
            Connection->Offload.Params.Operation = XDP_QUIC_OPERATION_REMOVE;
            Connection->Offload.Params.Status = HRESULT_FROM_WIN32(ERROR_IO_PENDING);
            InsertTailList(&QeoParams.Connections, &Connection->Offload.TransactionEntry);
        }

        RtlReleasePushLockExclusive(&QeoSettings->Lock);

        ASSERT(QeoParams.ConnectionCount > 0 && !IsListEmpty(&QeoParams.Connections));

        Status =
            XdpIfRevertInterfaceOffload(
                IfSetHandle, InterfaceOffloadHandle, XdpOffloadQeo, &QeoParams, sizeof(QeoParams));

        RtlAcquirePushLockExclusive(&QeoSettings->Lock);

        while (!IsListEmpty(&QeoParams.Connections)) {
            Entry = RemoveHeadList(&QeoParams.Connections);
            XDP_OFFLOAD_QEO_CONNECTION *Connection =
                CONTAINING_RECORD(Entry, XDP_OFFLOAD_QEO_CONNECTION, Offload.TransactionEntry);

            FRE_ASSERT(Connection->State == XdpOffloadQeoRemoving);

            InitializeListHead(&Connection->Offload.TransactionEntry);

            if (!NT_SUCCESS(Status) || FAILED(Connection->Offload.Params.Status)) {
                TraceError(
                    TRACE_CORE,
                    "Failed to revert QEO connection from interface "
                    "IfSetHandle=%p InterfaceOffloadHandle=%p Status=%!STATUS! Connection.Status=%!HRESULT!",
                    IfSetHandle, InterfaceOffloadHandle, Status, Connection->Offload.Params.Status);
            }

            XdpOffloadQeoInvalidateConnection(QeoSettings, Connection);
        }
    }

    RtlReleasePushLockExclusive(&QeoSettings->Lock);
}
