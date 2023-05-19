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
    XdpLwfOffloadQeoAdding,
    XdpLwfOffloadQeoAdded,
    XdpLwfOffloadQeoRemoving,
} XDP_OFFLOAD_QEO_CONNECTION_STATE;

typedef struct _XDP_OFFLOAD_QEO_CONNECTION {
    LIST_ENTRY Entry;
    LIST_ENTRY TransactionEntry;
    XDP_OFFLOAD_QEO_CONNECTION_STATE State;
    XDP_QUIC_CONNECTION ConnectionParams;
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

        if (XdpOffloadQeoEqualConnections(&Connection->ConnectionParams, ConnectionKey)) {
            return Connection;
        }
    }

    return NULL;
}

NTSTATUS
XdpIrpInterfaceOffloadQeoSet(
    _In_ XDP_INTERFACE_OBJECT *InterfaceObject,
    _Inout_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    XDP_OFFLOAD_QEO_SETTINGS *QeoSettings = &InterfaceObject->QeoSettings;
    XDP_OFFLOAD_PARAMS_QEO QeoParams = {0};
    const XDP_QUIC_CONNECTION *ConnectionsIn = Irp->AssociatedIrp.SystemBuffer;
    UINT32 InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 OutputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    UINT32 BytesWritten = 0;
    LIST_ENTRY TransactionConnections;
    BOOLEAN LockHeld = FALSE;
    enum {
        TransactionNone,
        TransactionValidating,
        TransactionValidated,
    } TransactionState = TransactionNone;

    TraceEnter(TRACE_CORE, "Interface=%p", InterfaceObject);

    InitializeListHead(&TransactionConnections);

    if (InputBufferLength == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    QeoParams.Connections = ConnectionsIn;
    QeoParams.ConnectionsSize = InputBufferLength;
    QeoParams.ConnectionCount = 0;

    RtlAcquirePushLockExclusive(&QeoSettings->Lock);
    LockHeld = TRUE;
    TransactionState = TransactionValidating;

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
            OffloadConnection =
                ExAllocatePoolZero(
                    NonPagedPoolNx, sizeof(*OffloadConnection), XDP_POOLTAG_OFFLOAD_QEO);
            if (OffloadConnection == NULL) {
                Status = STATUS_NO_MEMORY;
                goto Exit;
            }

            OffloadConnection->State = XdpLwfOffloadQeoAdding;
            RtlCopyMemory(
                &OffloadConnection->ConnectionParams, ConnectionsIn,
                sizeof(OffloadConnection->ConnectionParams));
            InsertTailList(&TransactionConnections, &OffloadConnection->TransactionEntry);

            if (XdpOffloadQeoFindOffloadedConnection(
                    QeoSettings, &OffloadConnection->ConnectionParams)) {
                Status = STATUS_DUPLICATE_OBJECTID;
                goto Exit;
            }

            //InsertTailList(&QeoSettings->Connections, &OffloadConnection->Entry);

            break;
        case XDP_QUIC_OPERATION_REMOVE:
            OffloadConnection = XdpOffloadQeoFindOffloadedConnection(QeoSettings, ConnectionsIn);
            if (OffloadConnection == NULL) {
                Status = STATUS_NOT_FOUND;
                goto Exit;
            }

            if (OffloadConnection->State != XdpLwfOffloadQeoAdded) {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto Exit;
            }

            OffloadConnection->State = XdpLwfOffloadQeoRemoving;
            ASSERT(IsListEmpty(&OffloadConnection->TransactionEntry));
            InsertTailList(&TransactionConnections, &OffloadConnection->TransactionEntry);

            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        InputBufferLength -= ConnectionsIn->Header.Size;
        ConnectionsIn = RTL_PTR_ADD(ConnectionsIn, ConnectionsIn->Header.Size);
        QeoParams.ConnectionCount++;
    }

    TransactionState = TransactionValidated;
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
            XdpOffloadQeo, &QeoParams, sizeof(QeoParams), Irp->AssociatedIrp.SystemBuffer,
            OutputBufferLength, &BytesWritten);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Irp->IoStatus.Information = BytesWritten;

Exit:

    switch (TransactionState) {
    case TransactionValidating:
        ASSERT(LockHeld);
        ASSERT(!NT_SUCCESS(Status));

        // TODO revert

        break;

    case TransactionValidated:
        if (!NT_SUCCESS(Status)) {
            // TODO revert?
        }

        break;

    case TransactionNone:
        break;
    default:
        FRE_ASSERT(FALSE);
    }

    //
    // To unblock the already significant offload refactoring, just delete each
    // connection entry for now. TODO: something better.
    //

    while (!IsListEmpty(&TransactionConnections)) {
        LIST_ENTRY *Entry = RemoveHeadList(&TransactionConnections);
        XDP_OFFLOAD_QEO_CONNECTION *OffloadConnection =
            CONTAINING_RECORD(Entry, XDP_OFFLOAD_QEO_CONNECTION, TransactionEntry);

        ExFreePoolWithTag(OffloadConnection, XDP_POOLTAG_OFFLOAD_QEO);
    }

    if (LockHeld) {
        RtlReleasePushLockExclusive(&QeoSettings->Lock);
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
XdpOfloadQeoRevertSettings(
    _In_ XDP_IFSET_HANDLE IfSetHandle,
    _In_ XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle,
    _Inout_ XDP_OFFLOAD_QEO_SETTINGS *QeoSettings
    )
{
    //
    // TODO
    //
    UNREFERENCED_PARAMETER(IfSetHandle);
    UNREFERENCED_PARAMETER(InterfaceOffloadHandle);
    UNREFERENCED_PARAMETER(QeoSettings);
}
